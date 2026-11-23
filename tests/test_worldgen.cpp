// Procedural terrain tests (S5.1b): determinism (golden checksums, garbage-prefilled buffers), seams (chunk bytes
// equal a per-tile world-coordinate reference), terrain shape, caves and ores, seed sensitivity, and use through
// World (real terrain, collision on the surface, edits survive eviction and reload). Goldens were captured from the
// implementation (seed 12345) and checked by eye on an ASCII dump, not derived independently.
// S8.5 round tests (testRound*): expectations follow the disc spec (formulas in the tests); only the chunk goldens are captured.
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

static int testRoundDeterminism() {
  // Golden checksums captured from the implementation (seed 12345), not derived independently.
  CHECK(roundChunkHash(kPlanetSpec, 0, 0, 12345) == 3499892850u);
  CHECK(roundChunkHash(kPlanetSpec, 1, 1, 12345) == 3415786110u);
  CHECK(roundChunkHash(kPlanetSpec, 2, 1, 12345) == 2910136831u);
  CHECK(roundChunkHash(kPlanetSpec, 3, 3, 12345) == 677150627u);
  CHECK(roundChunkHash(kAsteroid1Spec, 0, 0, 12345) == 1691173347u);
  CHECK(roundChunkHash(kAsteroid2Spec, 1, 0, 12345) == 68697569u);
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

// Shape of the whole map of s: disc, grass ring, dirt ring, wall layers, and the four axis rays (columns / rows G/2-1
// and G/2, so one doubled offset is +-1; index i = tiles from the edge, scanned to the centre).
static int checkRoundShape(const RoundSpec& s, uint32_t seed) {
  roundMap(s, seed);
  const int g = s.chunks * kChunkSize, r2 = 2 * s.radius;
  for (int ty = 0; ty < g; ty++)
    for (int tx = 0; tx < g; tx++) {
      const uint8_t f = g_rfg[ty * g + tx], w = g_rwall[ty * g + tx];
      const long d2 = roundD2(s, tx, ty);
      const bool inside = d2 <= sq(r2), grass = inside && d2 >= sq(r2 - 1);
      if (tx == 0 || ty == 0 || tx == g - 1 || ty == g - 1) CHECK(f == 0 && w == 0);
      CHECK((f != 0) == inside);
      CHECK((f == kGenGrass) == grass);
      if (!inside || grass) CHECK(w == 0);
      else CHECK(w == kGenWallDirt || w == kGenWallStone);
      if (inside && d2 >= sq(r2 - 3) && d2 < sq(r2 - 1)) CHECK(f == kGenDirt && w == kGenWallDirt);  // depth 1
      if (inside && d2 < sq(r2 - 5)) CHECK(f >= kGenStone && f <= kGenDeepOre);                         // depth >= 3
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
          else CHECK(f >= kGenStone && f <= kGenDeepOre);                  // last index is a central tile
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

// Every tile with fg == id must satisfy depth >= minDepth, i.e. d2 <= (2R - 2*minDepth)^2, plus one tile ring of
// slack (+2 doubled units) in case the implementation rounds depth differently.
static int checkOreDepth(const RoundSpec& s, uint8_t id, int minDepth) {
  const int g = s.chunks * kChunkSize;
  for (int ty = 0; ty < g; ty++)
    for (int tx = 0; tx < g; tx++)
      if (g_rfg[ty * g + tx] == id) CHECK(roundD2(s, tx, ty) <= sq(2 * s.radius - 2 * minDepth + 2));
  return 0;
}

static int testRoundOre() {
  roundMap(kPlanetSpec, 12345);
  long ore = 0, deep = 0, quad[4] = {0, 0, 0, 0};
  for (int ty = 0; ty < 128; ty++)
    for (int tx = 0; tx < 128; tx++) {
      const uint8_t f = g_rfg[ty * 128 + tx];
      ore += f == kGenOre;
      deep += f == kGenDeepOre;
      if (f == kGenOre) quad[static_cast<int>(roundTileGravity(kPlanetSpec, tx, ty))]++;
    }
  CHECK(ore > 0 && deep > 0);
  CHECK(!(quad[0] == quad[1] && quad[1] == quad[2] && quad[2] == quad[3]));  // ore zones depend on direction
  const uint32_t h = fnv(g_rwall, 128 * 128, fnv(g_rfg, 128 * 128));
  for (const RoundSpec& s : kRoundSpecs) {  // ore from depth R/6, deep ore from depth 2R/3 (planet: 10 and 40)
    roundMap(s, 12345);
    long o = 0, d = 0;  // every preset has both ids, so the depth checks below are not vacuous
    for (int i = 0; i < s.chunks * kChunkSize * s.chunks * kChunkSize; i++) { o += g_rfg[i] == kGenOre; d += g_rfg[i] == kGenDeepOre; }
    CHECK(o > 0 && d > 0);
    if (checkOreDepth(s, kGenOre, s.radius / 6) || checkOreDepth(s, kGenDeepOre, 2 * s.radius / 3)) return 1;
  }
  roundMap(kPlanetSpec, 2);
  CHECK(fnv(g_rwall, 128 * 128, fnv(g_rfg, 128 * 128)) != h);
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
  if (testRoundShape()) return 1;
  if (testRoundOre()) return 1;
  if (testRoundGravity()) return 1;
  if (testRoundThroughWorld()) return 1;
  printTiming();
  std::printf("test_worldgen: all passed\n");
  return 0;
}
