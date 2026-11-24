// pge_f429_planet*: the planet example (examples/planet/game.h) on the F429 board; one source, compile-time switches.
// PLANET_BENCH=1: scripted bot for 1860 ticks (60 warm-up + one lap of the planet, about 1740 ticks), then a summary
// in the debug log (timing, lap check, RAM figures, chunk generation time, dig hitch of the change log), ending with
// "# PLANET DONE". PLANET_USB=1: SAVE (Start) / LOAD go to planet.sav on a FAT32 USB stick in the USB USER port and the
// game loads the save at start, so it survives a power cycle; CPU at 168 MHz for the 48 MHz USB clock; the stick must
// be plugged in at power-up (as pge_f429_sandbox), else the game shows "NO STORAGE". PLANET_USB=0: storage stubs,
// 180 MHz. PLANET_ROTATE=1: the world turns smoothly with the planet (game flag rotate). PLANET_BACKBUF=1: vsync
// back buffer. PLANET_CORE=N: radius in tiles of the unbreakable core at the planet centre (default
// PlanetGame::kCoreDefault, 0 = none), to try sizes on the board (cmake -DPLANET_CORE=N). The planet-sized change log
// lives in SDRAM (PGE_RAM_EXT), as in pge_f429_planetmem. LED3 + a "FAILED rc=" line on init/run failure, LED4 after a finished bench.
#include <stdint.h>
#include <string.h>
#include "pge/engine.h"
#include "pge/hal/storage.h"
#include "pge/worldgen.h"
#include "board.h"
#include "game.h"
#if PLANET_USB
#include "ff.h"
#include "pge_storage_fatfs.h"
#endif

#ifndef PLANET_BENCH
#define PLANET_BENCH 0
#endif
#ifndef PLANET_USB
#define PLANET_USB 0
#endif
#ifndef PLANET_ROTATE
#define PLANET_ROTATE 0
#endif
#ifndef PLANET_BACKBUF
#define PLANET_BACKBUF 1
#endif
#ifndef PLANET_CORE
#define PLANET_CORE PlanetGame::kCoreDefault
#endif

extern "C" {
#if !PLANET_USB
int pge_hal_storage_mount(void) { return -4; }
int pge_hal_storage_open(const char*, int, pge_file*) { return -4; }
int32_t pge_hal_storage_read(pge_file, void*, uint32_t) { return -4; }
int32_t pge_hal_storage_write(pge_file, const void*, uint32_t) { return -4; }
int pge_hal_storage_close(pge_file) { return -4; }
int pge_hal_storage_rename(const char*, const char*) { return -4; }
int pge_hal_storage_remove(const char*) { return -4; }
#endif
extern uint32_t _sdata[], _edata[], _sbss[], _ebss[], _end[], _estack[], _ssdram[], _esdram[];  // linker script
}

namespace {
PlanetGame g_game;
alignas(8) PGE_RAM_EXT uint8_t g_log_buf[PlanetGame::kChangeCap * 5 + 64];  // key u32 + tile u8 per entry, plus slack
pge::Arena g_log;
#if PLANET_USB
FATFS g_fs;
#endif
}  // namespace

#if PLANET_BENCH
namespace {
constexpr int kChunks = PlanetGame::kSpec.chunks;

// Generates each chunk of the planet once into scratch buffers: the generator alone, without the world cache around it.
void chunkGenBench() {
  static uint8_t fg[pge::kChunkTiles], wall[pge::kChunkTiles];
  uint32_t sum = 0, mx = 0;
  for (int i = 0; i < kChunks * kChunks; i++) {
    const uint32_t t0 = pge_hal_time_us();
    pge::roundGen(PlanetGame::spec(), fg, wall, i % kChunks, i / kChunks, g_game.seed);
    const uint32_t dt = pge_hal_time_us() - t0;
    sum += dt;
    if (dt > mx) mx = dt;
  }
  pge_dbg_printf("chunkgen n=%d avg_us=%lu max_us=%lu\n", kChunks * kChunks,
                 (unsigned long)(sum / (kChunks * kChunks)), (unsigned long)mx);
}

// The g-th edit of the whole planet in change log key order (chunk, layer, tile): g = 0 is the smallest key, the last g
// the largest.
pge::Error editAt(pge::World& w, uint32_t g) {
  const int chunk = (int)(g >> 11), i = (int)(g & 1023);
  return w.set((chunk % kChunks) * 32 + (i & 31), (chunk / kChunks) * 32 + (i >> 5), (pge::Layer)((g >> 10) & 1),
               pge::kTileAir);
}

// Dig hitch (S8.2 open question): World::set keeps the change log sorted, so an edit that sorts before every logged one
// moves the whole log. Fills the log with n edits in ascending key order (each one an append), then times one edit with
// the smallest key (front insert, n entries logged) and one with the largest key (append, n + 1 entries logged).
void digHitch(pge::World& w, uint32_t n) {
  w.clearChanges();  // frees every chunk: load them again so the edits also write the cache, as a dig does
  w.ensureLoaded(pge::Rect{0, 0, PlanetGame::kWorldPx, PlanetGame::kWorldPx}, kChunks * kChunks);
  bool ok = true;
  for (uint32_t g = 1; g <= n; g++) ok = editAt(w, g) == pge::Ok && ok;
  const uint32_t before = w.changeCount();
  uint32_t t0 = pge_hal_time_us();
  ok = editAt(w, 0) == pge::Ok && ok;
  const uint32_t front_us = pge_hal_time_us() - t0;
  t0 = pge_hal_time_us();
  ok = editAt(w, PlanetGame::kChangeCap - 1) == pge::Ok && ok;
  const uint32_t append_us = pge_hal_time_us() - t0;
  pge_dbg_printf("dighitch entries=%lu front_us=%lu append_us=%lu end=%lu%s\n", (unsigned long)before,
                 (unsigned long)front_us, (unsigned long)append_us, (unsigned long)w.changeCount(),
                 ok && w.changeCount() == n + 2 ? "" : " ERROR");
}
}  // namespace
#endif

int main() {
  // Paint the unused RAM between _end and the stack pointer, so the stack high-water can be read at the end.
  uint32_t sp;
  __asm volatile("mov %0, sp" : "=r"(sp));
  for (uint32_t* p = _end; p < reinterpret_cast<uint32_t*>(sp - 512); p++) *p = 0xA5A5A5A5u;
#if PLANET_USB
  pge_board_init_ex(1);
  pge_fatfs_storage_bind(&g_fs, "");   // only stores the pointer; mount() runs in onCreate
#else
  pge_board_init();
#endif
  pge_f429_set_opts(0, PLANET_BACKBUF);
  pge::Error err = pge::init();  // the display init also brings up the SDRAM
  if (err == pge::Ok) {
    memset(g_log_buf, 0, sizeof g_log_buf);  // NOLOAD section: startup never zeroes it
    g_log.init(g_log_buf, sizeof g_log_buf);
    g_game.log_arena = &g_log;
    g_game.rotate = PLANET_ROTATE;
    g_game.core_tiles = PLANET_CORE;
    g_game.load_on_start = PLANET_USB;
#if PLANET_BENCH
    g_game.autoplay = true;
    g_game.quit_after = 1860;
#endif
    err = pge::run(g_game);
  }
  if (err != pge::Ok) {
    const pge::Arena& a = pge::engine().initArena();
    pge_dbg_printf("# pge_f429_planet init/run FAILED rc=%d arena hw=%lu cap=%lu failed=%lu\n", (int)err,
                   (unsigned long)a.highWater(), (unsigned long)a.capacity(), (unsigned long)a.failed());
    pge_board_led(3, 1);
    for (;;) {}
  }
#if PLANET_BENCH
  const PlanetGame::Bench& b = g_game.bench();
  const pge::Stats& s = pge::engine().stats();
  const unsigned long n = b.n ? b.n : 1, avg_frame = b.sum_frame_us / n;
  const unsigned long fps10 = avg_frame ? 10000000ul / avg_frame : 0;
  pge_dbg_printf("# PLANET BENCH rotate=%d char=%u samples=%lu clock_mhz=%lu backbuf=%d\n", PLANET_ROTATE,
                 (unsigned)g_game.charH(), (unsigned long)b.n, (unsigned long)(pge_board_sysclk_hz() / 1000000u),
                 PLANET_BACKBUF);
  pge_dbg_printf("avg_us tick=%lu band=%lu flush=%lu frame=%lu fps=%lu.%lu\n", b.sum_tick_us / n, b.sum_band_us / n,
                 b.sum_flush_us / n, avg_frame, fps10 / 10, fps10 % 10);
  pge_dbg_printf("max_us tick=%lu band=%lu flush=%lu frame=%lu\n", (unsigned long)b.max_tick_us,
                 (unsigned long)b.max_band_us, (unsigned long)b.max_flush_us, (unsigned long)b.max_frame_us);
  pge_dbg_printf("ticks_dropped=%lu tick_overruns=%lu draw_dropped=%u\n", (unsigned long)s.ticks_dropped,
                 (unsigned long)s.tick_overruns, (unsigned)s.draw_dropped);
  // lap: the bot walks Down -> Left -> Up -> Right -> Down, 4 gravity switches (the host test checks the same)
  pge_dbg_printf("lap ticks=%lu switches=%lu gravity=%c back_at_start=%d\n", (unsigned long)g_game.ticks(),
                 (unsigned long)g_game.switches(), "DLUR"[(int)g_game.gravity() & 3],
                 g_game.switches() == 4 && g_game.gravity() == pge::GravityDir::Down);
  // RAM (all measured at run time or read from linker symbols)
  uint32_t* q = _end;
  while (q < _estack && *q == 0xA5A5A5A5u) q++;
  pge_dbg_printf("ram data=%lu bss=%lu end=0x%08lx estack=0x%08lx stack_hw=%lu of %lu\n",
                 (unsigned long)((_edata - _sdata) * 4), (unsigned long)((_ebss - _sbss) * 4), (unsigned long)_end,
                 (unsigned long)_estack, (unsigned long)((_estack - q) * 4), (unsigned long)((_estack - _end) * 4));
  const pge::Arena& ia = pge::engine().initArena();
  const pge::Arena& fa = pge::engine().frameArena();
  pge_dbg_printf("arena init hw=%lu cap=%lu failed=%lu band=%lu | frame hw=%lu cap=%lu\n",
                 (unsigned long)ia.highWater(), (unsigned long)ia.capacity(), (unsigned long)ia.failed(),
                 (unsigned long)(PGE_LOGICAL_W * PGE_BAND_LINES * sizeof(pge::Color)), (unsigned long)fa.highWater(),
                 (unsigned long)fa.capacity());
  pge_dbg_printf("sdram log used=%lu cap=%lu failed=%lu section=0x%08lx..0x%08lx\n", (unsigned long)g_log.used(),
                 (unsigned long)g_log.capacity(), (unsigned long)g_log.failed(), (unsigned long)_ssdram,
                 (unsigned long)_esdram);
  pge_dbg_printf("world cache_slots=16 loaded=%u chunks=%ux%u change=%lu/%lu\n",
                 (unsigned)g_game.world().chunksLoaded(), (unsigned)g_game.world().chunksW(),
                 (unsigned)g_game.world().chunksH(), (unsigned long)g_game.world().changeCount(),
                 (unsigned long)g_game.world().changeCapacity());
  chunkGenBench();
  digHitch(g_game.world(), 2048);
  digHitch(g_game.world(), 8192);
  pge_dbg_puts("# PLANET DONE\n");
  pge_board_led(4, 1);
#endif
  for (;;) {}
}
