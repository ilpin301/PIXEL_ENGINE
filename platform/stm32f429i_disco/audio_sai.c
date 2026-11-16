/* hal/audio.h on the STM32F429I-DISCO: SAI1 block A, I2S master transmitter, circular DMA2 stream 1 (channel 0), to a
 * MAX98357A-class amp (no MCLK needed): PE4 FS_A, PE5 SCK_A, PE6 SD_A (all AF6).  PE2 (MCLK) is not configured.
 *
 * Audio kernel clock from PLLI2S (the LTDC pixel clock on PLLSAI is untouched).  HSE 8 MHz / PLLM 8 = 1 MHz VCO input:
 *   44.1 kHz family: N=429, Q=2, DivQ=19: 429 MHz / 2 / 19 = 11.28947 MHz (target 11.2896 MHz, -0.001 %)
 *   48 kHz family:   N=344, Q=7, DivQ=1:  344 MHz / 7 / 1  = 49.14286 MHz (target 49.152 MHz = 4 x 12.288, -0.019 %)
 * The 12.288 MHz kernel of the plain 48k family cannot make 32 kHz or 16 kHz: the SAI divider is MCKDIV*2 (or 1 for
 * MCKDIV = 0) and fs = kernel / (divider * 256), so 12.288 MHz would need divider 1.5 / 3.  49.152 MHz gives
 * dividers 4 / 6 / 12 / 24 for 48 / 32 / 16 / 8 kHz, 11.2896 MHz gives 1 / 2 / 4 for 44.1 / 22.05 / 11.025 kHz.
 * HAL_SAI_Init derives MCKDIV itself from the RCC registers (SAI_GetInputClock) and Init.AudioFrequency.
 *
 * The DMA buffer is plain .bss in SRAM1/2 (DMA cannot reach CCM, and nothing here is placed in .ccmram). */
#include <string.h>
#include "stm32f4xx_hal.h"
#include "pge/hal/audio.h"

#ifndef PGE_F429_AUDIO_BUF_FRAMES
#define PGE_F429_AUDIO_BUF_FRAMES 512   /* stereo frames per DMA half; fill() is called with this many */
#endif
#define HALF_SAMPLES (PGE_F429_AUDIO_BUF_FRAMES * 2)
_Static_assert(2 * HALF_SAMPLES <= 65535, "HAL_SAI_Transmit_DMA size is uint16_t halfwords");

volatile uint32_t pge_f429_audio_irq_count;

static int16_t s_buf[2 * HALF_SAMPLES] __attribute__((aligned(4)));
static SAI_HandleTypeDef s_hsai;
static DMA_HandleTypeDef s_dma;
static pge_audio_fill_fn s_fill;
static int s_inited, s_running;

static const struct { uint32_t rate; uint8_t family; } kRates[] = {
  {8000, 1}, {11025, 0}, {16000, 1}, {22050, 0}, {32000, 1}, {44100, 0}, {48000, 1},
};
static const struct { uint16_t n; uint8_t q, divq; uint32_t hz; } kPll[2] = {
  {429, 2, 19, 11289473u},
  {344, 7, 1, 49142857u},
};

void HAL_SAI_MspInit(SAI_HandleTypeDef* hsai) {
  GPIO_InitTypeDef g = {0};
  __HAL_RCC_SAI1_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_DMA2_CLK_ENABLE();
  g.Pin = GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6;
  g.Mode = GPIO_MODE_AF_PP;
  g.Pull = GPIO_NOPULL;
  g.Speed = GPIO_SPEED_FREQ_HIGH;
  g.Alternate = GPIO_AF6_SAI1;
  HAL_GPIO_Init(GPIOE, &g);

  s_dma.Instance = DMA2_Stream1;
  s_dma.Init.Channel = DMA_CHANNEL_0;
  s_dma.Init.Direction = DMA_MEMORY_TO_PERIPH;
  s_dma.Init.PeriphInc = DMA_PINC_DISABLE;
  s_dma.Init.MemInc = DMA_MINC_ENABLE;
  s_dma.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
  s_dma.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
  s_dma.Init.Mode = DMA_CIRCULAR;
  s_dma.Init.Priority = DMA_PRIORITY_HIGH;
  s_dma.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
  if (HAL_DMA_Init(&s_dma) != HAL_OK) return;
  __HAL_LINKDMA(hsai, hdmatx, s_dma);
  HAL_NVIC_SetPriority(DMA2_Stream1_IRQn, 5, 0);   /* SysTick is at 15 (TICK_INT_PRIORITY): fill() delays the tick */
  HAL_NVIC_EnableIRQ(DMA2_Stream1_IRQn);
}

void DMA2_Stream1_IRQHandler(void) { HAL_DMA_IRQHandler(s_hsai.hdmatx); }

void HAL_SAI_TxHalfCpltCallback(SAI_HandleTypeDef* hsai) {
  (void)hsai;
  pge_f429_audio_irq_count++;
  s_fill(s_buf, PGE_F429_AUDIO_BUF_FRAMES);
}

void HAL_SAI_TxCpltCallback(SAI_HandleTypeDef* hsai) {
  (void)hsai;
  pge_f429_audio_irq_count++;
  s_fill(s_buf + HALF_SAMPLES, PGE_F429_AUDIO_BUF_FRAMES);
}

int pge_hal_audio_init(uint32_t rate, pge_audio_fill_fn fill) {
  unsigned i = 0;
  while (i < sizeof kRates / sizeof kRates[0] && kRates[i].rate != rate) i++;
  if (i == sizeof kRates / sizeof kRates[0] || !fill) return -1;
  const unsigned fam = kRates[i].family;

  if (s_inited) {
    if (s_running) HAL_SAI_DMAStop(&s_hsai);
    s_running = 0;
    HAL_SAI_DeInit(&s_hsai);
  }
  s_inited = 0;
  s_fill = fill;

  RCC_PeriphCLKInitTypeDef clk = {0};
  clk.PeriphClockSelection = RCC_PERIPHCLK_SAI_PLLI2S;
  clk.PLLI2S.PLLI2SN = kPll[fam].n;
  clk.PLLI2S.PLLI2SQ = kPll[fam].q;
  clk.PLLI2SDivQ = kPll[fam].divq;
  if (HAL_RCCEx_PeriphCLKConfig(&clk) != HAL_OK) return -4;

  memset(&s_hsai, 0, sizeof s_hsai);
  s_hsai.Instance = SAI1_Block_A;
  s_hsai.Init.AudioMode = SAI_MODEMASTER_TX;
  s_hsai.Init.Synchro = SAI_ASYNCHRONOUS;
  s_hsai.Init.SynchroExt = SAI_SYNCEXT_DISABLE;
  s_hsai.Init.OutputDrive = SAI_OUTPUTDRIVE_ENABLE;
  s_hsai.Init.NoDivider = SAI_MASTERDIVIDER_ENABLE;
  s_hsai.Init.FIFOThreshold = SAI_FIFOTHRESHOLD_1QF;
  s_hsai.Init.ClockSource = SAI_CLKSOURCE_PLLI2S;   /* SAI_GetInputClock writes DCKCFGR.SAI1ASRC from this */
  s_hsai.Init.AudioFrequency = rate;
  s_hsai.Init.MonoStereoMode = SAI_STEREOMODE;
  s_hsai.Init.CompandingMode = SAI_NOCOMPANDING;
  s_hsai.Init.TriState = SAI_OUTPUT_NOTRELEASED;
  if (HAL_SAI_InitProtocol(&s_hsai, SAI_I2S_STANDARD, SAI_PROTOCOL_DATASIZE_16BIT, 2) != HAL_OK) return -4;

  /* Guard the HAL's own divider rounding: fs = kernel / (max(1, 2 * MCKDIV) * 256), within 0.5 %. */
  const uint32_t div = s_hsai.Init.Mckdiv ? 2u * s_hsai.Init.Mckdiv : 1u;
  const int32_t err = (int32_t)(kPll[fam].hz / (div * 256u)) - (int32_t)rate;
  if ((err < 0 ? -err : err) * 200 > (int32_t)rate) return -4;
  s_inited = 1;
  return 0;
}

int pge_hal_audio_start(void) {
  if (!s_inited) return -1;
  if (s_running) return 0;
  memset(s_buf, 0, sizeof s_buf);
  s_fill(s_buf, PGE_F429_AUDIO_BUF_FRAMES);
  s_fill(s_buf + HALF_SAMPLES, PGE_F429_AUDIO_BUF_FRAMES);
  if (HAL_SAI_Transmit_DMA(&s_hsai, (uint8_t*)s_buf, (uint16_t)(2 * HALF_SAMPLES)) != HAL_OK) return -4;
  s_running = 1;
  return 0;
}

int pge_hal_audio_stop(void) {
  if (!s_inited) return -1;
  if (s_running) HAL_SAI_DMAStop(&s_hsai);
  s_running = 0;
  return 0;
}
