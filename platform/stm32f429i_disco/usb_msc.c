/* USB stick (mass storage) on the DISCO's USB USER port: OTG HS core with the embedded full-speed PHY, ST USB Host
 * library (MSC class) and FatFs disk functions. Written for this repo; not derived from the DOOM/ tree.
 * Needs SYSCLK 168 MHz (pge_board_init_ex(1)) for the 48 MHz USB clock. */
#include "stm32f4xx_hal.h"
#include "usbh_core.h"
#include "usbh_msc.h"
#include "ff.h"
#include "diskio.h"
#include "board.h"

static HCD_HandleTypeDef s_hcd;
static USBH_HandleTypeDef s_host;
static int s_started;
static MSC_HandleTypeDef s_msc_mem;

void* pge_usbh_malloc(size_t n) { return n <= sizeof s_msc_mem ? &s_msc_mem : NULL; }   /* single class; reused after a re-enumeration */

/* ---- HAL MSP + callbacks (ISR context: only these calls, nothing that waits) ------------------ */
void HAL_HCD_MspInit(HCD_HandleTypeDef* h) {
  (void)h;
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  GPIO_InitTypeDef g = {0};
  g.Pin = GPIO_PIN_14 | GPIO_PIN_15;   /* D- / D+ */
  g.Mode = GPIO_MODE_AF_PP;
  g.Pull = GPIO_NOPULL;
  g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  g.Alternate = GPIO_AF12_OTG_HS_FS;
  HAL_GPIO_Init(GPIOB, &g);
  g.Pin = GPIO_PIN_13;   /* VBUS sense */
  g.Mode = GPIO_MODE_INPUT;
  g.Alternate = 0;
  HAL_GPIO_Init(GPIOB, &g);
  __HAL_RCC_USB_OTG_HS_CLK_ENABLE();
  g.Pin = GPIO_PIN_4;   /* VBUS power switch enable, active low: start with it off */
  g.Mode = GPIO_MODE_OUTPUT_PP;
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_4, GPIO_PIN_SET);
  HAL_GPIO_Init(GPIOC, &g);
  HAL_Delay(200);   /* let VBUS fall */
  HAL_NVIC_SetPriority(OTG_HS_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(OTG_HS_IRQn);
}
void HAL_HCD_MspDeInit(HCD_HandleTypeDef* h) { (void)h; __HAL_RCC_USB_OTG_HS_CLK_DISABLE(); }

void HAL_HCD_SOF_Callback(HCD_HandleTypeDef* h) { USBH_LL_IncTimer(h->pData); }
void HAL_HCD_Connect_Callback(HCD_HandleTypeDef* h) { USBH_LL_Connect(h->pData); }
void HAL_HCD_Disconnect_Callback(HCD_HandleTypeDef* h) { USBH_LL_Disconnect(h->pData); }
void HAL_HCD_PortEnabled_Callback(HCD_HandleTypeDef* h) { USBH_LL_PortEnabled(h->pData); }
void HAL_HCD_PortDisabled_Callback(HCD_HandleTypeDef* h) { USBH_LL_PortDisabled(h->pData); }
void HAL_HCD_HC_NotifyURBChange_Callback(HCD_HandleTypeDef* h, uint8_t chnum, HCD_URBStateTypeDef urb_state) {
  (void)h; (void)chnum; (void)urb_state;
}
void OTG_HS_IRQHandler(void) { HAL_HCD_IRQHandler(&s_hcd); }

/* ---- USB Host library low-level interface ------------------------------------------------------ */
USBH_StatusTypeDef USBH_LL_Init(USBH_HandleTypeDef* phost) {
  s_hcd.Instance = USB_OTG_HS;
  s_hcd.Init.Host_channels = 11;
  s_hcd.Init.dma_enable = 0;
  s_hcd.Init.low_power_enable = 0;
  s_hcd.Init.phy_itface = HCD_PHY_EMBEDDED;
  s_hcd.Init.Sof_enable = 0;
  s_hcd.Init.speed = HCD_SPEED_HIGH;
  s_hcd.Init.use_external_vbus = 1;
  s_hcd.pData = phost;
  phost->pData = &s_hcd;
  if (HAL_HCD_Init(&s_hcd) != HAL_OK) return USBH_FAIL;
  USBH_LL_SetTimer(phost, HAL_HCD_GetCurrentFrame(&s_hcd));
  return USBH_OK;
}
USBH_StatusTypeDef USBH_LL_DeInit(USBH_HandleTypeDef* phost) { HAL_HCD_DeInit(phost->pData); return USBH_OK; }
USBH_StatusTypeDef USBH_LL_Start(USBH_HandleTypeDef* phost) { HAL_HCD_Start(phost->pData); return USBH_OK; }
USBH_StatusTypeDef USBH_LL_Stop(USBH_HandleTypeDef* phost) { HAL_HCD_Stop(phost->pData); return USBH_OK; }
USBH_StatusTypeDef USBH_LL_ResetPort(USBH_HandleTypeDef* phost) { HAL_HCD_ResetPort(phost->pData); return USBH_OK; }

USBH_SpeedTypeDef USBH_LL_GetSpeed(USBH_HandleTypeDef* phost) {
  switch (HAL_HCD_GetCurrentSpeed(phost->pData)) {
    case 0: return USBH_SPEED_HIGH;
    case 2: return USBH_SPEED_LOW;
    default: return USBH_SPEED_FULL;   /* 1 and anything unexpected */
  }
}
uint32_t USBH_LL_GetLastXferSize(USBH_HandleTypeDef* phost, uint8_t pipe) {
  return HAL_HCD_HC_GetXferCount(phost->pData, pipe);
}
USBH_StatusTypeDef USBH_LL_OpenPipe(USBH_HandleTypeDef* phost, uint8_t pipe, uint8_t epnum, uint8_t dev_address,
                                    uint8_t speed, uint8_t ep_type, uint16_t mps) {
  HAL_HCD_HC_Init(phost->pData, pipe, epnum, dev_address, speed, ep_type, mps);
  return USBH_OK;
}
USBH_StatusTypeDef USBH_LL_ClosePipe(USBH_HandleTypeDef* phost, uint8_t pipe) {
  HAL_HCD_HC_Halt(phost->pData, pipe);
  return USBH_OK;
}
USBH_StatusTypeDef USBH_LL_SubmitURB(USBH_HandleTypeDef* phost, uint8_t pipe, uint8_t direction, uint8_t ep_type,
                                     uint8_t token, uint8_t* pbuff, uint16_t length, uint8_t do_ping) {
  HAL_HCD_HC_SubmitRequest(phost->pData, pipe, direction, ep_type, token, pbuff, length, do_ping);
  return USBH_OK;
}
USBH_URBStateTypeDef USBH_LL_GetURBState(USBH_HandleTypeDef* phost, uint8_t pipe) {
  return (USBH_URBStateTypeDef)HAL_HCD_HC_GetURBState(phost->pData, pipe);
}
USBH_StatusTypeDef USBH_LL_DriverVBUS(USBH_HandleTypeDef* phost, uint8_t state) {
  (void)phost;
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_4, state ? GPIO_PIN_RESET : GPIO_PIN_SET);   /* active low */
  HAL_Delay(200);
  return USBH_OK;
}
USBH_StatusTypeDef USBH_LL_SetToggle(USBH_HandleTypeDef* phost, uint8_t pipe, uint8_t toggle) {
  (void)phost;
  if (s_hcd.hc[pipe].ep_is_in) s_hcd.hc[pipe].toggle_in = toggle;
  else s_hcd.hc[pipe].toggle_out = toggle;
  return USBH_OK;
}
uint8_t USBH_LL_GetToggle(USBH_HandleTypeDef* phost, uint8_t pipe) {
  (void)phost;
  return s_hcd.hc[pipe].ep_is_in ? s_hcd.hc[pipe].toggle_in : s_hcd.hc[pipe].toggle_out;
}
void USBH_Delay(uint32_t Delay) { HAL_Delay(Delay); }

/* ---- readiness ---------------------------------------------------------------------------------- */
static int unit_ready(void) {
  return s_started && s_host.gState == HOST_CLASS && s_host.pActiveClass != NULL && USBH_MSC_IsReady(&s_host) &&
         USBH_MSC_UnitIsReady(&s_host, 0);
}

static void on_event(USBH_HandleTypeDef* h, uint8_t id) {   /* runs from USBH_Process, thread context */
  (void)h;
  pge_dbg_printf("usb: event %u\n", id);
}

/* The host state machine is pumped only here (mount time): the stick must be present when the game mounts storage.
 * Plain reads/writes after that do not need USBH_Process. */
static int wait_ready(void) {
  if (!s_started) {
    USBH_Init(&s_host, on_event, 0);
    USBH_RegisterClass(&s_host, USBH_MSC_CLASS);
    USBH_Start(&s_host);
    s_started = 1;
  }
  const uint32_t t0 = HAL_GetTick();
  while (!unit_ready()) {
    USBH_Process(&s_host);
    const uint32_t dt = HAL_GetTick() - t0;
    if (!s_host.device.is_connected && dt > 1500) { pge_dbg_puts("usb: no device\n"); return -1; }
    if (dt > 6000) { pge_dbg_printf("usb: timeout gState=%d\n", (int)s_host.gState); return -1; }
  }
  MSC_LUNTypeDef info;
  /* FatFs here is built for 512 B sectors; a bigger block would overflow its buffers */
  if (USBH_MSC_GetLUNInfo(&s_host, 0, &info) != USBH_OK || info.capacity.block_size != 512) {
    pge_dbg_puts("usb: lun info failed or block size != 512\n");
    return -1;
  }
  pge_dbg_printf("usb: msc ready blocks=%lu\n", (unsigned long)info.capacity.block_nbr);
  return 0;
}

/* SCSI SYNCHRONIZE CACHE(10); the Cube MSC class has no call for it, and without it some sticks lose their write
 * cache when the board resets and cuts VBUS. Best effort. */
static void sync_cache(void) {
  if (!unit_ready()) return;
  MSC_HandleTypeDef* m = (MSC_HandleTypeDef*)s_host.pActiveClass->pData;
  if (m->unit[0].state != MSC_IDLE) return;
  memset(m->hbot.cbw.field.CB, 0, CBW_CB_LENGTH);
  m->hbot.cbw.field.CB[0] = 0x35;
  m->hbot.cbw.field.CBLength = CBW_LENGTH;
  m->hbot.cbw.field.DataTransferLength = 0;
  m->hbot.cbw.field.Flags = USB_EP_DIR_OUT;
  m->hbot.state = BOT_SEND_CBW;
  m->hbot.cmd_state = BOT_CMD_WAIT;
  const uint32_t t0 = HAL_GetTick();
  USBH_StatusTypeDef st;
  do { st = USBH_MSC_BOT_Process(&s_host, 0); } while (st == USBH_BUSY && HAL_GetTick() - t0 < 2000);
  m->hbot.state = BOT_SEND_CBW;   /* always leave BOT ready for the next command */
  m->hbot.cmd_state = BOT_CMD_SEND;
  if (st != USBH_OK) pge_dbg_printf("usb: sync st=%d\n", (int)st);
}

/* ---- FatFs disk functions ----------------------------------------------------------------------- */
DSTATUS disk_status(BYTE pdrv) { return (pdrv == 0 && unit_ready()) ? 0 : STA_NOINIT; }
DSTATUS disk_initialize(BYTE pdrv) { return (pdrv == 0 && wait_ready() == 0) ? 0 : STA_NOINIT; }

DRESULT disk_read(BYTE pdrv, BYTE* buff, DWORD sector, UINT count) {
  if (pdrv || !unit_ready()) return RES_NOTRDY;
  return USBH_MSC_Read(&s_host, 0, sector, buff, count) == USBH_OK ? RES_OK : RES_ERROR;
}
DRESULT disk_write(BYTE pdrv, const BYTE* buff, DWORD sector, UINT count) {
  if (pdrv || !unit_ready()) return RES_NOTRDY;
  return USBH_MSC_Write(&s_host, 0, sector, (BYTE*)buff, count) == USBH_OK ? RES_OK : RES_ERROR;
}
DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void* buff) {
  (void)buff;
  if (pdrv) return RES_PARERR;
  if (cmd == CTRL_SYNC) { sync_cache(); return RES_OK; }   /* _MAX_SS == _MIN_SS and no mkfs: FatFs only asks for CTRL_SYNC */
  return RES_PARERR;
}
