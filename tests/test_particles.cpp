// Particle pool tests (S5.2): exhaustion, exact lifetime, motion vs an independent reference, clamps and
// saturation, camera-relative drawing into a band, palette fallback, draw leaves state alone.
// Particle state is observed through draw() only; the class has no accessors by design.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "pge/particles.h"

#define CHECK(c)                                                          \
  do {                                                                    \
    if (!(c)) {                                                           \
      std::printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); \
      return 1;                                                           \
    }                                                                     \
  } while (0)

using namespace pge;

static const int W = 64;
static Color g_buf[W * W];
static const Color kPal[8] = {0x1000, 0x1001, 0x1002, 0x1003, 0x1004, 0x1005, 0x1006, 0x1007};

// Draws into a W x W canvas and finds the pixel of palette entry `idx`. Returns how many such pixels exist.
static int where(const Particles& p, int idx, int cam_x, int cam_y, int& ox, int& oy) {
  std::memset(g_buf, 0, sizeof g_buf);
  Canvas c(g_buf, W, W, 0, W, 0);
  p.draw(c, cam_x, cam_y, kPal, 8);
  int n = 0;
  for (int y = 0; y < W; y++)
    for (int x = 0; x < W; x++)
      if (g_buf[y * W + x] == kPal[idx]) { n++; ox = x; oy = y; }
  return n;
}

static int litPixels() {
  int n = 0;
  for (int i = 0; i < W * W; i++) n += g_buf[i] != 0;
  return n;
}

static int testPool() {
  Particles p;
  const int N = PGE_PARTICLE_COUNT;
  CHECK(!p.spawn(1, 1, 0, 0, 0, 0) && p.size() == 0 && p.counters().full == 0 && p.counters().high == 0);  // life 0
  for (int i = 0; i < N; i++) CHECK(p.spawn(i, 0, 0, 0, 5, 0));
  CHECK(p.size() == N && p.counters().high == N && p.counters().full == 0);
  CHECK(!p.spawn(0, 0, 0, 0, 5, 0));
  CHECK(p.counters().full == 1 && p.counters().high == N && p.size() == N);
  CHECK(!p.spawn(0, 0, 0, 0, 0, 0) && p.counters().full == 1);  // refused for life 0 before the pool is asked
  for (int i = 0; i < 5; i++) p.update();
  CHECK(p.size() == 0);
  CHECK(p.spawn(0, 0, 0, 0, 1, 0) && p.size() == 1 && p.counters().high == N);  // freed slots come back

  // Lives exactly `life` update() calls.
  Particles q;
  int x, y;
  CHECK(q.spawn(10, 10, 0, 0, 3, 2));
  q.update();
  q.update();
  CHECK(q.size() == 1 && where(q, 2, 0, 0, x, y) == 1);
  q.update();
  CHECK(q.size() == 0 && where(q, 2, 0, 0, x, y) == 0);
  CHECK(q.spawn(10, 10, 0, 0, 255, 2));
  for (int i = 0; i < 254; i++) q.update();
  CHECK(q.size() == 1);
  q.update();
  CHECK(q.size() == 0);
  return 0;
}

struct Ref {
  int x, y, vx, vy, life, color;
};

static int testMotion() {
  for (int gp = 0; gp <= 3; gp += 3) {
    Particles p;
    Ref ref[] = {{10, 10, 2, 1, 20, 0}, {10, 40, -1, 3, 12, 1}, {50, 50, 0, -2, 25, 2},
                 {20, 5, 1, 120, 20, 3}, {40, 30, -2, 0, 7, 4}, {30, 30, 0, -125, 16, 5}};
    for (const Ref& r : ref) CHECK(p.spawn(r.x, r.y, r.vx, r.vy, (uint8_t)r.life, (uint8_t)r.color));
    for (int t = 0; t < 30; t++) {
      p.update((uint8_t)gp);
      int alive = 0;
      for (Ref& r : ref) {
        if (r.life > 0) {
          if (gp && r.life % gp == 0) r.vy = std::min(127, r.vy + 1);
          r.x += r.vx;
          r.y += r.vy;
          r.life--;
        }
        int x = -1, y = -1;
        const bool in = r.life > 0 && r.x >= 0 && r.x < W && r.y >= 0 && r.y < W;
        CHECK(where(p, r.color, 0, 0, x, y) == (in ? 1 : 0));
        if (in) CHECK(x == r.x && y == r.y);
        alive += r.life > 0;
      }
      CHECK(p.size() == alive);
    }
    CHECK(p.size() == 0);
  }
  return 0;
}

static int testClamps() {
  Particles p;
  int x, y;
  // vx, vy clamp to int8: seen after one update.
  CHECK(p.spawn(0, 0, 1000, 1000, 9, 1));
  CHECK(p.spawn(0, 0, -1000, -1000, 9, 2));
  p.update();
  CHECK(where(p, 1, 100, 100, x, y) == 1 && x == 27 && y == 27);
  CHECK(where(p, 2, -150, -150, x, y) == 1 && x == 22 && y == 22);

  // x, y clamp to int16 at spawn and saturate while moving.
  Particles s;
  CHECK(s.spawn(100000, -100000, 0, 0, 9, 3));
  CHECK(where(s, 3, 32767 - 10, -32768 - 10, x, y) == 1 && x == 10 && y == 10);
  CHECK(s.spawn(32760, -32760, 127, -127, 9, 4));
  s.update();
  CHECK(where(s, 4, 32767 - 10, -32768 - 10, x, y) == 1 && x == 10 && y == 10);
  s.update();
  CHECK(where(s, 4, 32767 - 10, -32768 - 10, x, y) == 1 && x == 10 && y == 10);

  // Gravity caps vy at 127: from 125 with period 1 it stops growing, so y advances 126 + 127 * k afterwards.
  Particles g;
  CHECK(g.spawn(0, 0, 0, 125, 20, 5));
  int yy = 0, vy = 125;
  for (int t = 0; t < 4; t++) {
    g.update(1);
    vy = std::min(127, vy + 1);
    yy += vy;
  }
  CHECK(yy == 126 + 127 * 3);
  CHECK(where(g, 5, 0, yy - 5, x, y) == 1 && y == 5 && x == 0);
  return 0;
}

static int testDraw() {
  Particles p;
  CHECK(p.spawn(6, 12, 0, 0, 2, 1) && p.spawn(5, 5, 0, 0, 2, 2));  // second one maps to screen row 2: off the band
  Color band[16 * 4];
  std::memset(band, 0, sizeof band);
  Canvas c(band, 16, 16, 8, 4, 0);
  p.draw(c, 1, 2, kPal, 8);
  int lit = 0;
  for (int i = 0; i < 16 * 4; i++) lit += band[i] != 0;
  CHECK(lit == 1 && band[(10 - 8) * 16 + 5] == kPal[1]);
  Color again[16 * 4];
  std::memset(again, 0, sizeof again);
  Canvas c2(again, 16, 16, 8, 4, 0);
  p.draw(c2, 1, 2, kPal, 8);
  p.draw(c2, 1, 2, kPal, 8);  // drawing twice changes nothing
  CHECK(std::memcmp(band, again, sizeof band) == 0 && p.size() == 2);
  p.update();
  CHECK(p.size() == 2);  // draw did not age the particles
  p.update();
  CHECK(p.size() == 0);

  // Palette handling: out-of-range color uses entry 0; null or empty palette draws nothing.
  Particles f;
  int x, y;
  CHECK(f.spawn(20, 21, 0, 0, 9, 5) && f.spawn(30, 31, 0, 0, 9, 200));
  std::memset(g_buf, 0, sizeof g_buf);
  Canvas cf(g_buf, W, W, 0, W, 0);
  f.draw(cf, 0, 0, kPal, 3);
  CHECK(litPixels() == 2 && g_buf[21 * W + 20] == kPal[0] && g_buf[31 * W + 30] == kPal[0]);
  std::memset(g_buf, 0, sizeof g_buf);
  f.draw(cf, 0, 0, kPal, 0);
  f.draw(cf, 0, 0, nullptr, 8);
  CHECK(litPixels() == 0);
  CHECK(where(f, 5, 0, 0, x, y) == 1 && x == 20 && y == 21);
  CHECK(where(f, 5, 10, 0, x, y) == 1 && x == 10 && y == 21);
  CHECK(where(f, 5, 21, 0, x, y) == 0);  // left of the screen
  return 0;
}

int main() {
  int rc = 0;
  rc |= testPool();
  rc |= testMotion();
  rc |= testClamps();
  rc |= testDraw();
  if (rc) return 1;
  std::printf("particles: all tests passed\n");
  return 0;
}
