// pge_f429_sandbox / pge_f429_sandbench: the sandbox example (examples/sandbox/game.h) on the F429 board.
// SANDBOX_BENCH=1: scripted bot for 1860 ticks (60 warm-up + 1800 samples), then a summary in the debug log: timing,
// RAM figures and chunk generation time. SANDBOX_BACKBUF=1: vsync back buffer. LED3 on init/run failure, LED4 after
// a finished bench. SANDBOX_USB=1 (pge_f429_sandbox): SAVE / LOAD go to sandbox.sav on a FAT32 USB stick in the USB USER
// port (micro-USB OTG adapter), CPU at 168 MHz for the 48 MHz USB clock; the stick must be plugged in at power-up (mount
// runs once at game start, waits up to 1.5 s for a device and 6 s for enumeration), else the game shows "NO STORAGE".
// SANDBOX_USB=0 (pge_f429_sandbench): storage stubs, 180 MHz.
#include <stdint.h>
#include "pge/engine.h"
#include "pge/hal/storage.h"
#include "board.h"
#include "game.h"
#if SANDBOX_USB
#include "ff.h"
#include "pge_storage_fatfs.h"
#endif

#ifndef SANDBOX_BENCH
#define SANDBOX_BENCH 0
#endif
#ifndef SANDBOX_USB
#define SANDBOX_USB 0
#endif
#ifndef SANDBOX_BACKBUF
#define SANDBOX_BACKBUF 1
#endif
#ifndef SANDBOX_CACHE
#define SANDBOX_CACHE 12  // chunk cache slots, 2 KiB each
#endif
#ifndef SANDBOX_CHANGE_CAP
#define SANDBOX_CHANGE_CAP 2048  // change log entries, 5 B each
#endif

extern "C" {
#if !SANDBOX_USB
int pge_hal_storage_mount(void) { return -4; }
int pge_hal_storage_open(const char*, int, pge_file*) { return -4; }
int32_t pge_hal_storage_read(pge_file, void*, uint32_t) { return -4; }
int32_t pge_hal_storage_write(pge_file, const void*, uint32_t) { return -4; }
int pge_hal_storage_close(pge_file) { return -4; }
int pge_hal_storage_rename(const char*, const char*) { return -4; }
int pge_hal_storage_remove(const char*) { return -4; }
#endif
extern uint32_t _sdata[], _edata[], _sbss[], _ebss[], _end[], _estack[];  // linker script
}

static SandboxGame g_game;
#if SANDBOX_USB
static FATFS g_fs;
#endif

#if SANDBOX_BENCH
// Time ensureLoaded() on every chunk that is not resident after the run: one 4x4 px rect inside the chunk, so exactly
// one generation (plus an LRU eviction, plus the logged edits of that chunk) per call. Row = chunk row.
static void chunkGenBench(pge::World& w) {
  uint32_t n[8] = {}, sum[8] = {}, mx[8] = {};
  const int px = pge::kChunkSize * w.tilePx();
  for (int cy = 0; cy < w.chunksH() && cy < 8; cy++)
    for (int cx = 0; cx < w.chunksW(); cx++) {
      if (w.isLoaded(cx, cy)) continue;
      const uint32_t t0 = pge_hal_time_us();
      w.ensureLoaded(pge::Rect{cx * px + 1, cy * px + 1, 4, 4});
      const uint32_t dt = pge_hal_time_us() - t0;
      if (!w.isLoaded(cx, cy)) continue;
      n[cy]++;
      sum[cy] += dt;
      if (dt > mx[cy]) mx[cy] = dt;
    }
  uint32_t tn = 0, ts = 0, tm = 0;
  for (int r = 0; r < 8; r++) {
    tn += n[r];
    ts += sum[r];
    if (mx[r] > tm) tm = mx[r];
  }
  pge_dbg_printf("chunkgen n=%lu avg_us=%lu max_us=%lu PGE_MAX_CHUNK_LOADS_PER_TICK=%d\n", (unsigned long)tn,
                 (unsigned long)(ts / (tn ? tn : 1)), (unsigned long)tm, PGE_MAX_CHUNK_LOADS_PER_TICK);
  for (int r = 0; r < w.chunksH() && r < 8; r++)
    pge_dbg_printf("chunkgen row=%d n=%lu avg_us=%lu max_us=%lu\n", r, (unsigned long)n[r],
                   (unsigned long)(sum[r] / (n[r] ? n[r] : 1)), (unsigned long)mx[r]);
}
#endif

int main() {
  // Paint the unused RAM between _end and the stack pointer, so the stack high-water can be read at the end.
  uint32_t sp;
  __asm volatile("mov %0, sp" : "=r"(sp));
  for (uint32_t* p = _end; p < reinterpret_cast<uint32_t*>(sp - 512); p++) *p = 0xA5A5A5A5u;
#if SANDBOX_USB
  pge_board_init_ex(1);
  pge_fatfs_storage_bind(&g_fs, "");   // only stores the pointer; mount() runs in onCreate
#else
  pge_board_init();
#endif
  pge_f429_set_opts(0, SANDBOX_BACKBUF);
  pge::Error err = pge::init();
  if (err == pge::Ok) {
    g_game.chunk_cache = SANDBOX_CACHE;
    g_game.change_cap = SANDBOX_CHANGE_CAP;
#if SANDBOX_BENCH
    g_game.autoplay = true;
    g_game.quit_after = 1860;
#endif
    err = pge::run(g_game);
  }
  if (err != pge::Ok) {
    const pge::Arena& a = pge::engine().initArena();
    pge_dbg_printf("# pge_f429_sandbox init/run FAILED rc=%d arena hw=%lu cap=%lu failed=%lu\n", (int)err,
                   (unsigned long)a.highWater(), (unsigned long)a.capacity(), (unsigned long)a.failed());
    pge_board_led(3, 1);
    for (;;) {}
  }
#if SANDBOX_BENCH
  const SandboxGame::Bench& b = g_game.bench();
  const pge::Stats& s = pge::engine().stats();
  const unsigned long n = b.n ? b.n : 1;
  pge_dbg_printf("# SANDBOX BENCH backbuf=%d samples=%lu\n", SANDBOX_BACKBUF, (unsigned long)b.n);
  pge_dbg_printf("avg_us tick=%lu band=%lu flush=%lu frame=%lu\n", b.sum_tick_us / n, b.sum_band_us / n,
                 b.sum_flush_us / n, b.sum_frame_us / n);
  pge_dbg_printf("max_us tick=%lu band=%lu frame=%lu\n", (unsigned long)b.max_tick_us, (unsigned long)b.max_band_us,
                 (unsigned long)b.max_frame_us);
  pge_dbg_printf("ticks_dropped=%lu tick_overruns=%lu draw_dropped=%u\n", (unsigned long)s.ticks_dropped,
                 (unsigned long)s.tick_overruns, (unsigned)s.draw_dropped);
  // RAM (all measured at run time or read from linker symbols)
  uint32_t* q = _end;
  while (q < _estack && *q == 0xA5A5A5A5u) q++;
  pge_dbg_printf("ram data=%lu bss=%lu end=0x%08lx estack=0x%08lx stack_hw=%lu of %lu\n",
                 (unsigned long)((_edata - _sdata) * 4), (unsigned long)((_ebss - _sbss) * 4), (unsigned long)_end,
                 (unsigned long)_estack, (unsigned long)((_estack - q) * 4), (unsigned long)((_estack - _end) * 4));
  const pge::Arena& ia = pge::engine().initArena();
  const pge::Arena& fa = pge::engine().frameArena();
  pge_dbg_printf("arena init hw=%lu cap=%lu failed=%lu band=%lu | frame hw=%lu cap=%lu\n", (unsigned long)ia.highWater(),
                 (unsigned long)ia.capacity(), (unsigned long)ia.failed(),
                 (unsigned long)(PGE_LOGICAL_W * PGE_BAND_LINES * sizeof(pge::Color)), (unsigned long)fa.highWater(),
                 (unsigned long)fa.capacity());
  pge_dbg_printf("world cache_slots=%d loaded=%u chunks=%ux%u change=%lu/%lu\n", SANDBOX_CACHE,
                 (unsigned)g_game.world().chunksLoaded(), (unsigned)g_game.world().chunksW(),
                 (unsigned)g_game.world().chunksH(), (unsigned long)g_game.world().changeCount(),
                 (unsigned long)g_game.world().changeCapacity());
  pge_dbg_printf("pools entity hi=%u full=%u cap=%u | pool_high[0..3]=%u %u %u %u\n",
                 (unsigned)g_game.entities().counters().high, (unsigned)g_game.entities().counters().full,
                 (unsigned)g_game.entities().capacity(), (unsigned)s.pool_high[0], (unsigned)s.pool_high[1],
                 (unsigned)s.pool_high[2], (unsigned)s.pool_high[3]);
  chunkGenBench(g_game.world());
  pge_dbg_puts("# SANDBOX DONE\n");
  pge_board_led(4, 1);
#endif
  for (;;) {}
}
