// pge_f429_b1: benchmark set B1 on the STM32F429I-DISCO in four display variants, plus flush/fill micro benchmarks.
// Everything goes to the debug ring buffer (see board.h / docs/dev/toolchain.md); "# B1 DONE" marks the end.
#include "b1.h"
#include "pge/hal/display.h"
#include "pge/hal/time.h"
#include "board.h"

namespace {

enum { kFrames = 30, kMicroReps = 100 };

pge::Color g_band_sram[320 * 32];
pge::Color* const g_band_sdram = reinterpret_cast<pge::Color*>(0xD0200000u);   // 153,600 B, clear of FB0/FB1

struct Variant {
  const char* name;
  const char* orientation;
  const char* flush;
  bool dma2d, vsync, portrait;
};
const Variant kVariants[4] = {
    {"A", "landscape-transposed", "cpu-transpose", false, false, false},
    {"B", "portrait-native", "cpu-memcpy", false, false, true},
    {"C", "portrait-native", "dma2d", true, false, true},
    {"D", "landscape-transposed", "cpu-transpose", false, true, false},
};

void printLine(const char* line, void*) {
  pge_dbg_puts(line);
  pge_dbg_puts("\n");
}

[[noreturn]] void fail(int rc) {
  pge_dbg_printf("FAILED rc=%d\n", rc);
  pge_board_led(3, 1);
  for (;;) {}
}

void microFlush(const char* mode, uint16_t w) {
  static const uint16_t kHeights[3] = {8, 16, 32};
  for (uint16_t h : kHeights) {
    pge_rect r = {0, 0, w, h};
    uint32_t t0 = pge_hal_time_us();
    for (int i = 0; i < kMicroReps; i++)
      if (pge_hal_display_flush(r, g_band_sram) != 0) fail(-20);
    uint32_t total = pge_hal_time_us() - t0;   // total us for 100 reps = average in 0.01 us
    pge_dbg_printf("micro flush mode=%s band=%ux%u reps=%d avg_us=%lu.%02lu\n", mode, (unsigned)w, (unsigned)h,
                   kMicroReps, (unsigned long)(total / kMicroReps), (unsigned long)(total % kMicroReps));
  }
}

void microFill(bool dma2d) {
  uint32_t t0 = pge_hal_time_us();
  for (int i = 0; i < kMicroReps; i++) pge_f429_bench_fill(dma2d, (uint16_t)(0x1234u + i));
  uint32_t total = pge_hal_time_us() - t0;
  pge_dbg_printf("micro fill mode=%s size=240x320 reps=%d avg_us=%lu.%02lu\n", dma2d ? "dma2d" : "cpu", kMicroReps,
                 (unsigned long)(total / kMicroReps), (unsigned long)(total % kMicroReps));
}

void runVariant(const Variant& v) {
  const uint16_t w = v.portrait ? 240 : 320, h = v.portrait ? 320 : 240;
  pge_f429_set_opts(v.dma2d, v.vsync);
  pge_display_info info;
  if (pge_hal_display_init(w, h, &info) != 0) fail(-10);
  pge_dbg_printf("# variant=%s orientation=%s flush=%s vsync=%s logical=%ux%u\n", v.name, v.orientation, v.flush,
                 v.vsync ? "on" : "off", (unsigned)w, (unsigned)h);

  static const uint16_t kBands[3] = {8, 16, 32};
  static const uint16_t kBand16[1] = {16};
  const uint16_t full[1] = {h};
  pge_dbg_puts("# bandbuf=sram\n");
  B1Config cfg = {w, h, v.vsync ? kBand16 : kBands, (uint8_t)(v.vsync ? 1 : 3), kFrames, g_band_sram,
                  320 * 32, printLine, nullptr};
  int rc = b1_run(cfg);
  if (rc != 0) fail(rc);
  if (v.vsync) return;

  pge_dbg_puts("# bandbuf=sdram\n");
  cfg.bands = full;
  cfg.nbands = 1;
  cfg.band_buf = g_band_sdram;
  cfg.band_buf_pixels = (uint32_t)w * h;
  rc = b1_run(cfg);
  if (rc != 0) fail(rc);

  microFlush(v.flush, w);
  if (v.dma2d) {
    microFill(false);
    microFill(true);
  }
}

}  // namespace

int main() {
  pge_board_init();
  pge_dbg_printf("# B1 f429 sysclk=%lu build=Release -O2 cube=HAL frames=%d warmup=1 rgb565 ltdc\n",
                 (unsigned long)pge_board_sysclk_hz(), kFrames);
  for (const Variant& v : kVariants) runVariant(v);
  pge_dbg_puts("# B1 DONE\n");
  pge_board_led(4, 1);
  for (;;) {}
}
