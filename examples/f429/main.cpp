// pge_f429_scene: the S3.1a tile+sprite scene on the STM32F429I-DISCO LCD (S3.2a). Log: see board.h.
#include "pge/gfx.h"
#include "pge/hal/display.h"
#include "pge/hal/time.h"
#include "board.h"
#include "scene.h"

#ifndef PGE_F429_ROT
#define PGE_F429_ROT 0
#endif

static pge::Color g_band[320 * 16];

static void render(pge::Canvas& c, void* ctx) { scene::renderScene(c, (int)*static_cast<uint32_t*>(ctx)); }

int main() {
  pge_board_init();
  pge_dbg_printf("# pge_f429_scene sysclk=%lu orientation=landscape-transposed ROT=%d fb=0xD0000000 RGB565 ltdc\n",
                 (unsigned long)pge_board_sysclk_hz(), PGE_F429_ROT);

  pge_display_info info;
  if (pge_hal_display_init(320, 240, &info) != 0) {
    pge_dbg_puts("display init FAILED\n");
    pge_board_led(3, 1);
    for (;;) {}
  }
  pge_dbg_printf("display ok panel=%ux%u flags=0x%lx (no vsync: tearing accepted)\n", (unsigned)info.panel_w,
                 (unsigned)info.panel_h, (unsigned long)info.flags);
  if (pge_sdram_selftest() != 0) pge_board_led(3, 1);
  pge_board_led(4, 1);

  uint32_t t0 = pge_hal_time_us();
  pge_board_delay_ms(100);
  pge_dbg_printf("time check: HAL_Delay(100) = %lu us\n", (unsigned long)(pge_hal_time_us() - t0));

  const pge::RenderConfig cfg = {320, 240, 16, 0};
  uint32_t frame = 0, sec_frames = 0, sec_start = pge_hal_time_us();
  bool err_logged = false;
  for (;; frame++) {
    int err = pge::renderFrame(cfg, g_band, 320 * 16, frame, render, &frame);
    if (err != 0) {
      pge_board_led(3, 1);
      if (!err_logged) pge_dbg_printf("renderFrame failed: %d (frame %lu)\n", err, (unsigned long)frame);
      err_logged = true;
    }
    uint32_t now = pge_hal_time_us();
    pge_dbg_now_us = now;
    sec_frames++;
    if (now - sec_start >= 1000000u) {
      pge_dbg_printf("fps=%lu\n", (unsigned long)sec_frames);
      sec_frames = 0;
      sec_start += 1000000u;
    }
  }
}
