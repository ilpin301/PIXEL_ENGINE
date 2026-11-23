// pge_f429_planetmem (S8.2): memory check for a whole planet on the F429. A 4x4-chunk world (128 x 128 tiles of 8 px)
// with 16 cache slots in the engine init arena (internal SRAM) and the worst-case change log (128 x 128 x 2 = 32,768
// entries, 163,840 B) in an arena over a PGE_RAM_EXT buffer (external SDRAM, section .sdram). Fills the log, times a
// front insert into it, loads the planet, checks every tile and prints memory counters to the debug log.
// LED4 = pass, LED3 = fail (reason in the log).
#include <stdint.h>
#include <string.h>
#include "pge/engine.h"
#include "pge/world.h"
#include "board.h"

extern "C" {
extern uint32_t _sdata[], _edata[], _sbss[], _ebss[], _end[], _estack[], _ssdram[], _esdram[];  // linker script
}

namespace {
constexpr uint32_t kCells = 128u * 128u * 2u;  // every tile of both layers
alignas(8) PGE_RAM_EXT uint8_t g_ext[kCells * 5u + 64u];  // key u32 + tile u8 per entry, plus alignment slack
pge::TileProps g_props[256];
pge::Arena g_log;
pge::World g_world;

uint8_t want(int tx, int ty, int l) { return (tx == 0 && ty == 0 && l == 0) ? 9 : (uint8_t)(1 + (tx + ty + l) % 7); }

[[noreturn]] void fail(const char* what) {
  pge_dbg_printf("# PLANETMEM FAIL %s\n", what);
  pge_board_led(3, 1);
  for (;;) {}
}
}  // namespace

int main() {
  // Paint the unused RAM between _end and the stack pointer, so the stack high-water can be read at the end.
  uint32_t sp;
  __asm volatile("mov %0, sp" : "=r"(sp));
  for (uint32_t* p = _end; p < reinterpret_cast<uint32_t*>(sp - 512); p++) *p = 0xA5A5A5A5u;
  pge_board_init();
  if (pge::init() != pge::Ok) fail("pge::init");  // display init also brings up the SDRAM
  memset(g_ext, 0, sizeof g_ext);                 // NOLOAD section: startup never zeroes it
  g_log.init(g_ext, sizeof g_ext);

  pge::WorldConfig c = {};
  c.w_chunks = 4;
  c.h_chunks = 4;
  c.seed = 1234;
  c.gen = pge::flatGen;
  c.props = g_props;
  c.cache_slots = 16;
  c.change_capacity = kCells;
  c.tile_px = 8;
  c.log_arena = &g_log;
  pge::Arena& ia = pge::engine().initArena();
  if (g_world.init(c, ia) != pge::Ok) fail("world init");

  // Fill: ascending keys, so every set appends; fg (0,0) is left out for the front insert below.
  uint32_t t0 = pge_hal_time_us();
  for (int ch = 0; ch < 16; ch++)
    for (int l = 0; l < 2; l++)
      for (int i = 0; i < pge::kChunkTiles; i++) {
        if (ch == 0 && l == 0 && i == 0) continue;
        const int tx = (ch % 4) * 32 + (i & 31), ty = (ch / 4) * 32 + (i >> 5);
        if (g_world.set(tx, ty, (pge::Layer)l, want(tx, ty, l)) != pge::Ok) fail("fill");
      }
  const uint32_t fill_us = pge_hal_time_us() - t0;
  t0 = pge_hal_time_us();
  const pge::Error ins = g_world.set(0, 0, pge::Layer::Fg, 9);  // memmove of 32,767 entries (163,835 B) in SDRAM
  const uint32_t ins_us = pge_hal_time_us() - t0;
  if (ins != pge::Ok || g_world.changeCount() != kCells) fail("front insert");
  t0 = pge_hal_time_us();
  const pge::Error ow = g_world.set(70, 70, pge::Layer::Wall, want(70, 70, 1));  // full log: overwrite, no growth
  const uint32_t ow_us = pge_hal_time_us() - t0;
  if (ow != pge::Ok || g_world.changeCount() != kCells) fail("overwrite");

  // Load every chunk: generation plus applying its 2,048 logged edits.
  uint32_t load_sum = 0, load_max = 0;
  for (int cy = 0; cy < 4; cy++)
    for (int cx = 0; cx < 4; cx++) {
      t0 = pge_hal_time_us();
      g_world.ensureLoaded(pge::Rect{cx * 256 + 1, cy * 256 + 1, 4, 4}, 1);
      const uint32_t dt = pge_hal_time_us() - t0;
      load_sum += dt;
      if (dt > load_max) load_max = dt;
    }
  if (g_world.chunksLoaded() != 16) fail("load");
  for (int ty = 0; ty < 128; ty++)
    for (int tx = 0; tx < 128; tx++)
      if (g_world.get(tx, ty, pge::Layer::Fg) != want(tx, ty, 0) || g_world.get(tx, ty, pge::Layer::Wall) != want(tx, ty, 1))
        fail("readback");

  uint32_t* q = _end;
  while (q < _estack && *q == 0xA5A5A5A5u) q++;
  pge_dbg_printf("# PLANETMEM world=4x4 tile_px=8 slots=16 loaded=%u change=%lu/%lu\n", (unsigned)g_world.chunksLoaded(),
                 (unsigned long)g_world.changeCount(), (unsigned long)g_world.changeCapacity());
  pge_dbg_printf("time_us fill=%lu (32767 appends) front_insert=%lu overwrite=%lu load_avg=%lu load_max=%lu\n",
                 (unsigned long)fill_us, (unsigned long)ins_us, (unsigned long)ow_us, (unsigned long)(load_sum / 16),
                 (unsigned long)load_max);
  pge_dbg_printf("ram data=%lu bss=%lu end=0x%08lx estack=0x%08lx stack_hw=%lu of %lu\n",
                 (unsigned long)((_edata - _sdata) * 4), (unsigned long)((_ebss - _sbss) * 4), (unsigned long)_end,
                 (unsigned long)_estack, (unsigned long)((_estack - q) * 4), (unsigned long)((_estack - _end) * 4));
  pge_dbg_printf("arena init hw=%lu cap=%lu failed=%lu | log(sdram) hw=%lu cap=%lu failed=%lu\n",
                 (unsigned long)ia.highWater(), (unsigned long)ia.capacity(), (unsigned long)ia.failed(),
                 (unsigned long)g_log.highWater(), (unsigned long)g_log.capacity(), (unsigned long)g_log.failed());
  pge_dbg_printf("sdram section 0x%08lx..0x%08lx (%lu B)\n", (unsigned long)_ssdram, (unsigned long)_esdram,
                 (unsigned long)((_esdram - _ssdram) * 4));
  pge_dbg_puts("# PLANETMEM PASS\n");
  pge_board_led(4, 1);
  for (;;) {}
}
