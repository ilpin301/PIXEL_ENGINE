// Procedural terrain tests (S5.1b): determinism (golden checksums, garbage-prefilled buffers), seams (chunk bytes
// equal a per-tile world-coordinate reference), terrain shape, caves and ores, seed sensitivity, and use through
// World (real terrain, collision on the surface, edits survive eviction and reload). Goldens were captured from the
// implementation (seed 12345) and checked by eye on an ASCII dump, not derived independently.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include "pge/collision.h"
#include "pge/world.h"
#include "pge/worldgen.h"

#define CHECK(c)                                                          \
  do {                                                                    \
    if (!(c)) {                                                           \
      std::printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); \
      return 1;                                                           \
    }                                                                     \
  } while (0)

using namespace pge;

static TileProps g_props[256];
alignas(8) static uint8_t g_buf[1 << 18];

static uint32_t fnv(const uint8_t* p, int n, uint32_t h = 2166136261u) {
  for (int i = 0; i < n; i++) { h ^= p[i]; h *= 16777619u; }
  return h;
}

static uint32_t chunkHash(int cx, int cy, uint32_t seed) {
  uint8_t fg[kChunkTiles], wall[kChunkTiles];
  terrainGen(fg, wall, cx, cy, seed);
  return fnv(wall, kChunkTiles, fnv(fg, kChunkTiles));
}

static int testDeterminism() {
  // Golden checksums: same seed gives the same bytes on every run and every build.
  CHECK(chunkHash(0, 0, 12345) == 3523624389u);
  CHECK(chunkHash(0, 1, 12345) == 4040276979u);
  CHECK(chunkHash(1, 1, 12345) == 2708531697u);
  CHECK(chunkHash(5, 2, 12345) == 609311873u);
  CHECK(chunkHash(3, 3, 12345) == 2213387331u);
  // Different prefilled buffers give identical output (all 2 x 1,024 bytes are written, no hidden state).
  uint8_t a[2][kChunkTiles], b[2][kChunkTiles];
  memset(a, 0xAA, sizeof(a));
  memset(b, 0x55, sizeof(b));
  terrainGen(a[0], a[1], 2, 1, 777);
  terrainGen(b[0], b[1], 2, 1, 777);
  CHECK(memcmp(a, b, sizeof(a)) == 0);
  // Seed changes the chunk.
  CHECK(chunkHash(1, 1, 1) != chunkHash(1, 1, 2));
  CHECK(chunkHash(1, 1, 12345) != chunkHash(1, 1, 12346));
  // Generating other chunks in between changes nothing.
  const uint32_t h = chunkHash(3, 2, 99);
  (void)chunkHash(7, 7, 99);
  CHECK(chunkHash(3, 2, 99) == h);
  return 0;
}

static int testSeams() {
  const uint32_t seeds[] = {1, 99, 12345};
  for (uint32_t seed : seeds)
    for (int cy = 0; cy < 4; cy++)
      for (int cx = 0; cx < 4; cx++) {
        uint8_t fg[kChunkTiles], wall[kChunkTiles];
        terrainGen(fg, wall, cx, cy, seed);
        for (int ly = 0; ly < kChunkSize; ly++)
          for (int lx = 0; lx < kChunkSize; lx++) {
            uint8_t rf, rw;
            terrainTile(cx * kChunkSize + lx, cy * kChunkSize + ly, seed, &rf, &rw);
            CHECK(fg[ly * kChunkSize + lx] == rf && wall[ly * kChunkSize + lx] == rw);
          }
      }
  // Heights are continuous over chunk edges and negative columns are well defined.
  for (uint32_t seed : seeds)
    for (int x = -64; x < 4096; x++) {
      int d = terrainHeight(x + 1, seed) - terrainHeight(x, seed);
      if (d < 0) d = -d;
      CHECK(d <= 3);
    }
  return 0;
}

static int testShape() {
  const uint32_t seeds[] = {1, 12345};
  for (uint32_t seed : seeds) {
    int hmin = 1 << 30, hmax = -(1 << 30);
    for (int x = 0; x < 4096; x++) {
      const int h = terrainHeight(x, seed);
      if (h < hmin) hmin = h;
      if (h > hmax) hmax = h;
    }
    CHECK(hmin >= kGenSurfaceBase - 20 && hmax <= kGenSurfaceBase + 19);
    CHECK(hmax - hmin >= 25);  // hills, not a flat line
  }
  int differ = 0;
  for (int x = 0; x < 4096; x++) differ += terrainHeight(x, 1) != terrainHeight(x, 2);
  CHECK(differ > 2000);

  // Column structure of 256 columns x 128 rows.
  for (int x = 0; x < 256; x++) {
    const int h = terrainHeight(x, 12345);
    for (int y = 0; y < 128; y++) {
      uint8_t fg, wall;
      terrainTile(x, y, 12345, &fg, &wall);
      const int depth = y - h;
      if (depth < 0) {
        CHECK(fg == 0 && wall == 0);
      } else if (depth == 0) {
        CHECK(fg == kGenGrass && wall == 0);
      } else {
        CHECK(wall == (depth < 8 ? kGenWallDirt : kGenWallStone));
        if (depth <= 1) CHECK(fg == kGenDirt);
        if (depth < kGenCaveMinDepth) CHECK(fg == kGenDirt || fg == kGenStone);  // no cave, no ore yet
        if (fg == kGenOre) CHECK(depth >= kGenOreMinDepth);
        if (fg == kGenDeepOre) CHECK(depth >= kGenDeepOreMinDepth);
        CHECK(fg <= kGenDeepOre);
      }
    }
  }
  return 0;
}

static int testFeatures() {
  const uint32_t seeds[] = {1, 12345};
  for (uint32_t seed : seeds) {
    long cave = 0, elig = 0, ore = 0, oreElig = 0, deep = 0, deepElig = 0;
    for (int tx = 0; tx < 1024; tx++)
      for (int ty = 0; ty < 256; ty++) {
        uint8_t fg, wall;
        terrainTile(tx, ty, seed, &fg, &wall);
        const int depth = ty - terrainHeight(tx, seed);
        if (depth >= kGenCaveMinDepth) { elig++; if (fg == 0) { cave++; CHECK(wall == kGenWallStone); } }
        if (depth >= kGenOreMinDepth && fg != 0) { oreElig++; ore += fg == kGenOre; }
        if (depth >= kGenDeepOreMinDepth && fg != 0) { deepElig++; deep += fg == kGenDeepOre; }
      }
    CHECK(cave * 100 >= elig * 5 && cave * 100 <= elig * 20);  // 5%..20% of deep tiles are cave (measured 11-12%)
    CHECK(ore * 100 >= oreElig * 1 && ore * 40 <= oreElig * 1);  // about 1/64: between 1/100 and 1/40
    CHECK(deep * 200 >= deepElig * 1 && deep * 80 <= deepElig * 1);  // about 1/128: between 1/200 and 1/80
  }
  return 0;
}

static int testThroughWorld() {
  for (int t : {1, 2, 3, 4, 5}) g_props[t].flags = kTileSolid;
  Arena arena;
  arena.init(g_buf, sizeof(g_buf));
  WorldConfig c{};
  c.w_chunks = 8; c.h_chunks = 4; c.seed = 12345; c.gen = terrainGen; c.props = g_props;
  c.cache_slots = 4; c.tile_px = 16;
  World w;
  CHECK(w.init(c, arena) == Ok);
  // Surface chunks of columns 0..31 are rows 0..63, so chunks (0,0) and (0,1).
  CHECK(w.ensureLoaded(Rect{0, 0, 512, 1024}, 8) == 0);
  for (int x = 0; x < 32; x++) {
    const int h = terrainHeight(x, 12345);
    CHECK(w.get(x, h, Layer::Fg) == kGenGrass && w.get(x, h - 1, Layer::Fg) == 0);
    CHECK(w.get(x, h + 1, Layer::Fg) == kGenDirt);
  }
  // A body dropped from above lands on the grass.
  const int hx = terrainHeight(10, 12345);
  Body b{10 * 16 + 2, (hx - 8) * 16, 12, 14};
  MoveResult r = moveAndCollide(w, b, 0, 400);
  CHECK(r.on_ground && r.hit_y && b.y + b.h == hx * 16);
  // Edits survive eviction: dig a grass tile, place a block in the air, then push both chunks out of the cache.
  const int h5 = terrainHeight(5, 12345), h6 = terrainHeight(6, 12345);
  CHECK(w.set(5, h5, Layer::Fg, kTileAir) == Ok);
  CHECK(w.set(6, h6 - 1, Layer::Fg, kGenStone) == Ok);
  static uint8_t snap[64][32];
  for (int y = 0; y < 64; y++) for (int x = 0; x < 32; x++) snap[y][x] = w.get(x, y, Layer::Fg);
  CHECK(snap[h5][5] == 0 && snap[h6 - 1][6] == kGenStone);
  CHECK(w.ensureLoaded(Rect{512, 0, 2048, 16}, 8) == 0);  // chunks (1..4, 0) fill all 4 slots
  CHECK(!w.isLoaded(0, 0) && !w.isLoaded(0, 1));
  CHECK(w.get(5, h5, Layer::Fg) == kTileVoid);
  CHECK(w.ensureLoaded(Rect{0, 0, 512, 1024}, 8) == 0);
  for (int y = 0; y < 64; y++) for (int x = 0; x < 32; x++) CHECK(w.get(x, y, Layer::Fg) == snap[y][x]);
  return 0;
}

static void printTiming() {  // print-only: Debug host build, not representative (see STATUS for the -O2 numbers)
  static uint8_t fg[kChunkTiles], wall[kChunkTiles];
  volatile uint32_t sink = 0;
  const int iters = 200;
  for (int cy = 0; cy < 4; cy += 3) {
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < iters; i++) { terrainGen(fg, wall, i & 63, cy, 12345); sink = sink + fg[i & 1023]; }
    const auto t1 = std::chrono::steady_clock::now();
    std::printf("terrainGen chunk row %d: %.1f us/chunk (host, test build, not representative)\n", cy,
                std::chrono::duration<double, std::micro>(t1 - t0).count() / iters);
  }
  (void)sink;
}

int main() {
  if (testDeterminism()) return 1;
  if (testSeams()) return 1;
  if (testShape()) return 1;
  if (testFeatures()) return 1;
  if (testThroughWorld()) return 1;
  printTiming();
  std::printf("test_worldgen: all passed\n");
  return 0;
}
