#include <cstdio>
#include <cstring>
#include "b1.h"
#include "host_bus.h"
#include "pge/hal/display.h"
#include "pge/hal/time.h"

#define PGE_CHECK(cond) \
  do { if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); return 1; } } while (0)

enum { W = 320, H = 240 };

static pge::Color g_band[W * H];
static char g_out[16384];

static void collect(const char* line, void*) {
  std::strncat(g_out, line, sizeof g_out - std::strlen(g_out) - 2);
  std::strncat(g_out, "\n", sizeof g_out - std::strlen(g_out) - 1);
}

static int testB1() {
  pge_display_info info;
  PGE_CHECK(pge_hal_display_init(W, H, &info) == 0);
  const uint16_t bands[] = {16, 240};
  B1Config cfg = {W, H, bands, 2, 2, g_band, W * H, collect, nullptr};
  g_out[0] = 0;
  PGE_CHECK(b1_run(cfg) == 0);
  const char* names[] = {"clear", "flush_only", "sprites_opaque", "sprites_transparent",
                         "tiles8_static", "tiles16_static", "tiles8_scroll", "tiles16_scroll"};
  for (const char* n : names) {
    char key[64];
    std::snprintf(key, sizeof key, "band=16 item=%s ", n);
    PGE_CHECK(std::strstr(g_out, key) != nullptr);
  }
  PGE_CHECK(std::strstr(g_out, "ram: assets_bytes") != nullptr);

  cfg.band_buf_pixels = W * 16;
  g_out[0] = 0;
  PGE_CHECK(b1_run(cfg) == 0);
  PGE_CHECK(std::strstr(g_out, "band=240 skipped") != nullptr);
  PGE_CHECK(std::strstr(g_out, "band=16 item=clear ") != nullptr);
  return 0;
}

static int testBus() {
  pge_host_bus_set(1000000, 1);
  uint32_t t0 = pge_hal_time_us();
  pge_host_bus_charge(1000);  // 8000 us
  uint32_t dt = pge_hal_time_us() - t0;
  PGE_CHECK(dt >= 7900 && dt < 100000);
  pge_host_bus_set(0, 0);
  t0 = pge_hal_time_us();
  pge_host_bus_charge(1000000);
  PGE_CHECK(pge_hal_time_us() - t0 < 5000);
  return 0;
}

static int testTime() {
  uint32_t a = pge_hal_time_us();
  uint32_t b = pge_hal_time_us();
  PGE_CHECK((uint32_t)(b - a) < 0x80000000u);
  return 0;
}

int main() {
  if (testB1() || testBus() || testTime()) return 1;
  std::printf("test_bench OK\n");
  return 0;
}
