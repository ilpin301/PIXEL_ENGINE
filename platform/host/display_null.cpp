// Headless host display HAL: validates rects, charges the simulated bus, draws nothing.
#include "host_bus.h"
#include "pge/hal/display.h"

static uint16_t g_w, g_h;

extern "C" {

pge_error pge_hal_display_init(uint16_t want_w, uint16_t want_h, pge_display_info* out) {
  if (want_w == 0 || want_h == 0) return -1;
  g_w = want_w;
  g_h = want_h;
  if (out) { out->panel_w = want_w; out->panel_h = want_h; out->flags = 0; }
  return 0;
}

pge_error pge_hal_display_flush(pge_rect r, uint16_t* px) {
  if (r.w == 0 || r.h == 0 || !px || r.x + r.w > g_w || r.y + r.h > g_h) return -1;
  pge_host_bus_charge((uint32_t)r.w * r.h * 2);
  return 0;
}

pge_error pge_hal_display_wait(void) { return 0; }
void pge_hal_display_frame_begin(void) {}
void pge_hal_display_frame_end(void) {}

int pge_host_pump(void) { return 0; }

}  // extern "C"
