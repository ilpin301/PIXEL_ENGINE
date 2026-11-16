// World tests (S5.1a): flat generator, cache + LRU eviction, change log (edits survive eviction), bounds,
// activation hook, moveAndCollide(World) vs moveAndCollide(TileGrid), and a print-only change-log insert timing.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "pge/collision.h"
#include "pge/world.h"

#define CHECK(c)                                                          \
  do {                                                                    \
    if (!(c)) {                                                           \
      std::printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); \
      return 1;                                                           \
    }                                                                     \
  } while (0)

using namespace pge;

static TileProps g_props[256];  // 1 grass solid, 2 dirt solid, 3 wall, 4 door (activatable), 5 solid, 6 one-way, 7 hazard
alignas(8) static uint8_t g_buf[2][1 << 18];

static void initProps() {
  g_props[1].flags = kTileSolid;
  g_props[2].flags = kTileSolid;
  g_props[4].flags = kTileActivatable;
  g_props[5].flags = kTileSolid;
  g_props[6].flags = kTileOneWay;
  g_props[7].flags = kTileHazard;
}

struct Env {
  Arena arena;
  World w;
  Error init(int idx, int wc, int hc, int slots, uint32_t cap, ActivateFn act = nullptr, void* user = nullptr) {
    arena.init(g_buf[idx], sizeof(g_buf[idx]));
    WorldConfig c{};
    c.w_chunks = (uint16_t)wc;
    c.h_chunks = (uint16_t)hc;
    c.seed = 7;
    c.gen = flatGen;
    c.on_activate = act;
    c.user = user;
    c.props = g_props;
    c.cache_slots = (uint16_t)slots;
    c.change_capacity = cap;
    c.tile_px = 16;
    return w.init(c, arena);
  }
};

static Rect chunkView(int cx, int cy, int ncx = 1) { return Rect{cx * 512, cy * 512, ncx * 512, 16}; }

static uint32_t g_rng = 12345;
static uint32_t rnd() { g_rng = g_rng * 1664525u + 1013904223u; return g_rng >> 8; }

struct ActLog { int calls, tx, ty, tile; void* user_seen; };
static void onAct(World&, int tx, int ty, uint8_t tile, void* user) {
  ActLog* l = static_cast<ActLog*>(user);
  l->calls++; l->tx = tx; l->ty = ty; l->tile = tile; l->user_seen = user;
}

static int testGen() {
  uint8_t fg[kChunkTiles], wall[kChunkTiles];
  flatGen(fg, wall, 0, 0, 0);
  CHECK(fg[15 * 32 + 5] == 0 && fg[16 * 32 + 5] == 1 && fg[17 * 32 + 5] == 2 && fg[31 * 32] == 2);
  CHECK(wall[16 * 32] == 0 && wall[17 * 32] == 3 && wall[5 * 32] == 0);
  flatGen(fg, wall, 3, 1, 99);  // world rows 32..63: all dirt, all wall
  CHECK(fg[0] == 2 && fg[kChunkTiles - 1] == 2 && wall[0] == 3 && wall[kChunkTiles - 1] == 3);
  return 0;
}

static int testBasics() {
  Env e;
  CHECK(e.init(0, 8, 4, 4, 0) == Ok);
  CHECK(e.w.changeCapacity() == 4096 && e.w.tilePx() == 16 && e.w.seed() == 7);
  World& w = e.w;
  CHECK(w.get(0, 16, Layer::Fg) == kTileVoid);  // not loaded
  CHECK(w.tileFlags(0, 16) == kTileSolid);
  CHECK(w.ensureLoaded(Rect{0, 0, 16, 16}) == 0);
  CHECK(w.isLoaded(0, 0) && w.chunksLoaded() == 1);
  CHECK(w.get(0, 16, Layer::Fg) == 1 && w.get(31, 17, Layer::Fg) == 2 && w.get(0, 15, Layer::Fg) == 0);
  CHECK(w.get(0, 17, Layer::Wall) == 3 && w.get(0, 16, Layer::Wall) == 0);
  CHECK(w.tileFlags(0, 15) == 0 && w.tileFlags(0, 16) == kTileSolid);
  CHECK(w.get(-1, 0, Layer::Fg) == kTileVoid && w.get(0, -1, Layer::Fg) == kTileVoid);
  CHECK(w.get(256, 0, Layer::Fg) == kTileVoid && w.get(0, 128, Layer::Fg) == kTileVoid);
  CHECK(!w.inBounds(-1, 0) && !w.inBounds(256, 0) && !w.inBounds(0, 128) && w.inBounds(255, 127));
  CHECK(w.set(-1, 0, Layer::Fg, 1) == InvalidArg && w.set(256, 0, Layer::Fg, 1) == InvalidArg);
  CHECK(w.set(0, 128, Layer::Wall, 1) == InvalidArg);
  CHECK(w.set(0, 0, Layer::Fg, 255) == InvalidArg);
  CHECK(w.changeCount() == 0);
  // init argument checks
  WorldConfig c{};
  Arena a;
  a.init(g_buf[1], sizeof(g_buf[1]));
  World w2;
  c.w_chunks = 2; c.h_chunks = 2; c.props = g_props; c.gen = nullptr;
  CHECK(w2.init(c, a) == InvalidArg);
  c.gen = flatGen; c.props = nullptr;
  CHECK(w2.init(c, a) == InvalidArg);
  c.props = g_props; c.w_chunks = 0;
  CHECK(w2.init(c, a) == InvalidArg);
  c.w_chunks = 256; c.h_chunks = 256;  // 65536 chunks
  CHECK(w2.init(c, a) == InvalidArg);
  c.w_chunks = 2; c.h_chunks = 2; c.cache_slots = 4000;  // 8 MB of chunk data
  CHECK(w2.init(c, a) == OutOfMemory);
  return 0;
}

static int testCache() {
  Env e;
  CHECK(e.init(0, 8, 4, 4, 0) == Ok);
  World& w = e.w;
  for (int cx = 0; cx < 4; cx++) CHECK(w.ensureLoaded(chunkView(cx, 0)) == 0);
  CHECK(w.chunksLoaded() == 4);
  // LRU: touch chunk 0, so loading chunk 4 evicts chunk 1 (oldest untouched).
  CHECK(w.ensureLoaded(chunkView(0, 0)) == 0);
  CHECK(w.ensureLoaded(chunkView(4, 0)) == 0);
  CHECK(w.isLoaded(0, 0) && !w.isLoaded(1, 0) && w.isLoaded(2, 0) && w.isLoaded(3, 0) && w.isLoaded(4, 0));
  CHECK(w.chunksLoaded() == 4 && w.get(32, 16, Layer::Fg) == kTileVoid);

  // Plain eviction order: first loaded chunk becomes void.
  Env f;
  CHECK(f.init(1, 8, 4, 4, 0) == Ok);
  for (int cx = 0; cx < 5; cx++) CHECK(f.w.ensureLoaded(chunkView(cx, 0)) == 0);
  CHECK(!f.w.isLoaded(0, 0) && f.w.get(0, 16, Layer::Fg) == kTileVoid && f.w.chunksLoaded() == 4);

  // max_loads and the missing count.
  Env g;
  CHECK(g.init(0, 8, 4, 8, 0) == Ok);
  Rect three = chunkView(0, 0, 3);
  CHECK(g.w.ensureLoaded(three) == 2 && g.w.chunksLoaded() == 1);
  CHECK(g.w.ensureLoaded(three) == 1 && g.w.chunksLoaded() == 2);
  CHECK(g.w.ensureLoaded(three) == 0 && g.w.chunksLoaded() == 3);
  CHECK(g.w.ensureLoaded(three, 0) == 0);  // nothing missing, nothing loaded
  CHECK(g.w.ensureLoaded(chunkView(5, 0, 2), 0) == 2 && !g.w.isLoaded(5, 0));
  CHECK(g.w.ensureLoaded(chunkView(5, 0, 2), 5) == 0 && g.w.isLoaded(5, 0) && g.w.isLoaded(6, 0));
  // view partly outside the world is clamped; fully outside needs nothing
  CHECK(g.w.ensureLoaded(Rect{-1000, -1000, 1016, 1016}, 100) >= 0);
  CHECK(g.w.ensureLoaded(Rect{-100, -100, 50, 50}) == 0);
  CHECK(g.w.ensureLoaded(Rect{0, 0, 0, 0}) == 0);

  // Needed chunks are never evicted by the same call.
  Env h;
  CHECK(h.init(0, 8, 4, 4, 0) == Ok);
  for (int cx = 4; cx < 8; cx++) CHECK(h.w.ensureLoaded(chunkView(cx, 0)) == 0);
  CHECK(h.w.ensureLoaded(Rect{3 * 512, 0, 4 * 512, 16}, 10) == 0);  // chunks 3..6: only chunk 7 may go
  CHECK(h.w.isLoaded(3, 0) && h.w.isLoaded(4, 0) && h.w.isLoaded(5, 0) && h.w.isLoaded(6, 0) && !h.w.isLoaded(7, 0));
  CHECK(h.w.ensureLoaded(Rect{3 * 512, 0, 5 * 512, 16}, 10) == 1);  // chunks 3..7 on 4 slots
  CHECK(h.w.isLoaded(3, 0) && h.w.isLoaded(4, 0) && h.w.isLoaded(5, 0) && h.w.isLoaded(6, 0) && h.w.chunksLoaded() == 4);
  Env i;
  CHECK(i.init(1, 8, 4, 4, 0) == Ok);
  CHECK(i.w.ensureLoaded(Rect{0, 0, 8 * 512, 16}, 100) == 4);  // 8 needed, 4 slots
  CHECK(i.w.chunksLoaded() == 4);
  return 0;
}

static int testEdits() {
  Env e;
  CHECK(e.init(0, 8, 4, 4, 0) == Ok);
  World& w = e.w;
  CHECK(w.ensureLoaded(chunkView(0, 0)) == 0);
  CHECK(w.set(3, 20, Layer::Fg, 5) == Ok && w.set(3, 20, Layer::Wall, 6) == Ok);
  CHECK(w.get(3, 20, Layer::Fg) == 5 && w.get(3, 20, Layer::Wall) == 6 && w.changeCount() == 2);
  CHECK(w.set(3, 20, Layer::Fg, 0) == Ok);  // overwrite, no growth
  CHECK(w.set(3, 20, Layer::Fg, 5) == Ok && w.changeCount() == 2);
  for (int cx = 1; cx <= 4; cx++) CHECK(w.ensureLoaded(chunkView(cx, 0)) == 0);
  CHECK(!w.isLoaded(0, 0));
  CHECK(w.ensureLoaded(chunkView(0, 0)) == 0);
  CHECK(w.get(3, 20, Layer::Fg) == 5 && w.get(3, 20, Layer::Wall) == 6);
  CHECK(w.get(4, 20, Layer::Fg) == 2 && w.get(3, 21, Layer::Wall) == 3);  // neighbours untouched

  // edit in an unloaded chunk: only logged, applied on first load
  CHECK(!w.isLoaded(6, 2));
  CHECK(w.set(6 * 32 + 1, 2 * 32 + 3, Layer::Fg, 4) == Ok && w.changeCount() == 3);
  CHECK(w.get(6 * 32 + 1, 2 * 32 + 3, Layer::Fg) == kTileVoid);
  CHECK(w.ensureLoaded(chunkView(6, 2)) == 0);
  CHECK(w.get(6 * 32 + 1, 2 * 32 + 3, Layer::Fg) == 4 && w.get(6 * 32 + 2, 2 * 32 + 3, Layer::Fg) == 2);

  // sort order: random edits across all chunks, every chunk reads back right after its (re)load
  Env f;
  CHECK(f.init(1, 8, 4, 4, 4096) == Ok);
  std::vector<uint8_t> shadow(2 * 256 * 128, 0xFF);
  for (int n = 0; n < 1500; n++) {
    const int tx = (int)(rnd() % 256), ty = (int)(rnd() % 128), l = (int)(rnd() % 2);
    const uint8_t t = (uint8_t)(rnd() % 8);
    CHECK(f.w.set(tx, ty, (Layer)l, t) == Ok);
    shadow[(size_t)l * 256 * 128 + (size_t)ty * 256 + tx] = t;
  }
  uint32_t distinct = 0;
  for (uint8_t v : shadow) distinct += v != 0xFF;
  CHECK(f.w.changeCount() == distinct);
  for (int cy = 0; cy < 4; cy++)
    for (int cx = 0; cx < 8; cx++) {
      CHECK(f.w.ensureLoaded(chunkView(cx, cy)) == 0);
      uint8_t fg[kChunkTiles], wall[kChunkTiles];
      flatGen(fg, wall, cx, cy, 0);
      for (int ly = 0; ly < 32; ly++)
        for (int lx = 0; lx < 32; lx++) {
          const int tx = cx * 32 + lx, ty = cy * 32 + ly;
          for (int l = 0; l < 2; l++) {
            const uint8_t s = shadow[(size_t)l * 256 * 128 + (size_t)ty * 256 + tx];
            const uint8_t exp = s != 0xFF ? s : (l == 0 ? fg[ly * 32 + lx] : wall[ly * 32 + lx]);
            CHECK(f.w.get(tx, ty, (Layer)l) == exp);
          }
        }
    }

  // full log
  Env g;
  CHECK(g.init(0, 8, 4, 4, 10) == Ok);
  CHECK(g.w.changeCapacity() == 10 && !g.w.changeLogNearFull());
  for (int n = 0; n < 8; n++) CHECK(g.w.set(n, 0, Layer::Fg, 5) == Ok);
  CHECK(!g.w.changeLogNearFull());
  CHECK(g.w.set(8, 0, Layer::Fg, 5) == Ok && g.w.changeLogNearFull());  // 9 of 10
  CHECK(g.w.set(9, 0, Layer::Fg, 5) == Ok && g.w.changeCount() == 10);
  CHECK(g.w.set(10, 0, Layer::Fg, 5) == OutOfMemory && g.w.changeCount() == 10);
  CHECK(g.w.set(0, 0, Layer::Fg, 6) == Ok && g.w.changeCount() == 10);  // overwrite still works
  CHECK(g.w.set(0, 0, Layer::Wall, 6) == OutOfMemory);                   // other layer is a new cell
  CHECK(g.w.ensureLoaded(chunkView(0, 0)) == 0 && g.w.get(0, 0, Layer::Fg) == 6 && g.w.get(10, 0, Layer::Fg) == 0);
  return 0;
}

static int testDeterminism() {
  Env a, b;
  CHECK(a.init(0, 4, 2, 8, 0) == Ok && b.init(1, 4, 2, 8, 0) == Ok);
  const Rect all{0, 0, 4 * 512, 2 * 512};
  CHECK(a.w.ensureLoaded(all, 100) == 0 && b.w.ensureLoaded(all, 100) == 0);
  for (int ty = 0; ty < 64; ty++)
    for (int tx = 0; tx < 128; tx++)
      for (int l = 0; l < 2; l++) CHECK(a.w.get(tx, ty, (Layer)l) == b.w.get(tx, ty, (Layer)l));
  return 0;
}

static int testActivate() {
  ActLog log{0, 0, 0, 0, nullptr};
  Env e;
  CHECK(e.init(0, 2, 2, 4, 0, onAct, &log) == Ok);
  World& w = e.w;
  CHECK(w.ensureLoaded(chunkView(0, 0)) == 0);
  CHECK(!w.activate(5, 5) && log.calls == 0);  // air
  CHECK(!w.activate(0, 16) && log.calls == 0);  // grass, not activatable
  CHECK(w.set(5, 5, Layer::Fg, 4) == Ok);
  CHECK(w.activate(5, 5) && log.calls == 1 && log.tx == 5 && log.ty == 5 && log.tile == 4 && log.user_seen == &log);
  CHECK(!w.activate(-1, 0) && !w.activate(64, 0) && !w.activate(0, 64) && log.calls == 1);
  CHECK(!w.activate(40, 5) && log.calls == 1);  // unloaded chunk reads as void
  Env n;
  CHECK(n.init(1, 2, 2, 4, 0) == Ok);  // no callback
  CHECK(n.w.ensureLoaded(chunkView(0, 0)) == 0 && n.w.set(5, 5, Layer::Fg, 4) == Ok && !n.w.activate(5, 5));
  return 0;
}

static bool sameMove(const MoveResult& a, const MoveResult& b) {
  return a.hit_x == b.hit_x && a.hit_y == b.hit_y && a.on_ground == b.on_ground && a.touched_flags == b.touched_flags;
}

static int testCollision() {
  Env e;
  CHECK(e.init(0, 2, 1, 2, 0) == Ok);  // 64 x 32 tiles, fully loaded
  World& w = e.w;
  CHECK(w.ensureLoaded(Rect{0, 0, 1024, 512}, 10) == 0);
  // obstacles: solid blocks, one-way and hazard tiles above the surface (rows 8..15)
  for (int n = 0; n < 120; n++) {
    const int tx = (int)(rnd() % 64), ty = 8 + (int)(rnd() % 8);
    const uint8_t t = (uint8_t)(n % 3 == 0 ? 6 : n % 3 == 1 ? 7 : 5);
    CHECK(w.set(tx, ty, Layer::Fg, t) == Ok);
  }
  std::vector<uint8_t> tiles(64 * 32);
  for (int ty = 0; ty < 32; ty++)
    for (int tx = 0; tx < 64; tx++) tiles[(size_t)ty * 64 + tx] = w.get(tx, ty, Layer::Fg);
  const TileGrid g{tiles.data(), 64, 32, 16, 16, g_props};

  Body a{}, b{};
  int moves = 0;
  while (moves < 2000) {
    // fresh random start every 40 moves, in the air region, not overlapping a blocking tile
    if (moves % 40 == 0) {
      a = Body{(int)(rnd() % 900), (int)(rnd() % 160), 4 + (int)(rnd() % 20), 4 + (int)(rnd() % 20)};
      if (moveAndCollide(g, a, 0, 0).touched_flags & kTileSolid) continue;
      b = a;
    }
    const int dx = (int)(rnd() % 81) - 40, dy = (int)(rnd() % 81) - 40;
    const MoveResult ra = moveAndCollide(g, a, dx, dy);
    const MoveResult rb = moveAndCollide(w, b, dx, dy);
    CHECK(a.x == b.x && a.y == b.y && sameMove(ra, rb));
    moves++;
  }

  // unloaded chunk is solid: a body moving right from chunk 0 stops at the chunk border
  Env f;
  CHECK(f.init(1, 2, 1, 2, 0) == Ok && f.w.ensureLoaded(chunkView(0, 0)) == 0 && !f.w.isLoaded(1, 0));
  Body p{490, 100, 10, 10};
  const MoveResult r = moveAndCollide(f.w, p, 50, 0);
  CHECK(r.hit_x && p.x == 502 && p.y == 100);
  CHECK(f.w.ensureLoaded(chunkView(1, 0)) == 0);
  p = Body{490, 100, 10, 10};
  CHECK(!moveAndCollide(f.w, p, 50, 0).hit_x && p.x == 540);
  return 0;
}

static int testInsertTiming() {
  Env e;
  CHECK(e.init(0, 2, 1, 2, 4096) == Ok);
  using clk = std::chrono::steady_clock;
  long long total = 0, mx = 0;
  int n = 0;
  for (int c = 1; c >= 0; c--)
    for (int l = 1; l >= 0; l--)
      for (int i = 1023; i >= 0; i--, n++) {
        const int tx = c * 32 + (i & 31), ty = i >> 5;  // descending keys: every insert goes to the front
        const auto t0 = clk::now();
        const Error er = e.w.set(tx, ty, (Layer)l, 5);
        const long long ns = std::chrono::duration_cast<std::chrono::nanoseconds>(clk::now() - t0).count();
        CHECK(er == Ok);
        total += ns;
        if (ns > mx) mx = ns;
      }
  CHECK(n == 4096 && e.w.changeCount() == 4096);
  std::printf("change-log worst-case insert: %.1f us total, %.1f us max single\n", total / 1000.0, mx / 1000.0);
  return 0;
}

int main() {
  initProps();
  if (testGen()) return 1;
  if (testBasics()) return 1;
  if (testCache()) return 1;
  if (testEdits()) return 1;
  if (testDeterminism()) return 1;
  if (testActivate()) return 1;
  if (testCollision()) return 1;
  if (testInsertTiming()) return 1;
  std::printf("world: all tests passed\n");
  return 0;
}
