/* hal/display.h on the LTDC: 240x320 RGB565 framebuffer in SDRAM, logical 320x240 landscape via CPU transpose. */
#include <string.h>
#include "board.h"
#include "pge/hal/display.h"
#include "stm32f429i_discovery_lcd.h"

#ifndef PGE_F429_ROT
#define PGE_F429_ROT 0  /* 1: panelX = 239 - y, panelY = x.  0: panelX = y, panelY = 319 - x */
#endif

#define FB_ADDR 0xD0000000u
#define LOG_W 320u
#define LOG_H 240u
#define PAN_W 240u
#define PAN_H 320u

extern LTDC_HandleTypeDef LtdcHandler;   /* defined in the BSP lcd driver */

static uint16_t* const s_fb = (uint16_t*)FB_ADDR;

pge_error pge_hal_display_init(uint16_t want_w, uint16_t want_h, pge_display_info* out) {
  if (want_w != LOG_W || want_h != LOG_H || !out) return -1;
  if (BSP_LCD_Init() != LCD_OK) return -1;   /* also inits SDRAM */
  memset(s_fb, 0, PAN_W * PAN_H * 2u);

  LTDC_LayerCfgTypeDef cfg = {0};
  cfg.WindowX0 = 0;
  cfg.WindowX1 = PAN_W;
  cfg.WindowY0 = 0;
  cfg.WindowY1 = PAN_H;
  cfg.PixelFormat = LTDC_PIXEL_FORMAT_RGB565;
  cfg.FBStartAdress = FB_ADDR;
  cfg.Alpha = 255;
  cfg.Alpha0 = 0;
  cfg.BlendingFactor1 = LTDC_BLENDING_FACTOR1_PAxCA;   /* same as the BSP layer init */
  cfg.BlendingFactor2 = LTDC_BLENDING_FACTOR2_PAxCA;
  cfg.ImageWidth = PAN_W;
  cfg.ImageHeight = PAN_H;
  if (HAL_LTDC_ConfigLayer(&LtdcHandler, &cfg, 0) != HAL_OK) return -1;   /* enables the layer, immediate reload */

  out->panel_w = PAN_W;
  out->panel_h = PAN_H;
  out->flags = PGE_DISP_TRANSPOSED;   /* no ASYNC, no VSYNC: tearing accepted in S3.2a */
  return 0;
}

pge_error pge_hal_display_flush(pge_rect r, uint16_t* px) {
  if (!px || !r.w || !r.h || (uint32_t)r.x + r.w > LOG_W || (uint32_t)r.y + r.h > LOG_H) return -1;
  /* Outer loop over logical x = panel row, so the SDRAM writes are sequential. */
  for (uint32_t i = 0; i < r.w; i++) {
    uint32_t x = r.x + i;
#if PGE_F429_ROT
    uint16_t* dst = s_fb + x * PAN_W + (PAN_W - 1u - r.y);
    for (uint32_t j = 0; j < r.h; j++) *dst-- = px[j * r.w + i];
#else
    uint16_t* dst = s_fb + (PAN_H - 1u - x) * PAN_W + r.y;
    for (uint32_t j = 0; j < r.h; j++) *dst++ = px[j * r.w + i];
#endif
  }
  return 0;
}

pge_error pge_hal_display_wait(void) { return 0; }
void pge_hal_display_frame_begin(void) {}
void pge_hal_display_frame_end(void) {}
