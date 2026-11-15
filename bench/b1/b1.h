#pragma once
// Benchmark set B1: portable (pge_core + display/time HAL only), no heap, no platform includes.
#include <stdint.h>
#include "pge/gfx.h"

typedef void (*B1Print)(const char* line, void* user);

struct B1Config {
  uint16_t w, h;               // logical size, both >= 17 (sprites are 16x16)
  const uint16_t* bands;       // band heights in lines to test
  uint8_t nbands;
  uint32_t frames;             // timed frames per item (> 0); one extra warm-up frame is not timed
  pge::Color* band_buf;        // caller-owned
  uint32_t band_buf_pixels;
  B1Print print;
  void* user;
};

// Returns 0, the first nonzero renderFrame error, or -1 for a bad config.
int b1_run(const B1Config& cfg);
