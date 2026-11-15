// Headless input HAL: no buttons, never quits. Lives in pge_platform_host_null only; pge_platform_host has input_sdl.cpp.
#include "pge/hal/input.h"

extern "C" {

int pge_hal_input_init(void) { return 0; }

int pge_hal_input_poll(pge_raw_input* out) {
  if (!out) return -1;
  *out = pge_raw_input();
  return 0;
}

}  // extern "C"
