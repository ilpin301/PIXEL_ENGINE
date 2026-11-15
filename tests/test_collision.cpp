// Collision tests: hand-built scenarios for tile sizes 8x8 and 10x12, then a randomized comparison of
// moveAndCollide against an independent pixel-by-pixel oracle.
#include <cstdint>
#include <cstdio>
#include "pge/collision.h"

#define CHECK(c)                                                          \
  do {                                                                    \
    if (!(c)) {                                                           \
      std::printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); \
      return 1;                                                           \
    }                                                                     \
  } while (0)

using namespace pge;

static TileProps g_props[256];  // 0 air, 1 solid, 2 one-way, 3 hazard

static void initProps() {
  g_props[1].flags = kTileSolid;
  g_props[2].flags = kTileOneWay;
  g_props[3].flags = kTileHazard;
}

// Builds ids from rows of '.', '#', '=', '^' into buf (row-major) and returns the grid.
static TileGrid makeGrid(uint8_t* buf, const char* const* rows, int w, int h, int tw, int th) {
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      const char c = rows[y][x];
      buf[y * w + x] = c == '#' ? 1 : c == '=' ? 2 : c == '^' ? 3 : 0;
    }
  return TileGrid{buf, (uint16_t)w, (uint16_t)h, (uint8_t)tw, (uint8_t)th, g_props};
}

static const char* const kMain[10] = {
    "............",  //
    "............",  //
    "..####......",  // ceiling cols 2-5
    "............",  //
    "............",  //
    "......==....",  // one-way cols 6-7
    "..........#.",  // wall col 10
    "..........#.",  //
    "..^^........",  // hazard cols 2-3
    "############",  // floor
};

static const char* const kAir[3] = {"....", "....", "...."};

static const char* const kBig[8] = {
    "..........",  //
    "..........",  //
    "..........",  //
    "..........",  //
    ".......#..",  //
    "..........",  //
    "..........",  //
    "..........",  //
};

static int testScenarios(int tw, int th) {
  uint8_t buf[120], abuf[12], bbuf[80];
  const TileGrid g = makeGrid(buf, kMain, 12, 10, tw, th);
  const TileGrid air = makeGrid(abuf, kAir, 4, 3, tw, th);
  const TileGrid big = makeGrid(bbuf, kBig, 10, 8, tw, th);
  MoveResult r;
  Body b;

  // free move
  b = Body{1, 1, 4, 4};
  r = moveAndCollide(g, b, 3, 2);
  CHECK(b.x == 4 && b.y == 3 && !r.hit_x && !r.hit_y && !r.on_ground && r.touched_flags == 0);

  // wall right / left: exact flush
  b = Body{7 * tw, 6 * th + 1, 4, 4};
  r = moveAndCollide(g, b, 100, 0);
  CHECK(b.x == 10 * tw - 4 && b.y == 6 * th + 1 && r.hit_x && !r.hit_y);
  b = Body{11 * tw + 1, 6 * th + 1, 4, 4};
  r = moveAndCollide(g, b, -100, 0);
  CHECK(b.x == 11 * tw && r.hit_x && !r.hit_y);

  // floor landing, ceiling
  b = Body{4 * tw + 1, 3 * th + 1, 4, 4};
  r = moveAndCollide(g, b, 0, 100);
  CHECK(b.y == 9 * th - 4 && r.hit_y && r.on_ground && !r.hit_x);
  b = Body{3 * tw + 1, 3 * th + 1, 4, 4};
  r = moveAndCollide(g, b, 0, -100);
  CHECK(b.y == 3 * th && r.hit_y && !r.on_ground);

  // one-way: lands from above and stands
  b = Body{6 * tw + 1, 3 * th, 4, 4};
  r = moveAndCollide(g, b, 0, 100);
  CHECK(b.y == 5 * th - 4 && r.hit_y && r.on_ground);
  r = moveAndCollide(g, b, 0, 0);
  CHECK(b.y == 5 * th - 4 && !r.hit_y && r.on_ground);
  r = moveAndCollide(g, b, 1, 0);
  CHECK(b.x == 6 * tw + 2 && !r.hit_x && r.on_ground);
  // passes upward through it
  b = Body{6 * tw + 1, 6 * th + 1, 4, 4};
  r = moveAndCollide(g, b, 0, -3 * th);
  CHECK(b.y == 3 * th + 1 && !r.hit_y);
  // not blocked horizontally while overlapping it
  b = Body{5 * tw + 1, 5 * th + 1, 4, 4};
  r = moveAndCollide(g, b, 2 * tw, 0);
  CHECK(b.x == 7 * tw + 1 && !r.hit_x && !r.on_ground);
  // falls through when the bottom is already below the tile top
  b = Body{6 * tw + 1, 5 * th + 2, 4, 4};
  r = moveAndCollide(g, b, 0, 2 * th);
  CHECK(b.y == 5 * th + 2 + 2 * th && !r.hit_y);
  r = moveAndCollide(g, b, 0, 100);
  CHECK(b.y == 9 * th - 4 && r.hit_y && r.on_ground);

  // hazard: reported, never blocks
  b = Body{2 * tw + 1, 8 * th + 1, 4, 4};
  r = moveAndCollide(g, b, 0, 0);
  CHECK(r.touched_flags == kTileHazard && !r.hit_x && !r.hit_y);
  r = moveAndCollide(g, b, 0, 100);
  CHECK(b.y == 9 * th - 4 && r.hit_y && r.on_ground && r.touched_flags == kTileHazard);
  r = moveAndCollide(g, b, 0, 0);
  CHECK((r.touched_flags & kTileSolid) == 0);

  // huge moves do not tunnel
  b = Body{8 * tw + 1, 6 * th + 1, 4, 4};
  r = moveAndCollide(g, b, 500, 0);
  CHECK(b.x == 10 * tw - 4 && r.hit_x);
  b = Body{4 * tw + 1, 3 * th + 1, 4, 4};
  r = moveAndCollide(g, b, 0, 500);
  CHECK(b.y == 9 * th - 4 && r.hit_y);
  b = Body{9 * tw + 1, 6 * th + 1, 4, 4};
  r = moveAndCollide(g, b, -500, 0);
  CHECK(b.x == 0 && r.hit_x);
  b = Body{8 * tw + 1, 6 * th + 1, 4, 4};
  r = moveAndCollide(g, b, 0, -500);
  CHECK(b.y == 0 && r.hit_y);

  // map borders (all-air grid 4x3)
  b = Body{tw, th, 4, 4};
  r = moveAndCollide(air, b, 1000, 0);
  CHECK(b.x == 4 * tw - 4 && r.hit_x);
  r = moveAndCollide(air, b, -1000, 0);
  CHECK(b.x == 0 && r.hit_x);
  r = moveAndCollide(air, b, 0, 1000);
  CHECK(b.y == 3 * th - 4 && r.hit_y && r.on_ground);
  r = moveAndCollide(air, b, 0, -1000);
  CHECK(b.y == 0 && r.hit_y && !r.on_ground);
  // negative coordinates (precondition violated: defined result, snapped back to the border)
  b = Body{-3, th, 4, 4};
  r = moveAndCollide(air, b, -1, 0);
  CHECK(b.x == 0 && r.hit_x);
  b = Body{tw, -3, 4, 4};
  r = moveAndCollide(air, b, 0, -1);
  CHECK(b.y == 0 && r.hit_y);

  // body larger than a tile
  const int bw = 2 * tw + 3, bh = 2 * th + 3;
  b = Body{0, 3 * th, bw, bh};
  r = moveAndCollide(big, b, 1000, 0);
  CHECK(b.x == 7 * tw - bw && r.hit_x && !r.hit_y);
  r = moveAndCollide(big, b, 0, 1000);
  CHECK(b.y == 8 * th - bh && r.hit_y && r.on_ground);

  // X resolves before Y on a diagonal into a corner
  b = Body{8 * tw + 1, 5 * th, 4, 4};
  r = moveAndCollide(g, b, 3 * tw, 6 * th);
  CHECK(b.x == 11 * tw + 1 && b.y == 9 * th - 4 && !r.hit_x && r.hit_y && r.on_ground);

  // standing still on the floor
  b = Body{4 * tw + 1, 9 * th - 4, 4, 4};
  r = moveAndCollide(g, b, 0, 0);
  CHECK(b.x == 4 * tw + 1 && b.y == 9 * th - 4 && !r.hit_x && !r.hit_y && r.on_ground);

  // tileFlags
  CHECK(tileFlags(g, -1, 0) == kTileSolid && tileFlags(g, 12, 0) == kTileSolid);
  CHECK(tileFlags(g, 0, -1) == kTileSolid && tileFlags(g, 0, 10) == kTileSolid);
  CHECK(tileFlags(g, 0, 9) == kTileSolid && tileFlags(g, 6, 5) == kTileOneWay);
  CHECK(tileFlags(g, 2, 8) == kTileHazard && tileFlags(g, 0, 0) == 0);
  return 0;
}

// ---- randomized comparison with a pixel-by-pixel oracle ----
static uint32_t g_rng = 12345u;
static int rnd(int n) {  // 0..n-1
  g_rng = g_rng * 1664525u + 1013904223u;
  return (int)((g_rng >> 16) % (uint32_t)n);
}

// Oracle helpers use pixel rectangle overlap only (no tile-index math).
static bool oBlocking(const TileGrid& g, int x, int y, int w, int h, bool down, int prev_bottom) {
  if (x < 0 || y < 0 || x + w > g.w * g.tile_w || y + h > g.h * g.tile_h) return true;  // void
  for (int ty = 0; ty < g.h; ty++)
    for (int tx = 0; tx < g.w; tx++) {
      const int x0 = tx * g.tile_w, y0 = ty * g.tile_h;
      if (!(x < x0 + g.tile_w && x + w > x0 && y < y0 + g.tile_h && y + h > y0)) continue;
      const uint16_t f = g.props[g.tiles[ty * g.w + tx]].flags;
      if (f & kTileSolid) return true;
      if (down && (f & kTileOneWay) && prev_bottom <= y0) return true;
    }
  return false;
}

static uint16_t oTouched(const TileGrid& g, int x, int y, int w, int h) {
  uint16_t t = 0;
  if (x < 0 || y < 0 || x + w > g.w * g.tile_w || y + h > g.h * g.tile_h) t |= kTileSolid;
  for (int ty = 0; ty < g.h; ty++)
    for (int tx = 0; tx < g.w; tx++) {
      const int x0 = tx * g.tile_w, y0 = ty * g.tile_h;
      if (x < x0 + g.tile_w && x + w > x0 && y < y0 + g.tile_h && y + h > y0)
        t |= g.props[g.tiles[ty * g.w + tx]].flags;
    }
  return t;
}

static int testRandom(int* cases_out) {
  const int kCases = 20000;
  uint8_t buf[120];
  int cases = 0, attempts = 0, bad = 0;
  while (cases < kCases && attempts < 10 * kCases) {
    attempts++;
    const bool wide = (attempts & 1) != 0;  // alternate 8x8 and 10x12 tiles
    TileGrid g{buf, 12, 10, (uint8_t)(wide ? 10 : 8), (uint8_t)(wide ? 12 : 8), g_props};
    for (int i = 0; i < 120; i++) {
      const int v = rnd(8);
      buf[i] = (uint8_t)(v < 5 ? 0 : v - 4);
    }
    Body b{0, 0, 1 + rnd(25), 1 + rnd(25)};
    const int pw = g.w * g.tile_w, ph = g.h * g.tile_h;
    bool placed = false;
    for (int t = 0; t < 50 && !placed; t++) {
      b.x = rnd(pw - b.w + 1);
      b.y = rnd(ph - b.h + 1);
      placed = !oBlocking(g, b.x, b.y, b.w, b.h, false, 0);
    }
    if (!placed) continue;
    const int dx = rnd(81) - 40, dy = rnd(81) - 40;

    // oracle
    int ox = b.x, oy = b.y;
    bool hx = false, hy = false;
    for (int i = 0, s = dx < 0 ? -1 : 1; i < (dx < 0 ? -dx : dx); i++) {
      if (oBlocking(g, ox + s, oy, b.w, b.h, false, 0)) { hx = true; break; }
      ox += s;
    }
    for (int i = 0, s = dy < 0 ? -1 : 1; i < (dy < 0 ? -dy : dy); i++) {
      if (oBlocking(g, ox, oy + s, b.w, b.h, s > 0, oy + b.h)) { hy = true; break; }
      oy += s;
    }
    const bool og = oBlocking(g, ox, oy + 1, b.w, b.h, true, oy + b.h);
    const uint16_t ot = oTouched(g, ox, oy, b.w, b.h);

    const Body start = b;
    const MoveResult r = moveAndCollide(g, b, dx, dy);
    cases++;
    if (b.x != ox || b.y != oy || r.hit_x != hx || r.hit_y != hy || r.on_ground != og || r.touched_flags != ot) {
      if (bad++ < 5)
        std::printf("MISMATCH tile %dx%d box(%d,%d %dx%d) d(%d,%d): got (%d,%d) hx%d hy%d g%d t%d, want (%d,%d) hx%d hy%d g%d t%d\n",
                    g.tile_w, g.tile_h, start.x, start.y, start.w, start.h, dx, dy, b.x, b.y, r.hit_x, r.hit_y,
                    r.on_ground, r.touched_flags, ox, oy, hx, hy, og, ot);
    }
  }
  *cases_out = cases;
  return (bad != 0 || cases < kCases) ? 1 : 0;
}

int main() {
  initProps();
  if (testScenarios(8, 8)) return 1;
  if (testScenarios(10, 12)) return 1;
  int cases = 0;
  if (testRandom(&cases)) {
    std::printf("collision: random comparison FAILED (%d cases)\n", cases);
    return 1;
  }
  std::printf("collision: scenarios 8x8 + 10x12 OK, %d random cases match the oracle\n", cases);
  return 0;
}
