/* hal/display.h on the LTDC, 240x320 RGB565 panel, framebuffers in SDRAM (FB0 0xD0000000, FB1 0xD0040000).
 *  - logical 320x240: landscape-transposed, CPU transpose copy (default; PGE_F429_ROT picks the direction).
 *  - logical 240x320: portrait-native, rows copied as is: memcpy, or DMA2D M2M when pge_f429_set_opts(1, ..).
 *  - backbuf=1: flush writes FB(back); frame_end reloads LTDC at vblank and swaps (PGE_DISP_VSYNC).
 *    backbuf=0: flush writes the displayed buffer (tearing accepted).
 * Hardware init runs on the first pge_hal_display_init only; later calls just switch orientation and options. */
#include <string.h>
#include "board.h"
#include "pge/hal/display.h"
#include "stm32f429i_discovery_lcd.h"

#ifndef PGE_F429_ROT
#define PGE_F429_ROT 0  /* 1: panelX = 239 - y, panelY = x.  0: panelX = y, panelY = 319 - x */
#endif

#define FB0_ADDR 0xD0000000u
#define FB1_ADDR 0xD0040000u
#define PAN_W 240u
#define PAN_H 320u
#define FB_PIXELS (PAN_W * PAN_H)

extern LTDC_HandleTypeDef LtdcHandler;   /* defined in the BSP lcd driver */

static DMA2D_HandleTypeDef s_dma2d;
static int s_hw_ready, s_dma2d_ready;
static int s_opt_dma2d, s_opt_backbuf;   /* requested via pge_f429_set_opts */
static int s_portrait, s_use_dma2d, s_vsync;   /* active since the last init */
static uint32_t s_lw = 320u, s_lh = 240u;      /* logical size */
static uint16_t* s_front = (uint16_t*)FB0_ADDR;
static uint16_t* s_back = (uint16_t*)FB0_ADDR;   /* flush target; == s_front when not vsync */

void pge_f429_set_opts(int dma2d, int backbuf) {
  s_opt_dma2d = dma2d;
  s_opt_backbuf = backbuf;
}

static int dma2d_init(void) {
  if (s_dma2d_ready) return 0;
  __HAL_RCC_DMA2D_CLK_ENABLE();
  s_dma2d.Instance = DMA2D;
  s_dma2d.Init.Mode = DMA2D_M2M;
  s_dma2d.Init.ColorMode = DMA2D_OUTPUT_RGB565;
  s_dma2d.Init.OutputOffset = 0;
  if (HAL_DMA2D_Init(&s_dma2d) != HAL_OK) return -1;
  s_dma2d.LayerCfg[1].InputOffset = 0;
  s_dma2d.LayerCfg[1].InputColorMode = DMA2D_INPUT_RGB565;
  s_dma2d.LayerCfg[1].AlphaMode = DMA2D_NO_MODIF_ALPHA;
  s_dma2d.LayerCfg[1].InputAlpha = 0;
  if (HAL_DMA2D_ConfigLayer(&s_dma2d, 1) != HAL_OK) return -1;
  s_dma2d_ready = 1;
  return 0;
}

pge_error pge_hal_display_init(uint16_t want_w, uint16_t want_h, pge_display_info* out) {
  if (!out) return -1;
  int portrait;
  if (want_w == 320u && want_h == 240u) portrait = 0;
  else if (want_w == 240u && want_h == 320u) portrait = 1;
  else return -1;

  if (!s_hw_ready) {
    if (BSP_LCD_Init() != LCD_OK) return -1;   /* also inits SDRAM */
    LTDC_LayerCfgTypeDef cfg = {0};
    cfg.WindowX0 = 0;
    cfg.WindowX1 = PAN_W;
    cfg.WindowY0 = 0;
    cfg.WindowY1 = PAN_H;
    cfg.PixelFormat = LTDC_PIXEL_FORMAT_RGB565;
    cfg.FBStartAdress = FB0_ADDR;
    cfg.Alpha = 255;
    cfg.Alpha0 = 0;
    cfg.BlendingFactor1 = LTDC_BLENDING_FACTOR1_PAxCA;   /* same as the BSP layer init */
    cfg.BlendingFactor2 = LTDC_BLENDING_FACTOR2_PAxCA;
    cfg.ImageWidth = PAN_W;
    cfg.ImageHeight = PAN_H;
    if (HAL_LTDC_ConfigLayer(&LtdcHandler, &cfg, 0) != HAL_OK) return -1;   /* enables the layer, immediate reload */
    s_hw_ready = 1;
  } else {   /* a previous run may have left FB1 on screen */
    LTDC_Layer1->CFBAR = FB0_ADDR;
    LTDC->SRCR = LTDC_SRCR_IMR;
  }
  memset((void*)FB0_ADDR, 0, FB_PIXELS * 2u);
  memset((void*)FB1_ADDR, 0, FB_PIXELS * 2u);

  s_portrait = portrait;
  s_lw = want_w;
  s_lh = want_h;
  s_use_dma2d = portrait && s_opt_dma2d;   /* DMA2D only helps the straight copy; landscape stays CPU transpose */
  if (s_use_dma2d && dma2d_init() != 0) return -1;
  s_vsync = s_opt_backbuf != 0;
  s_front = (uint16_t*)FB0_ADDR;
  s_back = s_vsync ? (uint16_t*)FB1_ADDR : s_front;

  out->panel_w = PAN_W;
  out->panel_h = PAN_H;
  out->flags = (portrait ? 0u : PGE_DISP_TRANSPOSED) | (s_vsync ? PGE_DISP_VSYNC : 0u);
  return 0;
}

pge_error pge_hal_display_flush(pge_rect r, uint16_t* px) {
  if (!px || !r.w || !r.h || (uint32_t)r.x + r.w > s_lw || (uint32_t)r.y + r.h > s_lh) return -1;
  uint16_t* fb = s_back;
  if (s_portrait) {
    uint16_t* dst = fb + (uint32_t)r.y * PAN_W + r.x;
    if (s_use_dma2d) {
      DMA2D->OOR = PAN_W - r.w;   /* HAL_DMA2D_Start leaves OOR alone */
      if (HAL_DMA2D_Start(&s_dma2d, (uint32_t)px, (uint32_t)dst, r.w, r.h) != HAL_OK) return -1;
      return HAL_DMA2D_PollForTransfer(&s_dma2d, 100) == HAL_OK ? 0 : -1;
    }
    if (r.w == PAN_W) {
      memcpy(dst, px, (size_t)r.w * r.h * 2u);
    } else {
      for (uint32_t j = 0; j < r.h; j++) memcpy(dst + j * PAN_W, px + j * r.w, (size_t)r.w * 2u);
    }
    return 0;
  }
  /* Outer loop over logical x = panel row, so the SDRAM writes are sequential. */
  for (uint32_t i = 0; i < r.w; i++) {
    uint32_t x = r.x + i;
#if PGE_F429_ROT
    uint16_t* dst = fb + x * PAN_W + (PAN_W - 1u - r.y);
    for (uint32_t j = 0; j < r.h; j++) *dst-- = px[j * r.w + i];
#else
    uint16_t* dst = fb + (PAN_H - 1u - x) * PAN_W + r.y;
    for (uint32_t j = 0; j < r.h; j++) *dst++ = px[j * r.w + i];
#endif
  }
  return 0;
}

pge_error pge_hal_display_wait(void) { return 0; }   /* flush is synchronous */
void pge_hal_display_frame_begin(void) {}

void pge_hal_display_frame_end(void) {
  if (!s_vsync) return;
  LTDC_Layer1->CFBAR = (uint32_t)s_back;
  LTDC->SRCR = LTDC_SRCR_VBR;                  /* reload at the next vertical blanking */
  while (LTDC->SRCR & LTDC_SRCR_VBR) {}
  uint16_t* t = s_front;
  s_front = s_back;
  s_back = t;
}

/* Micro benchmark helper: fill the whole write framebuffer with one RGB565 colour. */
void pge_f429_bench_fill(int use_dma2d, uint16_t color) {
  if (use_dma2d && dma2d_init() == 0) {
    DMA2D->CR = DMA2D_R2M;                       /* Mode = R2M, START clear */
    DMA2D->OPFCCR = DMA2D_OUTPUT_RGB565;
    DMA2D->OCOLR = color;
    DMA2D->OMAR = (uint32_t)s_back;
    DMA2D->OOR = 0;
    DMA2D->NLR = (PAN_W << DMA2D_NLR_PL_Pos) | PAN_H;
    DMA2D->CR |= DMA2D_CR_START;
    while (DMA2D->CR & DMA2D_CR_START) {}
    DMA2D->IFCR = 0x3Fu;                         /* a stale TCIF would end the next HAL poll early */
    DMA2D->CR = DMA2D_M2M;                       /* back to the mode HAL_DMA2D_Init set */
    return;
  }
  uint32_t* p = (uint32_t*)s_back;
  uint32_t v = (uint32_t)color | ((uint32_t)color << 16);
  for (uint32_t i = 0; i < FB_PIXELS / 2u; i++) p[i] = v;
}
