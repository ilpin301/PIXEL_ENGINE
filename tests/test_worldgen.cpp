// Procedural terrain tests (S5.1b): determinism (golden checksums, garbage-prefilled buffers), seams (chunk bytes
// equal a per-tile world-coordinate reference), terrain shape, caves and ores, seed sensitivity, and use through
// World (real terrain, collision on the surface, edits survive eviction and reload). Goldens were captured from the
// implementation (seed 12345) and checked by eye on an ASCII dump, not derived independently.
// S8.5 round tests (testRound*): expectations follow the disc spec (formulas in the tests); only the chunk goldens are captured.
// S8.5 review fixes: grass band of one full tile, exact ore depth bounds, ore zone gate and wrap tests, ore at 64 seeds
// (the zone-test thresholds and the seed range 1..64 are calibrated from measurements, see the comments at each test).
#include <chrono>
#include <cmath>
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

// ---- S8.5 round world: expectations come from the disc spec (formulas below), not from worldgen.cpp ----
static const RoundSpec kRoundSpecs[3] = {kPlanetSpec, kAsteroid1Spec, kAsteroid2Spec};
static const GenFn kRoundFns[3] = {planetGen, asteroid1Gen, asteroid2Gen};
static uint8_t g_rfg[128 * 128], g_rwall[128 * 128];  // whole-map buffers, largest preset is 128 x 128

static long sq(long v) { return v * v; }

// Squared distance of the tile centre from the grid centre in doubled tile units (2t+1-G per axis).
static long roundD2(const RoundSpec& s, int tx, int ty) {
  const long g = s.chunks * kChunkSize;
  return sq(2L * tx + 1 - g) + sq(2L * ty + 1 - g);
}

static uint32_t roundChunkHash(const RoundSpec& s, int cx, int cy, uint32_t seed) {
  uint8_t fg[kChunkTiles], wall[kChunkTiles];
  roundGen(s, fg, wall, cx, cy, seed);
  return fnv(wall, kChunkTiles, fnv(fg, kChunkTiles));
}

// Builds the whole G x G map of s chunk by chunk into g_rfg / g_rwall (row-major, G bytes per row).
static void roundMap(const RoundSpec& s, uint32_t seed) {
  uint8_t fg[kChunkTiles], wall[kChunkTiles];
  const int g = s.chunks * kChunkSize;
  for (int cy = 0; cy < s.chunks; cy++)
    for (int cx = 0; cx < s.chunks; cx++) {
      roundGen(s, fg, wall, cx, cy, seed);
      for (int ly = 0; ly < kChunkSize; ly++)
        for (int lx = 0; lx < kChunkSize; lx++) {
          const int o = (cy * kChunkSize + ly) * g + cx * kChunkSize + lx;
          g_rfg[o] = fg[ly * kChunkSize + lx];
          g_rwall[o] = wall[ly * kChunkSize + lx];
        }
    }
}

static int isqrtL(long n) {
  int r = (int)std::sqrt((double)n);
  while ((long)r * r > n) r--;
  while ((long)(r + 1) * (r + 1) <= n) r++;
  return r;
}

// Depth of tile (tx, ty) below the rim in whole tiles: (2R - floor(sqrt(d2))) / 2 with d2 from roundD2; -1 outside the disc.
static int roundDepth(const RoundSpec& s, int tx, int ty) {
  const long d2 = roundD2(s, tx, ty);
  return d2 > sq(2L * s.radius) ? -1 : (2 * s.radius - isqrtL(d2)) / 2;
}

// Diamond pseudo-angle of a doubled-centre offset: 0..255 once around the centre, 64 per quadrant, 255 wraps to 0. The ore
// zone noise has a lattice of 32 of these steps by 8 tiles of depth (noiseRing in worldgen.cpp).
static int roundAngle(int dx, int dy) {
  const int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy, sum = ax + ay;
  if (sum == 0) return 0;
  if (dy >= 0) return dx >= 0 ? ay * 64 / sum : 64 + ax * 64 / sum;
  return dx < 0 ? 128 + ay * 64 / sum : 192 + ax * 64 / sum;
}

static int testRoundDeterminism() {
  // Golden checksums captured from the implementation (seed 12345), not derived independently (regenerated for the one-tile
  // grass band, and again for the cores: the four chunks that hold core tiles changed, the two far corners did not).
  CHECK(roundChunkHash(kPlanetSpec, 0, 0, 12345) == 3393096901u);
  CHECK(roundChunkHash(kPlanetSpec, 1, 1, 12345) == 433487596u);
  CHECK(roundChunkHash(kPlanetSpec, 2, 1, 12345) == 982779125u);
  CHECK(roundChunkHash(kPlanetSpec, 3, 3, 12345) == 1600163915u);
  CHECK(roundChunkHash(kAsteroid1Spec, 0, 0, 12345) == 4081954864u);
  CHECK(roundChunkHash(kAsteroid2Spec, 1, 0, 12345) == 1138706502u);
  // Without a core the same chunks are the old bytes (the goldens from before the cores).
  const RoundSpec noCore = {4, 60};
  CHECK(roundChunkHash(noCore, 1, 1, 12345) == 3415786110u && roundChunkHash(noCore, 2, 1, 12345) == 2910136831u);
  // Different prefilled buffers give identical output (all 2 x 1,024 bytes are written, no hidden state).
  uint8_t a[2][kChunkTiles], b[2][kChunkTiles];
  memset(a, 0xAA, sizeof(a));
  memset(b, 0x55, sizeof(b));
  roundGen(kPlanetSpec, a[0], a[1], 1, 2, 777);
  roundGen(kPlanetSpec, b[0], b[1], 1, 2, 777);
  CHECK(memcmp(a, b, sizeof(a)) == 0);
  // Seed changes the planet (some chunk of the 16 differs).
  int differ = 0;
  for (int cy = 0; cy < 4; cy++)
    for (int cx = 0; cx < 4; cx++) differ += roundChunkHash(kPlanetSpec, cx, cy, 1) != roundChunkHash(kPlanetSpec, cx, cy, 2);
  CHECK(differ > 0);
  // Generating another chunk in between changes nothing.
  const uint32_t h = roundChunkHash(kPlanetSpec, 2, 3, 99);
  (void)roundChunkHash(kPlanetSpec, 0, 1, 99);
  CHECK(roundChunkHash(kPlanetSpec, 2, 3, 99) == h);
  // Extreme seeds are stable and differ from each other.
  const uint32_t z = roundChunkHash(kPlanetSpec, 1, 1, 0), m = roundChunkHash(kPlanetSpec, 1, 1, 0xFFFFFFFFu);
  CHECK(roundChunkHash(kPlanetSpec, 1, 1, 0) == z && roundChunkHash(kPlanetSpec, 1, 1, 0xFFFFFFFFu) == m);
  CHECK(z != m);
  return 0;
}

// Only proves chunk-to-tile indexing (roundGen against roundTile in world coordinates, wrapper equality, chunks outside
// the grid are air); roundGen is a loop over roundTile, so the independent seam / shape proof is testRoundShape.
static int testRoundSeams() {
  static const uint8_t zero[kChunkTiles] = {0};
  const uint32_t seeds[] = {0, 1, 99, 12345, 0xFFFFFFFFu};
  for (int k = 0; k < 3; k++) {
    const RoundSpec& s = kRoundSpecs[k];
    for (uint32_t seed : seeds)
      for (int cy = -2; cy <= s.chunks + 1; cy++)
        for (int cx = -2; cx <= s.chunks + 1; cx++) {
          uint8_t fg[kChunkTiles], wall[kChunkTiles], wf[kChunkTiles], ww[kChunkTiles];
          roundGen(s, fg, wall, cx, cy, seed);
          kRoundFns[k](wf, ww, cx, cy, seed);  // the GenFn wrapper is the same function
          CHECK(memcmp(fg, wf, kChunkTiles) == 0 && memcmp(wall, ww, kChunkTiles) == 0);
          for (int ly = 0; ly < kChunkSize; ly++)
            for (int lx = 0; lx < kChunkSize; lx++) {
              uint8_t rf, rw;
              roundTile(s, cx * kChunkSize + lx, cy * kChunkSize + ly, seed, &rf, &rw);
              CHECK(fg[ly * kChunkSize + lx] == rf && wall[ly * kChunkSize + lx] == rw);
            }
          if (cx < 0 || cy < 0 || cx >= s.chunks || cy >= s.chunks)  // chunk outside the grid is all air
            CHECK(memcmp(fg, zero, kChunkTiles) == 0 && memcmp(wall, zero, kChunkTiles) == 0);
        }
  }
  return 0;
}

// Core (spec): kGenCore on exactly the tiles whose centre is within s.core tiles of the grid centre (doubled offsets
// d2 <= (2 core)^2), stone wall behind it, every other tile as without a core, core clipped to the disc, core 0 (and a
// negative one) is no core. Hand counts: core 1 is the 2 x 2 centre block (doubled offsets (+-1, +-1); the next ring
// has d2 10 > 4), core 2 adds (+-1, +-3) and (+-3, +-1) = 12 tiles, core 3 adds (+-1, +-5), (+-5, +-1), (+-3, +-5) and
// (+-5, +-3) = 32 tiles.
static int testRoundCore() {
  static uint8_t base[32 * 32];
  const int expect[4] = {0, 4, 12, 32};
  for (int c = 0; c < 4; c++) {
    const RoundSpec s = {1, 14, c}, none = {1, 14};
    roundMap(none, 12345);
    memcpy(base, g_rfg, sizeof(base));
    roundMap(s, 12345);
    int n = 0;
    for (int ty = 0; ty < 32; ty++)
      for (int tx = 0; tx < 32; tx++) {
        const bool core = c > 0 && roundD2(s, tx, ty) <= sq(2L * c);
        CHECK((g_rfg[ty * 32 + tx] == kGenCore) == core);
        if (core) CHECK(g_rwall[ty * 32 + tx] == kGenWallStone);
        else CHECK(g_rfg[ty * 32 + tx] == base[ty * 32 + tx]);
        n += core;
      }
    CHECK(n == expect[c]);
  }
  // The presets all have one, with the centre tiles in it; a core wider than the disc fills the disc and nothing else.
  for (const RoundSpec& s : kRoundSpecs) {
    const int g = s.chunks * kChunkSize;
    CHECK(s.core > 0);
    roundMap(s, 12345);
    for (int k = 0; k < 4; k++) CHECK(g_rfg[(g / 2 - 1 + k / 2) * g + g / 2 - 1 + k % 2] == kGenCore);
  }
  const RoundSpec huge = {1, 14, 100}, neg = {1, 14, -3}, none = {1, 14};
  roundMap(huge, 12345);
  for (int ty = 0; ty < 32; ty++)
    for (int tx = 0; tx < 32; tx++)
      CHECK((g_rfg[ty * 32 + tx] == kGenCore) == (roundD2(huge, tx, ty) <= sq(2L * huge.radius)));
  roundMap(none, 12345);
  memcpy(base, g_rfg, sizeof(base));
  roundMap(neg, 12345);
  CHECK(memcmp(base, g_rfg, sizeof(base)) == 0);
  return 0;
}

// A radius too large for the grid (disc covers the whole 32 x 32 grid and more): the ring of tiles just outside the
// grid and every chunk beyond it stay air in both layers; inside the grid the disc formula still applies.
static int testRoundOversize() {
  const RoundSpec big = {1, 40};
  const int g = big.chunks * kChunkSize;
  CHECK(roundD2(big, -1, -1) <= sq(2 * big.radius) && roundD2(big, g, g) <= sq(2 * big.radius));  // disc covers the ring
  uint8_t f, w;
  roundTile(big, 0, 0, 12345, &f, &w);
  CHECK(f != 0);  // inside the grid the disc is still solid
  for (int t = -1; t <= g; t++) {
    const int pts[4][2] = {{t, -1}, {t, g}, {-1, t}, {g, t}};
    for (const auto& p : pts) {
      f = w = 0xAA;
      roundTile(big, p[0], p[1], 12345, &f, &w);
      CHECK(f == 0 && w == 0);
    }
  }
  uint8_t fg[kChunkTiles], wall[kChunkTiles];
  for (int cy = -1; cy <= 1; cy++)
    for (int cx = -1; cx <= 1; cx++) {
      if (cx == 0 && cy == 0) continue;
      memset(fg, 0xAA, sizeof(fg));
      memset(wall, 0x55, sizeof(wall));
      roundGen(big, fg, wall, cx, cy, 12345);
      for (int i = 0; i < kChunkTiles; i++) CHECK(fg[i] == 0 && wall[i] == 0);
    }
  return 0;
}

// Shape of the whole map of s: disc, grass ring (centre within one tile of the rim), dirt ring, wall layers, and the four axis rays (columns / rows G/2-1
// and G/2, so one doubled offset is +-1; index i = tiles from the edge, scanned to the centre).
static int checkRoundShape(const RoundSpec& s, uint32_t seed) {
  roundMap(s, seed);
  const int g = s.chunks * kChunkSize, r2 = 2 * s.radius;
  for (int ty = 0; ty < g; ty++)
    for (int tx = 0; tx < g; tx++) {
      const uint8_t f = g_rfg[ty * g + tx], w = g_rwall[ty * g + tx];
      const long d2 = roundD2(s, tx, ty);
      const bool inside = d2 <= sq(r2), grass = inside && d2 >= sq(r2 - 2);
      if (tx == 0 || ty == 0 || tx == g - 1 || ty == g - 1) CHECK(f == 0 && w == 0);
      CHECK((f != 0) == inside);
      CHECK((f == kGenGrass) == grass);
      if (!inside || grass) CHECK(w == 0);
      else CHECK(w == kGenWallDirt || w == kGenWallStone);
      if (inside && d2 >= sq(r2 - 4) && d2 < sq(r2 - 2)) CHECK(f == kGenDirt && w == kGenWallDirt);  // 3-4 doubled units below the rim
      const bool core = inside && s.core > 0 && d2 <= sq(2L * s.core);  // centre within core tiles of the grid centre
      CHECK((f == kGenCore) == core);
      if (core) CHECK(w == kGenWallStone);
      if (inside && !core && d2 < sq(r2 - 6)) CHECK(f >= kGenStone && f <= kGenDeepOre);  // 7 or more: stone or ore
    }
  // On a ray the centre tile has d2 = (G-2i-1)^2 + 1, so the first solid tile (grass) is at i = G/2 - R.
  const int e0 = g / 2 - s.radius;
  for (int off = g / 2 - 1; off <= g / 2; off++)
    for (int dir = 0; dir < 4; dir++)  // 0 top down, 1 left to right, 2 bottom up, 3 right to left
      for (int i = 0; i < g / 2; i++) {
        const int a = dir < 2 ? i : g - 1 - i;
        const int x = (dir & 1) ? a : off, y = (dir & 1) ? off : a;
        const uint8_t f = g_rfg[y * g + x], w = g_rwall[y * g + x];
        if (i < e0) {
          CHECK(f == 0 && w == 0);
        } else if (i == e0) {
          CHECK(f == kGenGrass && w == 0);
        } else {
          CHECK(w == (i <= e0 + 7 ? kGenWallDirt : kGenWallStone));  // depth 1..7 dirt wall, depth >= 8 stone wall
          if (i == e0 + 1) CHECK(f == kGenDirt);
          else if (i == e0 + 2) CHECK(f >= kGenDirt && f <= kGenDeepOre);  // 1 or 2 dirt tiles
          else {  // last index is a central tile; the ray tile i has d2 = (G - 2i - 1)^2 + 1
            CHECK((f == kGenCore) == (s.core > 0 && sq(g - 2L * i - 1) + 1 <= sq(2L * s.core)));
            CHECK(f == kGenCore || (f >= kGenStone && f <= kGenDeepOre));
          }
        }
      }
  return 0;
}

static int testRoundShape() {
  CHECK(kPlanetSpec.chunks * kChunkSize / 2 - kPlanetSpec.radius == 4);  // hand check: planet surface is tile 4 / 123
  const uint32_t seeds[] = {12345, 1};
  for (uint32_t seed : seeds)
    for (const RoundSpec& s : kRoundSpecs)
      if (checkRoundShape(s, seed)) return 1;
  return 0;
}

// Ore depth rules (spec): ore only at depth >= R/6, deep ore only at depth >= 2R/3, depth as in roundDepth, no slack. With
// wantRing the ore must also show up at its threshold depth: in the first ring (depth == minDepth) pooled over the seeds
// and within the first two rings (minDepth, minDepth + 1) at seed 12345.
static int checkOreDepth(const RoundSpec& s, uint8_t id, int minDepth, bool wantRing) {
  const int g = s.chunks * kChunkSize;
  const uint32_t seeds[] = {12345, 1, 2, 3};
  long ring1 = 0, ring12 = 0;
  for (uint32_t seed : seeds) {
    roundMap(s, seed);
    for (int ty = 0; ty < g; ty++)
      for (int tx = 0; tx < g; tx++)
        if (g_rfg[ty * g + tx] == id) {
          const int d = roundDepth(s, tx, ty);
          CHECK(d >= minDepth);
          ring1 += d == minDepth;
          if (seed == 12345) ring12 += d <= minDepth + 1;
        }
  }
  if (wantRing) CHECK(ring1 > 0 && ring12 > 0);
  return 0;
}

static int testRoundOre() {
  roundMap(kPlanetSpec, 12345);
  const uint32_t h = fnv(g_rwall, 128 * 128, fnv(g_rfg, 128 * 128));
  for (int k = 0; k < 3; k++) {  // ore from depth R/6, deep ore from depth 2R/3 (planet: 10 and 40)
    const RoundSpec& s = kRoundSpecs[k];
    roundMap(s, 12345);
    long o = 0, d = 0;  // every preset has both ids, so the depth checks below are not vacuous
    for (int i = 0; i < s.chunks * kChunkSize * s.chunks * kChunkSize; i++) { o += g_rfg[i] == kGenOre; d += g_rfg[i] == kGenDeepOre; }
    CHECK(o > 0 && d > 0);
    const bool wantRing = k != 1;  // the 1x1 asteroid is too small to demand a ring
    if (checkOreDepth(s, kGenOre, s.radius / 6, wantRing) || checkOreDepth(s, kGenDeepOre, 2 * s.radius / 3, wantRing)) return 1;
  }
  roundMap(kPlanetSpec, 2);
  CHECK(fnv(g_rwall, 128 * 128, fnv(g_rfg, 128 * 128)) != h);
  return 0;
}

// Ore at 64 seeds (1..64): the planet has ore and deep ore at every seed. Asteroids without ore are allowed by design (user
// decision 2026-11-23; measured over seeds 0..2000 with the 2-tile cores: the 2x2 asteroid lacks deep ore at 48 seeds, the 1x1
// asteroid lacks ore at 7 and deep ore at 489; without the cores it was 44, 6 and 417), so for them ore and deep ore only have to appear at some of the seeds.
static int testRoundOreSeeds() {
  int oreSeeds[3] = {0, 0, 0}, deepSeeds[3] = {0, 0, 0};
  for (uint32_t seed = 1; seed <= 64; seed++)
    for (int k = 0; k < 3; k++) {
      const RoundSpec& s = kRoundSpecs[k];
      roundMap(s, seed);
      long o = 0, d = 0;
      for (int i = 0; i < s.chunks * kChunkSize * s.chunks * kChunkSize; i++) { o += g_rfg[i] == kGenOre; d += g_rfg[i] == kGenDeepOre; }
      if (k == 0) { CHECK(o > 0); CHECK(d > 0); }
      oreSeeds[k] += o > 0;
      deepSeeds[k] += d > 0;
    }
  for (int k = 1; k < 3; k++) { CHECK(oreSeeds[k] > 0); CHECK(deepSeeds[k] > 0); }
  std::printf("test_worldgen: asteroids with ore / deep ore at 64 seeds: 1x1 %d / %d, 2x2 %d / %d\n", oreSeeds[1], deepSeeds[1], oreSeeds[2], deepSeeds[2]);
  return 0;
}

// Ore zone gate (spec): ore needs a smooth noise over (pseudo-angle, depth) to be high, so on the planet (a) ore and deep
// ore are clearly rarer than the hash-only rates among the eligible stone tiles (ore 1/8, deep ore 1/16; measured 4.6% and
// 2.3% over these seeds, 12.5% and 6.1% with the gate removed), (b) whole zones (32 angle steps x 8 tiles of depth, with
// at least 150 eligible tiles) have no ore at all (63 of 65 seeds; none without the gate), and (c) the zones wrap: the
// 8 angle steps before the wrap (248..255) and after it (0..7) are open or closed together, per depth band (11 of 130
// bands disagree; 61 of 130 when the zone noise does not wrap, as for independent zones).
static int testRoundOreZones() {
  const RoundSpec& s = kPlanetSpec;
  const int g = s.chunks * kChunkSize, mo = s.radius / 6, md = 2 * s.radius / 3, nSeeds = 65;  // seeds 12345 and 1..64
  long elig = 0, ore = 0, eligD = 0, deep = 0, wrapCmp = 0, wrapMis = 0;
  int emptySeeds = 0;
  for (int n = 0; n < nSeeds; n++) {
    roundMap(s, n ? (uint32_t)n : 12345u);
    long cel[8][8] = {}, cop[8][8] = {}, wl[8] = {}, wr[8] = {}, wle[8] = {}, wre[8] = {};
    for (int ty = 0; ty < g; ty++)
      for (int tx = 0; tx < g; tx++) {
        const uint8_t f = g_rfg[ty * g + tx];
        const int depth = roundDepth(s, tx, ty);
        if (f < kGenStone || f == kGenCore || depth < mo) continue;  // ore-eligible: stone or ore from depth R/6
        const int ang = roundAngle(2 * tx + 1 - g, 2 * ty + 1 - g), j = depth >> 3, open = f >= kGenOre;
        if (depth < md) { elig++; ore += f == kGenOre; }
        else { eligD++; deep += f == kGenDeepOre; }
        cel[ang >> 5][j]++;
        cop[ang >> 5][j] += open;
        if (ang >= 248) { wle[j]++; wl[j] += open; }
        else if (ang < 8) { wre[j]++; wr[j] += open; }
      }
    bool empty = false;
    for (int a = 0; a < 8; a++)
      for (int j = 0; j < 8; j++) empty = empty || (cel[a][j] >= 150 && cop[a][j] == 0);
    emptySeeds += empty;
    for (int j = 0; j < 8; j++)
      if (wle[j] >= 40 && wre[j] >= 40) { wrapCmp++; wrapMis += (wl[j] > 0) != (wr[j] > 0); }
  }
  CHECK(elig > 0 && eligD > 0);
  CHECK(ore * 100 < elig * 8 && ore * 100 > elig * 2);
  CHECK(deep * 1000 < eligD * 45 && deep * 100 > eligD * 1);
  CHECK(emptySeeds * 2 >= nSeeds);
  CHECK(wrapCmp >= 64 && wrapMis * 4 <= wrapCmp);
  return 0;
}

// Grass rule (spec): every solid tile with a 4-neighbour outside the disc (or outside the grid) is grass.
static int testRoundGrass() {
  const uint32_t seeds[] = {12345, 1, 0};
  for (uint32_t seed : seeds)
    for (const RoundSpec& s : kRoundSpecs) {
      roundMap(s, seed);
      const int g = s.chunks * kChunkSize;
      long exposed = 0;
      for (int ty = 0; ty < g; ty++)
        for (int tx = 0; tx < g; tx++) {
          if (roundDepth(s, tx, ty) < 0) continue;  // not solid
          bool edge = false;
          for (int k = 0; k < 4; k++) {
            const int nx = tx + (k == 0) - (k == 1), ny = ty + (k == 2) - (k == 3);
            edge = edge || nx < 0 || ny < 0 || nx >= g || ny >= g || roundDepth(s, nx, ny) < 0;
          }
          if (edge) { exposed++; CHECK(g_rfg[ty * g + tx] == kGenGrass); }
        }
      CHECK(exposed >= 2 * s.radius);  // not vacuous: the rim has a few tiles per radius step
    }
  return 0;
}

static int testRoundGravity() {
  const GravityDir Dn = GravityDir::Down, Lf = GravityDir::Left, Up = GravityDir::Up, Rt = GravityDir::Right;
  struct C { int x, y, cx, cy; GravityDir d; };
  static const C cases[] = {
      {0, -5, 0, 0, Dn}, {0, 5, 0, 0, Up}, {5, 0, 0, 0, Lf}, {-5, 0, 0, 0, Rt},
      {3, 2, 0, 0, Lf}, {3, -2, 0, 0, Lf}, {-3, 2, 0, 0, Rt}, {-3, -2, 0, 0, Rt},
      {2, 3, 0, 0, Up}, {-2, 3, 0, 0, Up}, {2, -3, 0, 0, Dn}, {-2, -3, 0, 0, Dn},
      {4, 4, 0, 0, Up}, {-4, 4, 0, 0, Up}, {4, -4, 0, 0, Dn}, {-4, -4, 0, 0, Dn},  // exact ties: vertical wins
      {0, 0, 0, 0, Dn},                                                              // centre itself
      {110, 200, 100, 200, Lf}, {90, 200, 100, 200, Rt}, {100, 190, 100, 200, Dn}, {100, 210, 100, 200, Up},
      {2147483647, 0, 0, 0, Lf}, {-2147483647 - 1, 0, 0, 0, Rt}, {0, 2147483647, 0, 0, Up}};  // extreme ints
  for (const C& c : cases) CHECK(roundGravity(c.x, c.y, c.cx, c.cy) == c.d);
  // Tile form, planet: doubled tile-centre offsets 2t+1-128, so tile 10 is -107 and tile 120 is +113 (not mirror
  // images); real ties need tx == ty or tx + ty == 127.
  struct T { int tx, ty; GravityDir d; };
  static const T tiles[] = {
      {64, 10, Dn}, {64, 120, Up}, {10, 64, Rt}, {120, 64, Lf},
      {10, 10, Dn}, {120, 120, Up}, {10, 117, Up}, {120, 7, Dn},  // exact diagonal ties
      {10, 120, Up}, {120, 10, Lf},                               // near the diagonal, larger axis wins
      {30, 29, Dn}, {29, 30, Rt}};                                 // either side of the top-left diagonal
  for (const T& t : tiles) CHECK(roundTileGravity(kPlanetSpec, t.tx, t.ty) == t.d);
  // Axis rays (columns / rows 63 and 64) face the centre, up to the last tile before the centre where |dx2| = |dy2|.
  for (int off = 63; off <= 64; off++)
    for (int i = 0; i < 63; i++) {
      CHECK(roundTileGravity(kPlanetSpec, off, i) == Dn && roundTileGravity(kPlanetSpec, off, 127 - i) == Up);
      CHECK(roundTileGravity(kPlanetSpec, i, off) == Rt && roundTileGravity(kPlanetSpec, 127 - i, off) == Lf);
    }
  return 0;
}

static int testRoundThroughWorld() {
  for (int t : {1, 2, 3, 4, 5}) g_props[t].flags = kTileSolid;
  Arena arena;
  arena.init(g_buf, sizeof(g_buf));
  WorldConfig c{};
  c.w_chunks = 4; c.h_chunks = 4; c.seed = 12345; c.gen = planetGen; c.props = g_props;
  c.cache_slots = 16; c.tile_px = 8;
  World w;
  CHECK(w.init(c, arena) == Ok);
  CHECK(w.ensureLoaded(Rect{0, 0, 1024, 1024}, 16) == 0);  // all 16 chunks fit the cache
  CHECK(w.get(63, 4, Layer::Fg) == kGenGrass && w.get(63, 3, Layer::Fg) == kTileAir);
  // A small body dropped from near the top of column 63 lands on the grass at tile row 4.
  Body b{63 * 8 + 1, 8, 6, 6};
  MoveResult r = moveAndCollide(w, b, 0, 100);
  CHECK(r.on_ground && r.hit_y && b.y + b.h == 4 * 8);
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
  if (testRoundDeterminism()) return 1;
  if (testRoundSeams()) return 1;
  if (testRoundOversize()) return 1;
  if (testRoundCore()) return 1;
  if (testRoundShape()) return 1;
  if (testRoundGrass()) return 1;
  if (testRoundOre()) return 1;
  if (testRoundOreSeeds()) return 1;
  if (testRoundOreZones()) return 1;
  if (testRoundGravity()) return 1;
  if (testRoundThroughWorld()) return 1;
  printTiming();
  std::printf("test_worldgen: all passed\n");
  return 0;
}
