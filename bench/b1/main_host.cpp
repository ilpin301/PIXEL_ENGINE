// pge_b1_host [--frames N] [--bus-hz H] [--bus-bits B] [--bands a,b,c]   (headless, no window)
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "b1.h"
#include "host_bus.h"
#include "pge/hal/display.h"

#ifndef PGE_BUILD_TYPE
#define PGE_BUILD_TYPE "unknown"
#endif

static pge::Color g_band[320 * 240];

static void printLine(const char* line, void*) { std::fputs(line, stdout); std::fputc('\n', stdout); }

int main(int argc, char** argv) {
  uint32_t frames = 100, bus_hz = 0, bus_bits = 0;
  uint16_t bands[8] = {8, 16, 32, 240};
  uint8_t nbands = 4;
  for (int i = 1; i + 1 < argc; i += 2) {
    if (!std::strcmp(argv[i], "--frames")) frames = (uint32_t)std::strtoul(argv[i + 1], nullptr, 10);
    else if (!std::strcmp(argv[i], "--bus-hz")) bus_hz = (uint32_t)std::strtoul(argv[i + 1], nullptr, 10);
    else if (!std::strcmp(argv[i], "--bus-bits")) bus_bits = (uint32_t)std::strtoul(argv[i + 1], nullptr, 10);
    else if (!std::strcmp(argv[i], "--bands")) {
      nbands = 0;
      for (const char* p = argv[i + 1]; *p && nbands < 8;) {
        char* end;
        bands[nbands++] = (uint16_t)std::strtoul(p, &end, 10);
        if (end == p) { std::printf("bad --bands value\n"); return 1; }
        p = *end == ',' ? end + 1 : end;
      }
    }
  }
  pge_host_bus_set(bus_hz, bus_bits);
  pge_display_info info;
  if (pge_hal_display_init(320, 240, &info) != 0) { std::printf("display init failed\n"); return 1; }

  std::printf("# B1 benchmark set, platform=host (pge_platform_host_null, no window)\n");
  std::printf("# TAG: host, not representative (desktop CPU, not an MCU)\n");
  std::printf("# build_type=%s\n", PGE_BUILD_TYPE);
  if (bus_hz) std::printf("# logical=320x240 RGB565 frames=%u bus_sim=clock_hz=%u bits_per_clock=%u\n",
                          (unsigned)frames, (unsigned)bus_hz, (unsigned)bus_bits);
  else std::printf("# logical=320x240 RGB565 frames=%u bus_sim=off\n", (unsigned)frames);

  B1Config cfg = {320, 240, bands, nbands, frames, g_band, 320 * 240, printLine, nullptr};
  int err = b1_run(cfg);
  if (err != 0) { std::printf("b1_run failed: %d\n", err); return 1; }
  return 0;
}
