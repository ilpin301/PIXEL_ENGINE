// Planet tests (null host backend, no Engine::run): spawn on the rim, a full autoplay lap with both character sizes
// (gravity order, camera orient, never inside rock), digging the 2 x 2 aim zone along gravity in all 4 quadrants and its place
// beside the body, the size toggle, the upright
// character in all 4 camera orients, the tilted character (frames pre-rotated along the radial axis), the rotating view
// (view vector, steady world, upright character a third from the screen top, snap on load), Up = jump / jetpack, dig mode (A held), the
// aim in the rotating view (on the character's screen axes), world <-> screen mapping and touch digging in both views, and
// save / load round trips incl. hostile game blobs, and the unbreakable core (dig and touch skip it, the bot walks around
// it in a dug cavity without a gravity flip-flop and never enters the 48 px hold radius).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "game.h"

#define CHECK(c)                                                          \
  do {                                                                    \
    if (!(c)) {                                                           \
      std::printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); \
      return 1;                                                           \
    }                                                                     \
  } while (0)

using pge::GravityDir;

static PlanetGame g_a, g_b;  // large: keep off the stack
alignas(8) static uint8_t g_log_buf[PlanetGame::kChangeCap * 5 + 64];
static pge::Arena g_log;

// Each game allocates its world from the init arena and the log arena; both are reused, so only one game lives at a time.
static pge::Error create(PlanetGame& g) {
  pge::engine().initArena().reset();
  g_log.init(g_log_buf, sizeof g_log_buf);
  g.log_arena = &g_log;
  return g.onCreate(pge::engine());
}

static void tick(PlanetGame& g, int n) {
  for (int i = 0; i < n; i++) g.onTick(pge::Input{});
}

static bool onGround(PlanetGame& g) { return (g.entities().meta(g.player())->flags & pge::kEntOnGround) != 0; }
static const pge::Body& body(PlanetGame& g) { return *g.entities().body(g.player()); }
static bool msgIs(const PlanetGame& g, const char* s) { return std::strcmp(g.message(), s) == 0; }

static bool overlapsRock(PlanetGame& g) {
  const pge::Body& b = body(g);
  for (int ty = b.y >> 3; ty <= (b.y + b.h - 1) >> 3; ty++)
    for (int tx = b.x >> 3; tx <= (b.x + b.w - 1) >> 3; tx++)
      if (g.world().tileFlags(tx, ty) & pge::kTileSolid) return true;
  return false;
}

// One tick with the given buttons held; edges are made by an empty tick before.
static pge::Input press(uint32_t buttons) {
  pge::Input in;
  pge_raw_input r = {};
  in.update(r);
  r.buttons = buttons;
  in.update(r);
  return in;
}

// One tick with the stick aimed up (dir -1) or down (+1) on the aim axis 3: aims without the Up / Down buttons, so no jump.
static pge::Input stickAim(int dir) {
  pge::Input in;
  pge_raw_input r = {};
  in.update(r);
  r.axis[3] = static_cast<int16_t>(dir * 20000);
  in.update(r);
  return in;
}

// A touch that starts on this tick at the screen pixel (an empty tick before makes the edge).
static pge::Input touchAt(int sx, int sy) {
  pge::Input in;
  pge_raw_input r = {};
  in.update(r);
  r.touch_down = 1;
  r.touch_x = static_cast<int16_t>(sx);
  r.touch_y = static_cast<int16_t>(sy);
  in.update(r);
  return in;
}

static int testSpawn() {
  CHECK(g_b.char_h == 32);  // the default character is 32 tall (g_b stays untouched until testSaveLoad)
  g_a.char_h = 24;
  CHECK(create(g_a) == pge::Ok);
  tick(g_a, 60);
  const pge::Body& b = body(g_a);
  CHECK(onGround(g_a) && g_a.gravity() == GravityDir::Down && g_a.orient() == 0 && !g_a.turning());
  CHECK(b.w == 16 && b.h == 24 && b.x + b.w / 2 == PlanetGame::kCentre);
  int row = 0;
  while (g_a.world().get(PlanetGame::kCentre / 8, row, pge::Layer::Fg) == pge::kTileAir) row++;
  CHECK(row > 0 && b.y + b.h == row * 8);  // the feet stand on the first solid tile of the centre column
  CHECK(g_a.switches() == 0 && g_a.world().changeCount() == 0);
  // char 32 on the top rim: the camera keeps the character in the centre column at row kCharY (unclamped) and 64 px of free space are above the head
  g_a.char_h = 32;
  CHECK(create(g_a) == pge::Ok);
  tick(g_a, 60);
  const pge::Rect sr = g_a.playerScreenRect();
  const pge::Body hb = body(g_a);
  CHECK(std::abs(sr.x + sr.w / 2 - PGE_LOGICAL_W / 2) <= 1 && std::abs(sr.y + sr.h / 2 - PlanetGame::kCharY) <= 1);
  CHECK(onGround(g_a) && hb.h == 32 && hb.y >= 64);
  for (int ty = 0; ty < (hb.y >> 3); ty++)
    for (int tx = hb.x >> 3; tx <= (hb.x + hb.w - 1) >> 3; tx++) CHECK(!(g_a.world().tileFlags(tx, ty) & pge::kTileSolid));
  return 0;
}

// Autoplay holds screen Right: one lap Down -> Left -> Up -> Right -> Down, back past the start column.
static int testLap(uint8_t ch) {
  g_a.char_h = ch;
  g_a.autoplay = true;
  CHECK(create(g_a) == pge::Ok);
  const GravityDir seq[5] = {GravityDir::Down, GravityDir::Left, GravityDir::Up, GravityDir::Right, GravityDir::Down};
  int idx = 0;
  uint32_t first = 0, lap = 0;
  for (uint32_t t = 1; t <= 30000 && !lap; t++) {
    g_a.onTick(pge::Input{});
    CHECK(g_a.orient() == pge::orientForGravity(g_a.gravity()));
    CHECK(!overlapsRock(g_a));
    if (g_a.gravity() != seq[idx]) {
      CHECK(idx < 4 && g_a.gravity() == seq[idx + 1]);
      if (++idx == 1) first = t;
    }
    if (idx == 4 && body(g_a).x + body(g_a).w / 2 >= PlanetGame::kCentre) lap = t;
  }
  CHECK(lap != 0 && g_a.switches() == 4 && g_a.gravity() == GravityDir::Down);
  CHECK(g_a.bench().n == g_a.ticks() - 60);
  std::printf("lap char %d: first switch at tick %u, lap complete at tick %u\n", ch, first, lap);
  g_a.autoplay = false;
  return 0;
}

// One B press digs the whole 2 x 2 zone: the aim point is half the body plus 8 px beyond the centre along gravity (Down held), the
// zone is the 2 x 2 tiles around the tile corner nearest to it, and all 4 breakable tiles of it go.
static int testDig() {
  g_a.char_h = 24;
  CHECK(create(g_a) == pge::Ok);
  const GravityDir sides[4] = {GravityDir::Down, GravityDir::Left, GravityDir::Up, GravityDir::Right};
  for (GravityDir s : sides) {
    g_a.teleportToSurface(s);
    for (int i = 0; i < 60 && !onGround(g_a); i++) tick(g_a, 1);
    CHECK(onGround(g_a) && g_a.gravity() == s && !g_a.turning());
    const pge::Body b = body(g_a);  // before the dig tick
    const int gx = s == GravityDir::Left ? -1 : s == GravityDir::Right ? 1 : 0;
    const int gy = s == GravityDir::Up ? -1 : s == GravityDir::Down ? 1 : 0;
    const int reach = g_a.charH() / 2 + 8;
    const int tx = ((b.x + b.w / 2 + gx * reach + 4) >> 3) - 1, ty = ((b.y + b.h / 2 + gy * reach + 4) >> 3) - 1;
    const uint32_t before = g_a.world().changeCount();
    for (int i = 0; i < 4; i++) {
      const uint8_t t = g_a.world().get(tx + (i & 1), ty + (i >> 1), pge::Layer::Fg);
      CHECK(t != pge::kTileAir && (g_a.world().props(t).flags & pge::kTileBreakable));
    }
    g_a.onTick(press(PGE_BTN_DOWN | PGE_BTN_B));
    CHECK(g_a.targetX() == tx && g_a.targetY() == ty);
    for (int i = 0; i < 4; i++) CHECK(g_a.world().get(tx + (i & 1), ty + (i >> 1), pge::Layer::Fg) == pge::kTileAir);
    CHECK(g_a.world().changeCount() == before + 4);
  }
  // Air and the world's edge are not breakable; the zone just dug is air, so digging it again does nothing.
  CHECK(!g_a.dig(PlanetGame::kCentre / 8, 0) && !g_a.dig(-1, -1));
  CHECK(!g_a.digZone() && !msgIs(g_a, "LOG FULL"));
  return 0;
}

// For front, up and down aim in each gravity direction the zone touches the body on one side (no gap, no overlap) and covers
// its extent on the other axis: the spawn poses are tile-aligned, and so is the body edge along gravity when it stands.
static int testAimZone() {
  const GravityDir sides[4] = {GravityDir::Down, GravityDir::Left, GravityDir::Up, GravityDir::Right};
  const uint8_t sizes[2] = {24, 32};
  const int aims[3] = {0, 1, 2};  // front, above (stick: the Up button jumps), below
  for (uint8_t ch : sizes) {
    g_a.char_h = ch;
    CHECK(create(g_a) == pge::Ok);
    for (GravityDir s : sides) {
      g_a.teleportToSurface(s);
      for (int i = 0; i < 60 && !onGround(g_a); i++) tick(g_a, 1);
      CHECK(onGround(g_a) && g_a.gravity() == s && !g_a.turning());
      for (int btn : aims) {
        const pge::Body b = body(g_a);
        g_a.onTick(btn == 1 ? stickAim(-1) : btn == 2 ? press(PGE_BTN_DOWN) : pge::Input{});
        const int zx = g_a.targetX() * 8, zy = g_a.targetY() * 8;
        const int gx = std::max(zx - (b.x + b.w), b.x - (zx + 16)), gy = std::max(zy - (b.y + b.h), b.y - (zy + 16));
        if (std::max(gx, gy) != 0 || std::min(gx, gy) >= 0) {
          std::printf("aim zone check failed: char %d gravity %d aim %d: gaps %d, %d\n", ch, static_cast<int>(s), btn, gx, gy);
          return 1;
        }
      }
    }
  }
  return 0;
}

static int testToggle() {
  g_a.char_h = 24;
  CHECK(create(g_a) == pge::Ok);
  tick(g_a, 60);
  pge::Body b = body(g_a);
  g_a.onTick(press(PGE_BTN_SELECT));
  CHECK(body(g_a).w == 16 && body(g_a).h == 32 && body(g_a).y + body(g_a).h == b.y + b.h && g_a.charH() == 32);
  tick(g_a, 5);
  CHECK(onGround(g_a));
  g_a.onTick(press(PGE_BTN_SELECT));
  CHECK(body(g_a).h == 24 && body(g_a).y + body(g_a).h == b.y + b.h && g_a.charH() == 24);
  // sideways gravity: 24 x 16 -> 32 x 16, the feet side stays (left edge for Left, right edge for Right)
  g_a.teleportToSurface(GravityDir::Left);
  b = body(g_a);
  g_a.onTick(press(PGE_BTN_SELECT));
  CHECK(body(g_a).w == 32 && body(g_a).h == 16 && body(g_a).x == b.x && body(g_a).y == b.y);
  g_a.teleportToSurface(GravityDir::Right);
  b = body(g_a);
  g_a.onTick(press(PGE_BTN_SELECT));
  CHECK(body(g_a).w == 24 && body(g_a).x + body(g_a).w == b.x + b.w);
  // no room above the head: refused
  g_a.teleportToSurface(GravityDir::Down);
  tick(g_a, 5);
  b = body(g_a);
  CHECK(g_a.world().set(PlanetGame::kCentre / 8, (b.y - 1) >> 3, pge::Layer::Fg, pge::kGenStone) == pge::Ok);
  g_a.onTick(press(PGE_BTN_SELECT));
  CHECK(body(g_a).h == b.h && g_a.charH() == 24 && msgIs(g_a, "NO ROOM"));
  return 0;
}

static pge::Color g_buf[PGE_LOGICAL_W * PGE_LOGICAL_H];

// Renders one full-screen band; the character's head colour must sit above its boot colour, around the screen centre
// column, with terrain (not space) just below the feet.
static int checkUpright(PlanetGame& g, int tol = 16) {
  pge::Canvas c(g_buf, PGE_LOGICAL_W, PGE_LOGICAL_H, 0, PGE_LOGICAL_H, 0);
  g.onRender(c);
  CHECK(c.dropped() == 0);
  int head_n = 0, boot_n = 0, head_max_y = -1, boot_min_y = PGE_LOGICAL_H, boot_max_y = -1, head_sx = 0, boot_sx = 0;
  for (int y = 0; y < PGE_LOGICAL_H; y++)
    for (int x = 0; x < PGE_LOGICAL_W; x++) {
      const pge::Color p = g_buf[y * PGE_LOGICAL_W + x];
      if (p == PlanetGame::kHeadColor) {
        head_n++;
        head_sx += x;
        if (y > head_max_y) head_max_y = y;
      } else if (p == PlanetGame::kBootColor) {
        boot_n++;
        boot_sx += x;
        if (y < boot_min_y) boot_min_y = y;
        if (y > boot_max_y) boot_max_y = y;
      }
    }
  CHECK(head_n > 0 && boot_n > 0 && head_max_y < boot_min_y);
  const int hx = head_sx / head_n, bx = boot_sx / boot_n;
  CHECK(hx > PGE_LOGICAL_W / 2 - tol && hx < PGE_LOGICAL_W / 2 + tol && bx > PGE_LOGICAL_W / 2 - tol && bx < PGE_LOGICAL_W / 2 + tol);
  const pge::Color below = g_buf[(boot_max_y + 2) * PGE_LOGICAL_W + bx];
  CHECK(below != pge::rgb(6, 6, 20) && below != 0);
  return 0;
}

static int testUpright() {
  const GravityDir sides[4] = {GravityDir::Down, GravityDir::Left, GravityDir::Up, GravityDir::Right};
  const uint8_t sizes[2] = {24, 32};
  for (uint8_t ch : sizes) {
    g_a.char_h = ch;
    CHECK(create(g_a) == pge::Ok);
    for (GravityDir s : sides) {
      g_a.teleportToSurface(s);
      tick(g_a, 10);
      CHECK(onGround(g_a) && g_a.orient() == pge::orientForGravity(s));
      if (checkUpright(g_a)) {
        std::printf("upright check failed for gravity %d, char %d\n", static_cast<int>(s), ch);
        return 1;
      }
    }
  }
  return 0;
}

// ---- tilted character (frames pre-rotated along the radial axis) ----
static pge::Color g_buf2[PGE_LOGICAL_W * PGE_LOGICAL_H];

static int sgn(int v) { return v > 0 ? 1 : v < 0 ? -1 : 0; }

// With tilt index 0 the tilted drawing is pixel for pixel the upright one (whole frame, facing right and facing left).
static int testTiltZero() {
  const uint8_t sizes[2] = {24, 32};
  for (uint8_t ch : sizes) {
    g_a.char_h = ch;
    CHECK(create(g_a) == pge::Ok);
    tick(g_a, 10);
    for (int left = 0; left < 2; left++) {
      if (left) g_a.onTick(press(PGE_BTN_LEFT));  // one step left: the character now faces left, still in the centre column
      CHECK(g_a.tiltIndex() == 0);
      pge::Canvas c1(g_buf, PGE_LOGICAL_W, PGE_LOGICAL_H, 0, PGE_LOGICAL_H, 0), c2(g_buf2, PGE_LOGICAL_W, PGE_LOGICAL_H, 0, PGE_LOGICAL_H, 0);
      g_a.tilt = true;
      g_a.onRender(c1);
      g_a.tilt = false;
      g_a.onRender(c2);
      g_a.tilt = true;
      CHECK(c1.dropped() == 0 && c2.dropped() == 0);
      CHECK(std::memcmp(g_buf, g_buf2, sizeof g_buf) == 0);
    }
  }
  return 0;
}

static int opaque(const uint8_t* p, int n) {
  int k = 0;
  for (int i = 0; i < n; i++) k += p[i] != 0;
  return k;
}

// Every tilt of both walk frames keeps the opaque pixel count of the original within 15 percent.
static int checkFrames(PlanetGame& g) {
  const pge::SpriteSheet &o = g.charSheet(), &ts = g.tiltSheet();
  const int h = g.charH(), s = PlanetGame::tiltSide(h);
  CHECK(ts.w == s && ts.h == s && ts.frames == PlanetGame::kTiltN * 2 && s * s * ts.frames <= PlanetGame::kTiltBytes);
  for (int f = 0; f < 2; f++) {
    const int ref = opaque(o.px + f * 16 * h, 16 * h);
    for (int i = 0; i < PlanetGame::kTiltN; i++) {
      const int n = opaque(ts.px + (i * 2 + f) * s * s, s * s);
      if (std::abs(n - ref) * 100 > 15 * ref) {
        std::printf("tilt %d frame %d of char %d: %d opaque pixels, original %d\n", i - PlanetGame::kTiltMax, f, h, n, ref);
        return 1;
      }
    }
  }
  return 0;
}

static int testTiltFrames() {
  const uint8_t sizes[2] = {24, 32};
  for (uint8_t ch : sizes) {
    g_a.char_h = ch;
    CHECK(create(g_a) == pge::Ok);
    if (checkFrames(g_a)) return 1;
  }
  g_a.char_h = 24;  // Select rebuilds the frames for the new height
  CHECK(create(g_a) == pge::Ok);
  tick(g_a, 5);
  g_a.onTick(press(PGE_BTN_SELECT));
  CHECK(g_a.charH() == 32 && g_a.tiltSheet().w == PlanetGame::tiltSide(32));
  return checkFrames(g_a);
}

// The character leans to "sign" (head centroid right / left of the boot centroid) and its boots stand at the body's feet midpoint.
static int checkLean(PlanetGame& g, int sign) {
  pge::Canvas c(g_buf, PGE_LOGICAL_W, PGE_LOGICAL_H, 0, PGE_LOGICAL_H, 0);
  g.onRender(c);
  int hn = 0, bn = 0, hx = 0, bx = 0, by = 0;
  for (int y = 0; y < PGE_LOGICAL_H; y++)
    for (int x = 0; x < PGE_LOGICAL_W; x++) {
      const pge::Color p = g_buf[y * PGE_LOGICAL_W + x];
      if (p == PlanetGame::kHeadColor) {
        hn++;
        hx += x;
      } else if (p == PlanetGame::kBootColor) {
        bn++;
        bx += x;
        by += y;
      }
    }
  CHECK(hn > 0 && bn > 0);
  const int dx = hx / hn - bx / bn;
  const pge::Rect r = g.playerScreenRect();
  if (sgn(dx) != sign || std::abs(dx) < 6 || std::abs(bx / bn - (r.x + r.w / 2)) > 6 || std::abs(by / bn - (r.y + r.h)) > 6) {
    std::printf("lean check failed: head - boot dx %d (want sign %d), boots at %d,%d, feet midpoint %d,%d\n", dx, sign, bx / bn,
                by / bn, r.x + r.w / 2, r.y + r.h);
    return 1;
  }
  return 0;
}

// One lap with the bot: tilt 0 mid-quadrant (spawn), one index per tick, a jump from +4 to -4 at each of the 4 gravity switches
// (the bot walks right = clockwise), and the character visibly leaning right when the tilt first reaches +4.
static int testTiltLap(uint8_t ch) {
  g_a.char_h = ch;
  g_a.autoplay = true;
  CHECK(create(g_a) == pge::Ok);
  int prev = g_a.tiltIndex(), arrivals = 0;
  CHECK(prev == 0);
  uint32_t sw = 0;
  for (int t = 1; t <= 30000 && sw < 4; t++) {
    g_a.onTick(pge::Input{});
    const int cur = g_a.tiltIndex();
    if (g_a.switches() != sw) {
      sw = g_a.switches();
      CHECK(prev == PlanetGame::kTiltMax && cur == -PlanetGame::kTiltMax);
    } else {
      CHECK(std::abs(cur - prev) <= 1);
    }
    if (cur == PlanetGame::kTiltMax && prev == PlanetGame::kTiltMax - 1) {
      CHECK(!g_a.turning() && checkLean(g_a, 1) == 0);
      arrivals++;
    }
    prev = cur;
  }
  CHECK(sw == 4 && arrivals == 4);
  g_a.autoplay = false;
  return 0;
}

// Walking left from the top the tilt runs to -4 and jumps to +4 at the switch; every quadrant middle has tilt 0.
static int testTiltLeft() {
  const GravityDir sides[4] = {GravityDir::Down, GravityDir::Left, GravityDir::Up, GravityDir::Right};
  const uint8_t sizes[2] = {24, 32};
  for (uint8_t ch : sizes) {
    g_a.char_h = ch;
    CHECK(create(g_a) == pge::Ok);
    for (GravityDir s : sides) {
      g_a.teleportToSurface(s);
      tick(g_a, 10);
      CHECK(onGround(g_a) && g_a.tiltIndex() == 0);
    }
  }
  CHECK(create(g_a) == pge::Ok);
  int prev = g_a.tiltIndex(), arrivals = 0;
  for (int t = 1; t <= 3000 && g_a.switches() == 0; t++) {
    g_a.onTick(press(PGE_BTN_LEFT));
    const int cur = g_a.tiltIndex();
    if (g_a.switches() != 0) {
      CHECK(prev == -PlanetGame::kTiltMax && cur == PlanetGame::kTiltMax);
    } else {
      CHECK(std::abs(cur - prev) <= 1);
    }
    if (cur == -PlanetGame::kTiltMax && prev == -PlanetGame::kTiltMax + 1) {
      CHECK(!g_a.turning() && checkLean(g_a, -1) == 0);
      arrivals++;
    }
    prev = cur;
  }
  CHECK(g_a.switches() == 1 && g_a.gravity() == GravityDir::Right && arrivals == 1);
  return 0;
}

// ---- rotating view (rotate = true) ----

// The Q14 vector of orient clockwise quarter turns (the same convention as Canvas::tilemapRotated).
static void quarter(int orient, int& c, int& s) {
  c = orient == 0 ? 16384 : orient == 2 ? -16384 : 0;
  s = orient == 1 ? 16384 : orient == 3 ? -16384 : 0;
}

// At the middle of each quadrant the planet centre is on the body's axis: the view vector is exactly the quarter turn of
// the camera orient (a mirrored or swapped vector would still draw a centred character), the upright character stands at
// the screen centre on terrain, and the frame is not empty.
static int testRotateAxes() {
  const GravityDir sides[4] = {GravityDir::Down, GravityDir::Left, GravityDir::Up, GravityDir::Right};
  const uint8_t sizes[2] = {24, 32};
  for (uint8_t ch : sizes) {
    g_a.char_h = ch;
    g_a.rotate = true;
    CHECK(create(g_a) == pge::Ok);
    for (GravityDir s : sides) {
      g_a.teleportToSurface(s);
      tick(g_a, 10);
      int c, sn;
      quarter(pge::orientForGravity(s), c, sn);
      CHECK(onGround(g_a) && g_a.gravity() == s && !g_a.turning() && g_a.viewCos() == c && g_a.viewSin() == sn);
      const pge::Rect r = g_a.charScreenRect();
      CHECK(r.w == 16 && r.h == ch && std::abs(r.x + r.w / 2 - PGE_LOGICAL_W / 2) <= 1);
      CHECK(std::abs(r.y + r.h / 2 - PlanetGame::kCharY) <= 1);
      // the tile window is centred on the view centre, which is the point under the screen centre: kCharY above it is the body
      const pge::Body& vb = body(g_a);
      CHECK(std::abs(std::hypot(g_a.viewX() - (vb.x + vb.w / 2), g_a.viewY() - (vb.y + vb.h / 2)) - (PGE_LOGICAL_H / 2 - PlanetGame::kCharY)) <= 1.5);
      if (checkUpright(g_a, 3)) {
        std::printf("rotate render check failed for gravity %d, char %d\n", static_cast<int>(s), ch);
        return 1;
      }
      int drawn = 0;  // checkUpright left the frame in g_buf
      for (pge::Color p : g_buf) drawn += p != pge::rgb(6, 6, 20);
      CHECK(drawn > PGE_LOGICAL_W * PGE_LOGICAL_H / 5);
    }
  }
  g_a.rotate = false;
  return 0;
}

// Angle in degrees between two Q14 view vectors (clamped: the vectors are not exactly 16384 long).
static double turnDeg(int c0, int s0, int c1, int s1) {
  const double d = (static_cast<double>(c0) * c1 + static_cast<double>(s0) * s1) /
                   (std::hypot(c0, s0) * std::hypot(c1, s1));
  return std::acos(std::max(-1.0, std::min(1.0, d))) * 180.0 / 3.14159265358979;
}

// One lap with the bot in the rotating view: the same 4 switches as the 4-way camera but with no iris or freeze, the
// character stays on the vertical centre line, the view angle turns less than 3 degrees per tick, and the world point under
// the screen centre moves at most 4 px per tick (the bot's body falls at up to 3.6 px per tick and the view centre is whole
// pixels; without the jump release an 8 px stepUp showed as a 6.4 px step).
static int testRotateLap(uint8_t ch) {
  g_a.char_h = ch;
  g_a.rotate = true;
  g_a.autoplay = true;
  CHECK(create(g_a) == pge::Ok);
  const GravityDir seq[5] = {GravityDir::Down, GravityDir::Left, GravityDir::Up, GravityDir::Right, GravityDir::Down};
  const double kCos3 = std::cos(3.0 * 3.14159265358979 / 180) * 16384.0 * 16384.0;
  int idx = 0, pc = g_a.viewCos(), ps = g_a.viewSin(), px = g_a.viewX(), py = g_a.viewY();
  uint32_t lap = 0;
  double worst = 0, worst_deg = 0;
  for (uint32_t t = 1; t <= 30000 && !lap; t++) {
    g_a.onTick(pge::Input{});
    CHECK(g_a.orient() == pge::orientForGravity(g_a.gravity()) && !g_a.turning() && !overlapsRock(g_a));
    if (g_a.gravity() != seq[idx]) {
      CHECK(idx < 4 && g_a.gravity() == seq[idx + 1]);
      idx++;
    }
    const pge::Rect r = g_a.charScreenRect();
    CHECK(std::abs(r.x + r.w / 2 - PGE_LOGICAL_W / 2) <= 1);
    const int c = g_a.viewCos(), s = g_a.viewSin();
    CHECK(static_cast<double>(c) * pc + static_cast<double>(s) * ps > kCos3);
    worst_deg = std::max(worst_deg, turnDeg(pc, ps, c, s));
    // where the previous tick's centre point is now: the offset to the new view centre, turned by the new vector
    const double dx = px - g_a.viewX(), dy = py - g_a.viewY();
    const double step = std::hypot((c * dx - s * dy) / 16384.0, (s * dx + c * dy) / 16384.0);
    if (step > worst) worst = step;
    CHECK(step <= 4.0);
    pc = c;
    ps = s;
    px = g_a.viewX();
    py = g_a.viewY();
    if (idx == 4 && body(g_a).x + body(g_a).w / 2 >= PlanetGame::kCentre) lap = t;
  }
  CHECK(lap != 0 && g_a.switches() == 4 && g_a.gravity() == GravityDir::Down);
  std::printf("rotate lap char %d: lap complete at tick %u, largest world step %.2f px, largest view turn %.2f deg "
              "per tick\n", ch, lap, worst, worst_deg);
  g_a.autoplay = false;
  g_a.rotate = false;
  return 0;
}

// ---- view angle near the planet centre ----

// Rock out of a round cavity of "radius" tiles around the planet centre (fg only; the unbreakable core stays).
static int digCavity(PlanetGame& g, int radius) {
  const int c = PlanetGame::kCentre / 8;
  for (int ty = c - radius; ty < c + radius; ty++)
    for (int tx = c - radius; tx < c + radius; tx++) {
      const int dx = 2 * (tx - c) + 1, dy = 2 * (ty - c) + 1;  // tile centre from the planet centre, doubled
      if (dx * dx + dy * dy >= 4 * radius * radius) continue;
      g.dig(tx, ty);
      const uint8_t f = g.world().get(tx, ty, pge::Layer::Fg);
      CHECK(f == pge::kTileAir || f == pge::kGenCore);
    }
  return 0;
}

struct Spin {
  double max_deg, max_step;
  int over3, over_cap, near, hold_run;  // ticks over 3 degrees / over 1.9 degrees / within 48 px of the centre
  uint32_t switches;
};

// The box (x, y) in gravity gd inside the cavity, then "ticks" ticks of a fixed pseudo-random script (right, left,
// jetpack, mixes; a new key set every 20 ticks). Per tick: the angle between consecutive view vectors and the world
// step of the point that was under the screen centre (the lap test's formula).
static int spinRun(PlanetGame& g, GravityDir gd, int x, int y, int ticks, uint32_t seed, Spin& o) {
  static const uint32_t keys[6] = {PGE_BTN_RIGHT, PGE_BTN_RIGHT | PGE_BTN_UP, PGE_BTN_LEFT,
                                   PGE_BTN_UP, 0, PGE_BTN_LEFT | PGE_BTN_UP};
  g.teleportTo(gd, x, y);
  o = Spin{};
  o.switches = g.switches();
  pge::Input in;
  pge_raw_input raw = {};
  uint32_t k = seed;
  int pc = g.viewCos(), ps = g.viewSin(), px = g.viewX(), py = g.viewY();
  for (int t = 0; t < ticks; t++) {
    if (t % 20 == 0) {
      k = k * 1664525u + 1013904223u;
      raw.buttons = keys[(k >> 24) % 6];
    }
    in.update(raw);
    g.onTick(in);
    CHECK(!overlapsRock(g));
    const int c = g.viewCos(), s = g.viewSin();
    const double a = turnDeg(pc, ps, c, s);
    o.max_deg = std::max(o.max_deg, a);
    o.over3 += a > 3.0;
    o.over_cap += a > 1.9;
    const pge::Body& b = body(g);
    o.near += std::hypot(b.x + b.w / 2 - PlanetGame::kCentre, b.y + b.h / 2 - PlanetGame::kCentre) < 48.0;
    const double dx = px - g.viewX(), dy = py - g.viewY();
    o.max_step = std::max(o.max_step, std::hypot((c * dx - s * dy) / 16384.0, (s * dx + c * dy) / 16384.0));
    pc = c;
    ps = s;
    px = g.viewX();
    py = g.viewY();
  }
  o.switches = g.switches() - o.switches;
  return 0;
}

// Digging the planet hollow and flying about near its centre must not make the view jerk: it turns at most 2 degrees
// per tick (walking on the surface turns about 0.2) and the view centre moves smoothly. Three scripted runs from the
// middle, a diagonal and the cavity wall; the printed numbers are the evidence of the fix.
static int testCentreCavity() {
  g_a.char_h = 32;
  g_a.rotate = true;
  g_a.core_tiles = 0;  // the hold mechanism itself: with a core the body cannot get that close (testCoreWalk)
  CHECK(create(g_a) == pge::Ok);
  CHECK(digCavity(g_a, 10) == 0);
  struct Start { GravityDir g; int x, y; uint32_t seed; };
  const Start starts[3] = {{GravityDir::Down, 504, 480, 1}, {GravityDir::Down, 544, 456, 2},
                           {GravityDir::Right, 432, 504, 3}};
  int total_cap = 0, total_near = 0;
  double worst = 0;
  for (const Start& st : starts) {
    Spin o;
    if (spinRun(g_a, st.g, st.x, st.y, 600, st.seed, o)) return 1;
    std::printf("cavity run %u: max %.1f deg per tick, %d ticks over 3 deg, %d ticks over 1.9 deg, %u gravity "
                "switches, "
                "%d ticks within 48 px, max world step %.1f px\n",
                st.seed, o.max_deg, o.over3, o.over_cap, o.switches, o.near, o.max_step);
    total_cap += o.over_cap;
    total_near += o.near;
    worst = std::max(worst, o.max_deg);
  }
  CHECK(worst <= 2.05);
  CHECK(total_cap > 0 && total_near > 100);  // the limit was reached, and the character did spend time near the centre
  g_a.rotate = false;
  g_a.core_tiles = PlanetGame::kCoreDefault;
  return 0;
}

// Inside 48 px of the planet centre the view angle stays where it was (it was set by the pose): the vector is bit for
// bit the same every tick while the character, left to gravity in the cavity, falls back and forth across the centre. A
// live angle would flip by 180 degrees at each crossing.
static int testViewHold() {
  g_a.char_h = 32;
  g_a.rotate = true;
  g_a.core_tiles = 0;
  CHECK(create(g_a) == pge::Ok);
  CHECK(digCavity(g_a, 10) == 0);
  g_a.teleportTo(GravityDir::Down, 516, 476);  // body centre (524, 492): 23 px from the planet centre, off every axis
  const int c0 = g_a.viewCos(), s0 = g_a.viewSin();
  CHECK(c0 > 0 && s0 < 0 && c0 < 16384);
  int behind = 0;
  for (int t = 0; t < 300; t++) {
    g_a.onTick(pge::Input{});
    const pge::Body& b = body(g_a);
    CHECK(std::hypot(b.x + b.w / 2 - PlanetGame::kCentre, b.y + b.h / 2 - PlanetGame::kCentre) < 48.0);
    CHECK(g_a.viewCos() == c0 && g_a.viewSin() == s0);
    // the true radial direction would be more than 90 degrees from the held one: the held up vector (-s0, -c0) against
    // the offset
    const int ox = b.x + b.w / 2 - PlanetGame::kCentre, oy = b.y + b.h / 2 - PlanetGame::kCentre;
    behind += static_cast<int64_t>(s0) * ox + static_cast<int64_t>(c0) * oy > 0;
  }
  std::printf("view hold: %d of 300 ticks with the character on the far side of the centre, view vector unchanged\n",
              behind);
  CHECK(behind > 10);
  g_a.rotate = false;
  g_a.core_tiles = PlanetGame::kCoreDefault;
  return 0;
}

// ---- unbreakable core ----

// The core tiles are solid and not breakable. A zone that covers both core and rock (tile columns e and e + 1 at the
// two centre rows, e = the last core column: tile centres within k tiles are at most 2k - 1 doubled units from the
// centre) digs only the rock, with B and with a touch, in both views; the dig of any single core tile is refused and
// logs nothing.
static int testCoreDig() {
  const int k = PlanetGame::kCoreDefault, c = PlanetGame::kCentre / 8, e = c + k - 1;
  g_a.char_h = 32;
  for (int rot = 0; rot < 2; rot++) {
    g_a.rotate = rot != 0;
    CHECK(create(g_a) == pge::Ok);
    const uint16_t fl = g_a.world().props(pge::kGenCore).flags;
    CHECK((fl & pge::kTileSolid) && !(fl & pge::kTileBreakable));
    int n = 0;
    for (int ty = c - k; ty < c + k; ty++)
      for (int tx = c - k; tx < c + k; tx++)
        if (g_a.world().get(tx, ty, pge::Layer::Fg) == pge::kGenCore) {
          CHECK(!g_a.dig(tx, ty) && g_a.world().get(tx, ty, pge::Layer::Fg) == pge::kGenCore);
          n++;
        }
    CHECK(n > 0 && g_a.world().changeCount() == 0);
    for (int ty = c - 1; ty <= c; ty++) {
      CHECK(g_a.world().get(e, ty, pge::Layer::Fg) == pge::kGenCore);
      CHECK(g_a.world().props(g_a.world().get(e + 1, ty, pge::Layer::Fg)).flags & pge::kTileBreakable);
    }
    for (int touch = 0; touch < 2; touch++) {
      if (rot && !touch) continue;  // the rotating view turns the aim zone with the view vector (testRotateAim)
      g_a.teleportTo(GravityDir::Down, 8 * (e + 1) - 28, 496);  // the aim zone in front of the body is columns e, e + 1
      if (touch) {
        int sx, sy;
        g_a.worldToScreen(8 * (e + 1), PlanetGame::kCentre, sx, sy);  // the corner between the four zone tiles
        g_a.onTick(touchAt(sx, sy));
      } else {
        CHECK(g_a.targetX() == e && g_a.targetY() == c - 1 && g_a.digZone());
      }
      for (int ty = c - 1; ty <= c; ty++) {
        CHECK(g_a.world().get(e, ty, pge::Layer::Fg) == pge::kGenCore);
        CHECK(g_a.world().get(e + 1, ty, pge::Layer::Fg) == pge::kTileAir);
      }
      CHECK(g_a.world().changeCount() == 2);
      CHECK(create(g_a) == pge::Ok);
    }
  }
  g_a.rotate = false;
  return 0;
}

// The bot walks around the core in a cavity of 14 tiles (the core stays): it never overlaps rock, the body centre never
// enters the 48 px hold radius, the rotating view turns at most 2 degrees per tick, and gravity never flips back to
// the direction it just left (a flip-flop at the core corner trapped the 32 px character at core radius 5) while the
// bot keeps going round (at least two laps = 8 clockwise switches in 1500 ticks).
static int testCoreWalk(uint8_t ch, bool rot) {
  g_a.char_h = ch;
  g_a.rotate = rot;
  g_a.autoplay = true;
  CHECK(create(g_a) == pge::Ok);
  CHECK(digCavity(g_a, 14) == 0);
  g_a.teleportTo(GravityDir::Down, 504, PlanetGame::kCentre - 8 * PlanetGame::kCoreDefault - ch);  // on top of the core
  int pc = g_a.viewCos(), ps = g_a.viewSin(), prev = static_cast<int>(g_a.gravity());
  int before = -1, last_switch = -100, fwd = 0;
  double min_d = 1e9, worst = 0;
  for (int t = 0; t < 1500; t++) {
    g_a.onTick(pge::Input{});
    CHECK(!overlapsRock(g_a));
    const pge::Body& b = body(g_a);
    min_d = std::min(min_d, std::hypot(b.x + b.w / 2 - PlanetGame::kCentre, b.y + b.h / 2 - PlanetGame::kCentre));
    const int gd = static_cast<int>(g_a.gravity());
    if (gd != prev) {
      CHECK(!(gd == before && t - last_switch <= 30));  // no flip-flop
      fwd += ((gd - prev) & 3) == 1 ? 1 : -1;
      before = prev;
      prev = gd;
      last_switch = t;
    }
    if (rot) {
      const int c = g_a.viewCos(), s = g_a.viewSin();
      worst = std::max(worst, turnDeg(pc, ps, c, s));
      pc = c;
      ps = s;
    }
  }
  CHECK(min_d >= 48.0 && worst <= 2.05 && fwd >= 8);
  std::printf("core walk char %d %s: %d clockwise gravity switches, body centre no closer than %.1f px to the centre, "
              "largest view turn %.2f deg per tick\n", ch, rot ? "rotate" : "4-way", fwd, min_d, worst);
  g_a.autoplay = false;
  g_a.rotate = false;
  return 0;
}

// ---- compass ----

// Renders a frame: the tip-coloured pixels (shaft, head and "U") seen from the dial centre lie along the screen
// direction (ex, ey), the tail-coloured ones (shaft and "D") the opposite way (centroid within about 25 degrees).
static int checkCompass(PlanetGame& g, int ex, int ey) {
  pge::Canvas c(g_buf, PGE_LOGICAL_W, PGE_LOGICAL_H, 0, PGE_LOGICAL_H, 0);
  g.onRender(c);
  CHECK(c.dropped() == 0);
  long n[2] = {}, sx[2] = {}, sy[2] = {};
  for (int y = 0; y < 48; y++)
    for (int x = PGE_LOGICAL_W - 48; x < PGE_LOGICAL_W; x++) {
      const pge::Color p = g_buf[y * PGE_LOGICAL_W + x];
      const int k = p == PlanetGame::kCompassTip ? 0 : p == PlanetGame::kCompassTail ? 1 : -1;
      if (k < 0) continue;
      n[k]++;
      sx[k] += x - PlanetGame::kCompassX;
      sy[k] += y - PlanetGame::kCompassY;
    }
  for (int k = 0; k < 2; k++) {
    CHECK(n[k] > 20);
    const double mx = static_cast<double>(sx[k]) / n[k], my = static_cast<double>(sy[k]) / n[k];
    const double len = std::hypot(mx, my);
    const double d = (mx * ex + my * ey) / (len * std::hypot(ex, ey)) * (k ? -1 : 1);
    if (len < 4 || d < 0.9) {
      std::printf("compass %s end at (%.1f, %.1f) from the dial centre, expected along (%d, %d)\n", k ? "tail" : "tip",
                  mx, my, k ? -ex : ex, k ? -ey : ey);
      return 1;
    }
  }
  return 0;
}

// The arrow points to the planet's top: in the 4-way view it turns in quarter turns with the camera (planet top up /
// down / right / left of the picture when the character stands on the top / bottom / left / right), in the rotating
// view it follows the view vector (walking right from the top: up and to the left).
static int testCompass() {
  const GravityDir sides[4] = {GravityDir::Down, GravityDir::Up, GravityDir::Right, GravityDir::Left};
  const int ex[4] = {0, 0, 1, -1}, ey[4] = {-1, 1, 0, 0};
  g_a.char_h = 32;
  g_a.rotate = false;
  CHECK(create(g_a) == pge::Ok);
  for (int i = 0; i < 4; i++) {
    g_a.teleportToSurface(sides[i]);
    tick(g_a, 10);
    CHECK(g_a.gravity() == sides[i] && !g_a.turning());
    if (checkCompass(g_a, ex[i], ey[i])) {
      std::printf("compass check failed for gravity %d in the 4-way view\n", static_cast<int>(sides[i]));
      return 1;
    }
  }
  g_a.rotate = true;
  CHECK(create(g_a) == pge::Ok);
  for (int t = 0; t < 900 && -g_a.viewSin() < 10000; t++) g_a.onTick(press(PGE_BTN_RIGHT));
  tick(g_a, 40);
  CHECK(g_a.viewSin() < -9000 && g_a.viewCos() > 0);
  if (checkCompass(g_a, g_a.viewSin(), -g_a.viewCos())) {
    std::printf("compass check failed in the rotating view (view %d, %d)\n", g_a.viewCos(), g_a.viewSin());
    return 1;
  }
  g_a.rotate = false;
  return 0;
}

// ---- Up = jump / jetpack, dig mode (A), screen <-> world mapping, touch digging ----

// Up jumps from the ground and flies (a bare jump rises about 20 px; the jetpack climbs well beyond that). A is dig mode now:
// held on the ground with no direction it does not jump, and the character reports digging().
static int testUpJumps() {
  g_a.char_h = 32;
  CHECK(create(g_a) == pge::Ok);
  tick(g_a, 40);
  CHECK(onGround(g_a));
  int start = body(g_a).y, top = start;
  for (int t = 0; t < 90; t++) {
    g_a.onTick(press(PGE_BTN_UP));
    top = std::min(top, body(g_a).y);
  }
  CHECK(top <= start - 60);
  CHECK(create(g_a) == pge::Ok);
  tick(g_a, 40);
  CHECK(onGround(g_a));
  start = body(g_a).y;
  for (int t = 0; t < 90; t++) g_a.onTick(press(PGE_BTN_A));
  CHECK(body(g_a).y == start && g_a.digging());
  std::printf("Up: jumped and flew %d px; A on the ground: no jump, dig mode on\n", start - top);
  return 0;
}

// One tick with A held and the stick at (ax, ay); an empty tick before gives the press edge.
static pge::Input digInput(int16_t ax, int16_t ay) {
  pge::Input in;
  pge_raw_input r = {};
  in.update(r);
  r.buttons = PGE_BTN_A;
  r.axis[0] = ax;
  r.axis[1] = ay;
  in.update(r);
  return in;
}

// Dig mode: A held = no gravity, the character follows the stick in any direction (the down-right diagonal here), digs the
// rock ahead (never inside rock, the change log grows), hovers with the stick released, walks again once A is released,
// and the unbreakable core stops a dive straight down.
static int testDigMode() {
  g_a.char_h = 32;
  CHECK(create(g_a) == pge::Ok);
  tick(g_a, 40);
  CHECK(onGround(g_a));
  const int x0 = body(g_a).x, y0 = body(g_a).y;
  const uint32_t count = g_a.world().changeCount();
  for (int t = 0; t < 120; t++) {
    g_a.onTick(digInput(20000, 20000));
    CHECK(!overlapsRock(g_a) && g_a.digging());
  }
  CHECK(body(g_a).x > x0 + 20 && body(g_a).y > y0 + 20 && g_a.world().changeCount() > count);
  std::printf("dig mode: moved diagonally %d, %d px and dug %u tiles\n", body(g_a).x - x0, body(g_a).y - y0,
              static_cast<unsigned>(g_a.world().changeCount() - count));
  const int x1 = body(g_a).x, y1 = body(g_a).y;
  for (int t = 0; t < 60; t++) g_a.onTick(digInput(0, 0));
  CHECK(body(g_a).x == x1 && body(g_a).y == y1);  // no gravity
  tick(g_a, 120);
  CHECK(!g_a.digging() && onGround(g_a));

  CHECK(create(g_a) == pge::Ok);  // a fresh planet: straight down from the rim into the core
  const int ys = body(g_a).y;
  for (int t = 0; t < 1500; t++) {
    g_a.onTick(digInput(0, 30000));
    CHECK(!overlapsRock(g_a));
  }
  const pge::Body& b = body(g_a);
  CHECK(b.y > ys + 100);
  CHECK(b.y + b.h <= PlanetGame::kCentre - g_a.core_tiles * 8 + 8);  // the core stops it (one tile of slack)
  std::printf("dig mode: dived %d px, stopped by the core at body bottom %d (core top %d)\n", b.y - ys, b.y + b.h,
              PlanetGame::kCentre - g_a.core_tiles * 8);
  return 0;
}

// World points (tile centres around the character) mapped to the screen and back land in the same tile; in the 4-way view
// in the same pixel. Points off the screen are skipped; "count" collects how many were checked.
static int checkRoundTrip(PlanetGame& g, int& count) {
  const pge::Body b = body(g);
  const int bx = b.x + b.w / 2, by = b.y + b.h / 2;
  for (int dy = -90; dy <= 90; dy += 30)
    for (int dx = -150; dx <= 150; dx += 30) {
      const int px = ((bx + dx) >> 3) * 8 + 4, py = ((by + dy) >> 3) * 8 + 4;
      int sx, sy, wx, wy;
      g.worldToScreen(px, py, sx, sy);
      if (sx < 0 || sx >= PGE_LOGICAL_W || sy < 0 || sy >= PGE_LOGICAL_H) continue;
      g.screenToWorld(sx, sy, wx, wy);
      CHECK((wx >> 3) == (px >> 3) && (wy >> 3) == (py >> 3));
      CHECK(g.rotate || (wx == px && wy == py));
      count++;
    }
  return 0;
}

// In both views at the middle of each quadrant: a world point 40 px against gravity from the body centre lands 40 px above
// the character on the screen (the direction is right, not only self-consistent), and the round trip holds; in flight too.
static int testScreenMap() {
  const GravityDir sides[4] = {GravityDir::Down, GravityDir::Left, GravityDir::Up, GravityDir::Right};
  int count = 0;
  for (int rot = 0; rot < 2; rot++) {
    g_a.char_h = 32;
    g_a.rotate = rot != 0;
    CHECK(create(g_a) == pge::Ok);
    for (GravityDir s : sides) {
      g_a.teleportToSurface(s);
      tick(g_a, 30);
      CHECK(onGround(g_a) && g_a.gravity() == s && !g_a.turning());
      const pge::Body b = body(g_a);
      const pge::Rect r = rot ? g_a.charScreenRect() : g_a.playerScreenRect();
      const int gx = s == GravityDir::Left ? -1 : s == GravityDir::Right ? 1 : 0;
      const int gy = s == GravityDir::Up ? -1 : s == GravityDir::Down ? 1 : 0;
      int sx, sy;
      g_a.worldToScreen(b.x + b.w / 2 - 40 * gx, b.y + b.h / 2 - 40 * gy, sx, sy);
      CHECK(std::abs(sx - (r.x + r.w / 2)) <= 1 && std::abs((r.y + r.h / 2) - sy - 40) <= 1);
      if (checkRoundTrip(g_a, count)) return 1;
    }
    for (int i = 0; i < 40; i++) g_a.onTick(press(PGE_BTN_UP));  // in flight the view centre still eases towards its rest
    CHECK(!onGround(g_a));
    if (checkRoundTrip(g_a, count)) return 1;
  }
  CHECK(count > 300);
  g_a.rotate = false;
  return 0;
}

// A touch at the screen position of the world point (wx, wy) digs the 2 x 2 zone around the tile corner nearest to it: the
// breakable tiles of the zone (counted first) go, nothing else changes. The touch is put exactly on that corner, so the 1 px
// rounding of the rotating view cannot move the zone.
static int checkTouchDig(PlanetGame& g, int wx, int wy) {
  const int cx = (wx + 4) >> 3, cy = (wy + 4) >> 3;  // the corner, in tiles
  int n = 0;
  for (int i = 0; i < 4; i++) {
    const uint8_t t = g.world().get(cx - 1 + (i & 1), cy - 1 + (i >> 1), pge::Layer::Fg);
    n += (g.world().props(t).flags & pge::kTileBreakable) != 0;
  }
  CHECK(n > 0);
  const uint32_t before = g.world().changeCount();
  int sx, sy;
  g.worldToScreen(cx * 8, cy * 8, sx, sy);
  g.onTick(touchAt(sx, sy));
  CHECK(g.targetX() == cx - 1 && g.targetY() == cy - 1);
  CHECK(g.world().changeCount() == before + static_cast<uint32_t>(n));
  for (int i = 0; i < 4; i++) {
    const uint8_t t = g.world().get(cx - 1 + (i & 1), cy - 1 + (i >> 1), pge::Layer::Fg);
    CHECK(!(g.world().props(t).flags & pge::kTileBreakable));
  }
  return 0;
}

// Touching the rock just below the character's feet digs that zone, in all 4 gravity directions, in both views.
static int testTouchDig() {
  const GravityDir sides[4] = {GravityDir::Down, GravityDir::Left, GravityDir::Up, GravityDir::Right};
  for (int rot = 0; rot < 2; rot++) {
    g_a.char_h = 32;
    g_a.rotate = rot != 0;
    CHECK(create(g_a) == pge::Ok);
    for (GravityDir s : sides) {
      g_a.teleportToSurface(s);
      tick(g_a, 30);
      CHECK(onGround(g_a) && g_a.gravity() == s && !g_a.turning());
      const pge::Body b = body(g_a);
      const int gx = s == GravityDir::Left ? -1 : s == GravityDir::Right ? 1 : 0;
      const int gy = s == GravityDir::Up ? -1 : s == GravityDir::Down ? 1 : 0;
      const int reach = g_a.charH() / 2 + 8;
      if (checkTouchDig(g_a, b.x + b.w / 2 + gx * reach, b.y + b.h / 2 + gy * reach)) return 1;
    }
  }
  g_a.rotate = false;
  return 0;
}

// The 4-way iris ignores touch like every other input, and touch works again once it is over.
static int testTouchIris() {
  g_a.char_h = 32;
  g_a.rotate = false;
  CHECK(create(g_a) == pge::Ok);
  for (int t = 0; t < 3000 && !g_a.turning(); t++) g_a.onTick(press(PGE_BTN_LEFT));
  CHECK(g_a.turning());
  int wx, wy;
  g_a.screenToWorld(160, 200, wx, wy);  // well below the character: rock
  CHECK(g_a.world().props(g_a.world().get(wx >> 3, wy >> 3, pge::Layer::Fg)).flags & pge::kTileBreakable);
  const uint32_t before = g_a.world().changeCount();
  g_a.onTick(touchAt(160, 200));
  CHECK(g_a.turning() && g_a.world().changeCount() == before);
  tick(g_a, 20);
  CHECK(!g_a.turning());
  g_a.onTick(touchAt(160, 200));
  CHECK(g_a.world().changeCount() > before);
  return 0;
}

// In the rotating view the aim sits on the character's screen axes: at a non-cardinal view angle (about 37 degrees, before the
// gravity switch) the aim-up zone is straight above the upright character, the aim-down zone straight below it (the 4-way
// frame put them 14 px to the side). Round trip and touch dig hold at that angle too.
static int testRotateAim() {
  g_a.char_h = 32;
  g_a.rotate = true;
  CHECK(create(g_a) == pge::Ok);
  for (int t = 0; t < 900 && -g_a.viewSin() < 10000; t++) g_a.onTick(press(PGE_BTN_RIGHT));  // walking right: sin < 0
  CHECK(-g_a.viewSin() >= 10000 && g_a.gravity() == GravityDir::Down);
  tick(g_a, 40);  // stand still: the body settles and the view comes to rest
  CHECK(onGround(g_a) && g_a.gravity() == GravityDir::Down && -g_a.viewSin() >= 9000);
  const int view_sin = g_a.viewSin();
  int count = 0;
  if (checkRoundTrip(g_a, count)) return 1;
  CHECK(count > 40);
  int dxs[2] = {};
  for (int dir = -1; dir <= 1; dir += 2) {
    g_a.onTick(stickAim(dir));
    const pge::Rect r = g_a.charScreenRect();
    int sx, sy;
    g_a.worldToScreen((g_a.targetX() + 1) * 8, (g_a.targetY() + 1) * 8, sx, sy);  // the zone's centre
    const int dx = sx - (r.x + r.w / 2), dy = sy - (r.y + r.h / 2);
    dxs[(dir + 1) / 2] = dx;
    CHECK(std::abs(dx) <= 7 && dir * dy >= r.h / 2);
  }
  std::printf("rotate aim at view sin %d: zone centre %d px (up) and %d px (down) off the vertical centre line\n", view_sin,
              dxs[0], dxs[1]);
  const pge::Body b = body(g_a);
  CHECK(checkTouchDig(g_a, b.x + b.w / 2, b.y + b.h / 2 + g_a.charH() / 2 + 8) == 0);
  g_a.rotate = false;
  return 0;
}

static const char* kSave = "test_planet_tmp.sav";
static const char* kMissing = "test_planet_missing.sav";

static void rmSave(const char* p) {
  char b[96];
  std::remove(p);
  std::snprintf(b, sizeof b, "%s.tmp", p);
  std::remove(b);
  std::snprintf(b, sizeof b, "%s.bak", p);
  std::remove(b);
}

static void putLE(uint8_t* o, int v) {
  for (int i = 0; i < 4; i++) o[i] = static_cast<uint8_t>(static_cast<uint32_t>(v) >> (8 * i));
}

static int testSaveLoad() {
  rmSave(kSave);
  g_a.char_h = 32;
  g_a.save_path = kSave;
  CHECK(create(g_a) == pge::Ok);
  g_a.teleportToSurface(GravityDir::Left);
  tick(g_a, 40);
  CHECK(onGround(g_a));
  const pge::Body b = body(g_a);
  const int tx = (b.x - 4) >> 3, ty = (b.y + b.h / 2) >> 3;
  CHECK(g_a.dig(tx, ty) && g_a.saveToStorage() && msgIs(g_a, "SAVED"));
  const uint32_t ticks = g_a.ticks();
  // a fresh game loads it (g_a is gone once g_b is created): the dug tile, pose, gravity and size come back
  g_b.save_path = kSave;
  g_b.load_on_start = true;
  CHECK(create(g_b) == pge::Ok);
  g_b.load_on_start = false;
  CHECK(msgIs(g_b, "LOADED") && g_b.world().get(tx, ty, pge::Layer::Fg) == pge::kTileAir);
  CHECK(g_b.gravity() == GravityDir::Left && g_b.orient() == pge::orientForGravity(GravityDir::Left) && !g_b.turning());
  CHECK(g_b.charH() == 32 && body(g_b).x == b.x && body(g_b).y == b.y && body(g_b).w == b.w && body(g_b).h == b.h);
  CHECK(g_b.world().changeCount() == 1 && g_b.ticks() == ticks);
  CHECK(g_b.world().get(tx, ty + 1, pge::Layer::Fg) != pge::kTileAir);  // only the dug tile changed
  tick(g_b, 20);
  CHECK(onGround(g_b) && !overlapsRock(g_b));

  // no save file: nothing changes
  g_b.save_path = kMissing;
  CHECK(!g_b.loadFromStorage() && msgIs(g_b, "NO SAVE") && g_b.world().changeCount() == 1);

  // hostile blobs in valid save files: rejected, the world's edits are dropped and the character respawns at the top
  g_b.save_path = kSave;
  g_b.char_h = 24;
  CHECK(create(g_b) == pge::Ok && g_b.charH() == 24);  // a re-created game starts at the configured size, not the loaded 32
  g_b.teleportToSurface(GravityDir::Down);
  const pge::Body sp = body(g_b);  // the spawn pose
  g_b.teleportToSurface(GravityDir::Left);
  const pge::Body lp = body(g_b);
  static uint8_t iobuf[512];
  pge::Storage st;
  st.setBuffer(iobuf, sizeof iobuf);
  CHECK(st.mount() == pge::Ok);
  typedef void (*Mut)(uint8_t*);
  struct Case { const char* name; Mut mut; uint32_t len; bool good; };
  const Case cases[] = {
      {"control", [](uint8_t*) {}, PlanetGame::kBlob, true},
      {"gravity 4", [](uint8_t* p) { p[9] = 4; }, PlanetGame::kBlob, false},
      {"gravity 7", [](uint8_t* p) { p[9] = 7; }, PlanetGame::kBlob, false},
      {"char 28", [](uint8_t* p) { p[10] = 28; }, PlanetGame::kBlob, false},
      {"version 2", [](uint8_t* p) { p[0] = 2; }, PlanetGame::kBlob, false},
      {"x outside", [](uint8_t* p) { putLE(p + 1, 1024 - 15); }, PlanetGame::kBlob, false},
      {"y negative", [](uint8_t* p) { putLE(p + 5, -1); }, PlanetGame::kBlob, false},
      {"inside rock", [](uint8_t* p) { putLE(p + 1, 504); putLE(p + 5, 504); }, PlanetGame::kBlob, false},
      {"too short", [](uint8_t*) {}, PlanetGame::kBlob - 1, false},
  };
  for (const Case& cs : cases) {
    g_b.teleportToSurface(GravityDir::Up);
    if (g_b.world().changeCount() == 0) CHECK(g_b.dig(tx, ty));
    uint8_t blob[PlanetGame::kBlob] = {1};
    putLE(blob + 1, lp.x);
    putLE(blob + 5, lp.y);
    blob[9] = static_cast<uint8_t>(GravityDir::Left);
    blob[10] = 24;
    cs.mut(blob);
    rmSave(kSave);
    CHECK(pge::saveGame(st, kSave, g_b.world(), 77, blob, cs.len) == pge::Ok);
    const bool ok = g_b.loadFromStorage();
    const pge::Body& nb = body(g_b);
    if (cs.good) {
      if (!ok || !msgIs(g_b, "LOADED") || g_b.gravity() != GravityDir::Left || nb.x != lp.x || nb.y != lp.y ||
          g_b.world().changeCount() != 1 || g_b.ticks() != 77 || g_b.turning()) {
        std::printf("good blob case '%s' not loaded\n", cs.name);
        return 1;
      }
    } else if (ok || !msgIs(g_b, "LOAD FAILED") || g_b.gravity() != GravityDir::Down || nb.x != sp.x || nb.y != sp.y ||
               g_b.world().changeCount() != 0 || g_b.world().get(tx, ty, pge::Layer::Fg) == pge::kTileAir) {
      std::printf("bad blob case '%s' not rejected / reset\n", cs.name);
      return 1;
    }
  }
  rmSave(kSave);
  return 0;
}

// A new pose recomputes the aim at once (no stale outline before the first tick), and a failed load that wiped the change
// log (a file that fails after the world was touched) puts rock back: a character standing in a dug hole must not stay in it.
static int testPoseAimAndFailedLoad() {
  rmSave(kSave);
  g_a.char_h = 24;
  g_a.rotate = false;
  g_a.save_path = kSave;
  CHECK(create(g_a) == pge::Ok);
  g_a.teleportToSurface(GravityDir::Left);
  tick(g_a, 40);
  g_a.teleportToSurface(GravityDir::Down);  // orient 0, facing right: the zone around the corner nearest to the point 16 px right
  CHECK(g_a.targetX() == ((body(g_a).x + 8 + 16 + 4) >> 3) - 1 && g_a.targetY() == ((body(g_a).y + body(g_a).h / 2 + 4) >> 3) - 1);

  g_a.teleportToSurface(GravityDir::Left);
  tick(g_a, 40);
  CHECK(onGround(g_a));
  const pge::Body b = body(g_a);
  for (int ty = b.y >> 3; ty <= (b.y + b.h - 1) >> 3; ty++) CHECK(g_a.dig((b.x - 4) >> 3, ty));
  tick(g_a, 20);  // the character drops into the hole
  CHECK(onGround(g_a) && body(g_a).x < b.x);
  // a valid file whose game blob is one byte too long: the log section is applied first, then the load fails
  static uint8_t iobuf[512];
  pge::Storage st;
  st.setBuffer(iobuf, sizeof iobuf);
  CHECK(st.mount() == pge::Ok);
  uint8_t blob[PlanetGame::kBlob + 1] = {1};
  rmSave(kSave);
  CHECK(pge::saveGame(st, kSave, g_a.world(), 77, blob, sizeof blob) == pge::Ok);
  CHECK(!g_a.loadFromStorage() && msgIs(g_a, "LOAD FAILED"));
  CHECK(g_a.world().changeCount() == 0 && !overlapsRock(g_a));  // the log is gone, so the hole is rock again
  rmSave(kSave);
  return 0;
}

// A load puts the view on the loaded pose at once: the saved pose is high above the rim on the far side of the planet, and
// an easing view would still show the character far from the screen centre.
static int testRotateSaveLoad() {
  rmSave(kSave);
  g_a.char_h = 24;
  g_a.rotate = true;
  g_a.save_path = kSave;
  CHECK(create(g_a) == pge::Ok);
  g_a.teleportToSurface(GravityDir::Left);
  tick(g_a, 5);
  for (int i = 0; i < 30; i++) g_a.onTick(press(PGE_BTN_UP));  // jetpack
  CHECK(!onGround(g_a) && g_a.saveToStorage() && msgIs(g_a, "SAVED"));
  const pge::Body hi = body(g_a);
  g_a.teleportToSurface(GravityDir::Down);
  tick(g_a, 5);
  CHECK(std::abs(g_a.viewCos() - 16384) <= 1 && g_a.viewSin() == 0);
  CHECK(g_a.loadFromStorage() && msgIs(g_a, "LOADED") && body(g_a).x == hi.x && body(g_a).y == hi.y);
  pge::Rect r = g_a.charScreenRect();  // before the next tick
  CHECK(g_a.viewCos() == 0 && g_a.viewSin() == -16384 && g_a.gravity() == GravityDir::Left);
  CHECK(std::abs(r.x + r.w / 2 - PGE_LOGICAL_W / 2) <= 1 && std::abs(r.y + r.h / 2 - PlanetGame::kCharY) <= 1);
  tick(g_a, 1);
  r = g_a.charScreenRect();
  CHECK(std::abs(r.y + r.h / 2 - PlanetGame::kCharY) <= 3);
  g_a.rotate = false;
  rmSave(kSave);
  return 0;
}

int main() {
  CHECK(pge::init() == pge::Ok);
  if (testSpawn()) return 1;
  if (testLap(24)) return 1;
  if (testLap(32)) return 1;
  if (testDig() || testAimZone()) return 1;
  if (testToggle()) return 1;
  if (testUpright()) return 1;
  if (testTiltZero() || testTiltFrames() || testTiltLap(24) || testTiltLap(32) || testTiltLeft()) return 1;
  std::printf("tilt buffer: %d bytes\n", PlanetGame::kTiltBytes);
  if (testRotateAxes() || testRotateLap(24) || testRotateLap(32) || testRotateSaveLoad()) return 1;
  if (testCentreCavity() || testViewHold() || testCompass()) return 1;
  if (testCoreDig() || testCoreWalk(24, true) || testCoreWalk(32, true)) return 1;
  if (testCoreWalk(24, false) || testCoreWalk(32, false)) return 1;
  if (testUpJumps() || testDigMode() || testScreenMap() || testTouchDig() || testTouchIris() || testRotateAim()) return 1;
  if (testSaveLoad() || testPoseAimAndFailedLoad()) { rmSave(kSave); return 1; }
  std::printf("test_planet: ok\n");
  return 0;
}
