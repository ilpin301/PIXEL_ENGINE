// pge_f429_platformer / platbench / platbench_nobb: the platformer demo (examples/platformer/game.h) on the F429 board.
// PGE_PLATFORMER_BENCH=1: scripted bot for 1860 ticks (60 warm-up + 1800 samples), summary to the debug log, then idle.
// PGE_PLAT_BACKBUF=1: vsync back buffer. LED3 on init/run failure, LED4 after a finished bench.
#include "pge/engine.h"
#include "board.h"
#include "game.h"

#ifndef PGE_PLATFORMER_BENCH
#define PGE_PLATFORMER_BENCH 0
#endif
#ifndef PGE_PLAT_BACKBUF
#define PGE_PLAT_BACKBUF 1
#endif

static PlatformerGame g_game;

int main() {
  pge_board_init();
  pge_f429_set_opts(0, PGE_PLAT_BACKBUF);
  pge::Error err = pge::init();
  if (err == pge::Ok) {
#if PGE_PLATFORMER_BENCH
    g_game.autoplay = true;
    g_game.quit_after = 1860;
#endif
    err = pge::run(g_game);
  }
  if (err != pge::Ok) {
    pge_dbg_printf("# pge_f429_platformer init/run FAILED rc=%d\n", (int)err);
    pge_board_led(3, 1);
    for (;;) {}
  }
#if PGE_PLATFORMER_BENCH
  const PlatformerGame::Bench& b = g_game.bench();
  const pge::Stats& s = pge::engine().stats();
  const unsigned long n = b.n ? b.n : 1;
  pge_dbg_printf("# PLAT BENCH backbuf=%d samples=%lu\n", PGE_PLAT_BACKBUF, (unsigned long)b.n);
  pge_dbg_printf("avg_us tick=%lu band=%lu flush=%lu frame=%lu\n", b.sum_tick_us / n, b.sum_band_us / n,
                 b.sum_flush_us / n, b.sum_frame_us / n);
  pge_dbg_printf("max_us tick=%lu band=%lu frame=%lu\n", (unsigned long)b.max_tick_us, (unsigned long)b.max_band_us,
                 (unsigned long)b.max_frame_us);
  pge_dbg_printf("ticks_dropped=%lu tick_overruns=%lu draw_dropped=%u\n", (unsigned long)s.ticks_dropped,
                 (unsigned long)s.tick_overruns, (unsigned)s.draw_dropped);
  pge_dbg_puts("# PLAT DONE\n");
  pge_board_led(4, 1);
#endif
  for (;;) {}
}
