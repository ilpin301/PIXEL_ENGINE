// Platformer tests (null host backend, no Engine::run): 4000 autoplay ticks with independent invariants, then
// hand-fed input (Right held, then A) on a fresh instance.
#include <cstdio>
#include "game.h"

#define CHECK(c)                                                          \
  do {                                                                    \
    if (!(c)) {                                                           \
      std::printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); \
      return 1;                                                           \
    }                                                                     \
  } while (0)

static PlatformerGame g_auto, g_manual;  // large: keep off the stack

// Independent of the game's collision code: any solid tile under the body box?
static bool overlapsSolid(const PlatformerGame& g) {
  const pge::Body& b = g.body();
  const pge::TileGrid& t = g.grid();
  for (int ty = b.y / t.tile_h; ty <= (b.y + b.h - 1) / t.tile_h; ty++)
    for (int tx = b.x / t.tile_w; tx <= (b.x + b.w - 1) / t.tile_w; tx++)
      if (t.props[t.tiles[ty * t.w + tx]].flags & pge::kTileSolid) return true;
  return false;
}

static int testAutoplay() {
  g_auto.autoplay = true;
  CHECK(g_auto.onCreate(pge::engine()) == pge::Ok);
  const pge::TileGrid& t = g_auto.grid();
  const int world_w = t.w * t.tile_w, world_h = t.h * t.tile_h;
  const int spawn_x = g_auto.body().x;
  int max_x = spawn_x, max_cam = 0;
  for (int i = 0; i < 4000; i++) {
    g_auto.onTick(pge::Input{});
    const pge::Body& b = g_auto.body();
    CHECK(b.x >= 0 && b.y >= 0 && b.x + b.w <= world_w && b.y + b.h <= world_h);
    CHECK(!overlapsSolid(g_auto));
    CHECK(g_auto.cameraX() >= 0 && g_auto.cameraX() <= world_w - PGE_LOGICAL_W);
    if (b.x > max_x) max_x = b.x;
    if (g_auto.cameraX() > max_cam) max_cam = g_auto.cameraX();
  }
  std::printf("autoplay: ticks=%u max_x_progress=%d max_cam=%d deaths=%u\n", (unsigned)g_auto.ticks(), max_x - spawn_x,
              max_cam, (unsigned)g_auto.deaths());
  CHECK(g_auto.ticks() == 4000);
  CHECK(max_x - spawn_x >= 600);
  CHECK(max_cam >= 600);
  CHECK(g_auto.deaths() < 40);
  CHECK(g_auto.bench().n == 4000 - 60);
  return 0;
}

static int testManual() {
  CHECK(g_manual.onCreate(pge::engine()) == pge::Ok);
  pge::Input in;
  pge_raw_input raw = {};
  const int x0 = g_manual.body().x;
  raw.buttons = PGE_BTN_RIGHT;
  for (int i = 0; i < 120; i++) { in.update(raw, 1); g_manual.onTick(in); }
  CHECK(g_manual.body().x > x0 + 50);
  raw.buttons = 0;  // settle on the ground, then press A
  for (int i = 0; i < 30; i++) { in.update(raw, 1); g_manual.onTick(in); }
  const int y0 = g_manual.body().y;
  raw.buttons = PGE_BTN_A;
  int min_y = y0;
  for (int i = 0; i < 30; i++) {
    in.update(raw, 1);
    g_manual.onTick(in);
    if (g_manual.body().y < min_y) min_y = g_manual.body().y;
  }
  std::printf("manual: x %d -> %d, jump rise %d px, deaths=%u\n", x0, g_manual.body().x, y0 - min_y,
              (unsigned)g_manual.deaths());
  CHECK(y0 - min_y >= 30);
  CHECK(g_manual.deaths() == 0);
  return 0;
}

int main() {
  CHECK(pge::init() == pge::Ok);
  if (testAutoplay() || testManual()) return 1;
  std::printf("platformer: ok\n");
  return 0;
}
