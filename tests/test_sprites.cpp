// Sprite tests: goldens for flips and 90-degree rotations, an independent reference model compared with full
// and band rendering at many clipped positions, atlas sheets, animation helper (own mock display HAL).
#include <cstdio>
#include <cstring>
#include "pge/gfx.h"
#include "pge/hal/display.h"
#include "pge/sprite.h"

#define CHECK(c)                                                          \
  do {                                                                    \
    if (!(c)) {                                                           \
      std::printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); \
      return 1;                                                           \
    }                                                                     \
  } while (0)

using namespace pge;

// ---- mock display HAL: a plane of g_pw x g_ph pixels that flushed rects are copied into ----
enum { MAXPX = 32 * 32 };
static Color g_fb[MAXPX];
static int g_pw, g_ph;

static void mockReset(int pw, int ph) {
  std::memset(g_fb, 0xFF, sizeof g_fb);
  g_pw = pw;
  g_ph = ph;
}

extern "C" {
pge_error pge_hal_display_init(uint16_t, uint16_t, pge_display_info*) { return 0; }
pge_error pge_hal_display_flush(pge_rect r, uint16_t* px) {
  if (r.x + r.w > g_pw || r.y + r.h > g_ph) return -2;
  for (int y = 0; y < r.h; y++) std::memcpy(&g_fb[(r.y + y) * g_pw + r.x], px + y * r.w, r.w * sizeof(Color));
  return 0;
}
pge_error pge_hal_display_wait(void) { return 0; }
void pge_hal_display_frame_begin(void) {}
void pge_hal_display_frame_end(void) {}
}

static const Color kClear = 0x1234;

// ---- 1. goldens: 4x3 asymmetric sprite on an 8x6 canvas ----
//   ###.
//   #...
//   #o..
static const Color kPal[4] = {0x7777, 1, 2, 3};
static const uint8_t kS[12] = {1, 1, 1, 0, 1, 0, 0, 0, 1, 2, 0, 0};
static const SpriteSheet kSheet = {kS, kPal, 4, 3, 1, true};

enum { AW = 8, AH = 6 };
struct Golden { const char* name; SpriteFlags f; bool transparent; int x, y; const char* art; };

static const Golden kGoldens[] = {
    {"none", None, true, 2, 1,
     "........\n"
     "..###...\n"
     "..#.....\n"
     "..#o....\n"
     "........\n"
     "........\n"},
    {"opaque", None, false, 2, 1,
     "........\n"
     "..###+..\n"
     "..#+++..\n"
     "..#o++..\n"
     "........\n"
     "........\n"},
    {"flipX", FlipX, true, 2, 1,
     "........\n"
     "...###..\n"
     ".....#..\n"
     "....o#..\n"
     "........\n"
     "........\n"},
    {"flipY", FlipY, true, 2, 1,
     "........\n"
     "..#o....\n"
     "..#.....\n"
     "..###...\n"
     "........\n"
     "........\n"},
    {"rot90", Rot90, true, 2, 1,
     "........\n"
     "..###...\n"
     "..o.#...\n"
     "....#...\n"
     "........\n"
     "........\n"},
    {"rot180", Rot180, true, 2, 1,
     "........\n"
     "....o#..\n"
     ".....#..\n"
     "...###..\n"
     "........\n"
     "........\n"},
    {"rot270", Rot270, true, 2, 1,
     "........\n"
     "........\n"
     "..#.....\n"
     "..#.o...\n"
     "..###...\n"
     "........\n"},
    {"flipX+rot90", FlipX | Rot90, true, 2, 1,  // flip first, then rotate (the other order looks different)
     "........\n"
     "........\n"
     "....#...\n"
     "..o.#...\n"
     "..###...\n"
     "........\n"},
    {"clipLeftTop", FlipX, true, -2, -1,
     ".#......\n"
     "o#......\n"
     "........\n"
     "........\n"
     "........\n"
     "........\n"},
    {"clipRightBottom", Rot90, true, 6, 3,
     "........\n"
     "........\n"
     "........\n"
     "......##\n"
     "......o.\n"
     "........\n"},
};

static int testGoldens() {
  static Color buf[AW * AH];
  char art[AW * AH + AH + 1];
  for (const Golden& g : kGoldens) {
    std::memset(buf, 0, sizeof buf);
    Canvas c(buf, AW, AH, 0, AH, 0);
    SpriteSheet s = kSheet;
    s.transparent = g.transparent;
    c.sprite(s, 0, g.x, g.y, g.f);
    char* o = art;
    for (int y = 0; y < AH; y++) {
      for (int x = 0; x < AW; x++) {
        Color v = buf[y * AW + x];
        *o++ = v == 0 ? '.' : v == 1 ? '#' : v == 2 ? 'o' : v == 0x7777 ? '+' : '?';
      }
      *o++ = '\n';
    }
    *o = 0;
    if (std::strcmp(art, g.art) != 0) std::printf("golden %s differs, got:\n%s", g.name, art);
    CHECK(std::strcmp(art, g.art) == 0);
  }
  return 0;
}

// ---- 2. reference model (explicit image transforms) vs Canvas, full height and bands ----
struct Img { int w, h; uint8_t p[64]; };

static Img flipXImg(const Img& a) {
  Img o = a;
  for (int y = 0; y < a.h; y++)
    for (int x = 0; x < a.w; x++) o.p[y * a.w + x] = a.p[y * a.w + a.w - 1 - x];
  return o;
}
static Img flipYImg(const Img& a) {
  Img o = a;
  for (int y = 0; y < a.h; y++)
    for (int x = 0; x < a.w; x++) o.p[y * a.w + x] = a.p[(a.h - 1 - y) * a.w + x];
  return o;
}
static Img rotCw(const Img& a) {  // pixel (x, y) moves to (h - 1 - y, x)
  Img o;
  o.w = a.h;
  o.h = a.w;
  for (int y = 0; y < a.h; y++)
    for (int x = 0; x < a.w; x++) o.p[x * o.w + (a.h - 1 - y)] = a.p[y * a.w + x];
  return o;
}
static Img transform(Img a, int flags) {
  if (flags & FlipX) a = flipXImg(a);
  if (flags & FlipY) a = flipYImg(a);
  for (int k = 0; k < ((flags >> 2) & 3); k++) a = rotCw(a);
  return a;
}

enum { RW = 24, RH = 18, SPW = 7, SPH = 5 };
static uint8_t g_spx[SPW * SPH];
static const Color kPal4[4] = {0x0100, 0x0101, 0x0102, 0x0103};
struct Job { const SpriteSheet* s; int x, y; SpriteFlags f; };
static void drawJob(Canvas& c, void* p) {
  const Job* j = static_cast<const Job*>(p);
  c.sprite(*j->s, 0, j->x, j->y, j->f);
}

static int testReference() {
  for (int i = 0; i < SPW * SPH; i++) g_spx[i] = (uint8_t)((i * 7 + i / 3) % 4);
  Img base;
  base.w = SPW;
  base.h = SPH;
  std::memcpy(base.p, g_spx, sizeof g_spx);
  static Color band[RW * RH], ref[RW * RH];
  const int xs[] = {-9, -6, -3, 0, 5, 17, 20, 23, 24};
  const int ys[] = {-8, -5, -2, 0, 3, 7, 13, 16, 19};
  const int bands[] = {18, 7, 3, 1};
  for (int tr = 0; tr < 2; tr++)
    for (int f = 0; f < 16; f++) {
      const Img t = transform(base, f);
      for (int x : xs)
        for (int y : ys) {
          for (int i = 0; i < RW * RH; i++) ref[i] = kClear;
          for (int v = 0; v < t.h; v++)
            for (int u = 0; u < t.w; u++) {
              int dx = x + u, dy = y + v;
              if (dx < 0 || dx >= RW || dy < 0 || dy >= RH) continue;
              uint8_t idx = t.p[v * t.w + u];
              if (tr && idx == 0) continue;
              ref[dy * RW + dx] = kPal4[idx];
            }
          const SpriteSheet s = {g_spx, kPal4, SPW, SPH, 1, tr != 0};
          Job job = {&s, x, y, static_cast<SpriteFlags>(f)};
          for (int b : bands) {
            mockReset(RW, RH);
            RenderConfig cfg = {RW, RH, (uint16_t)b, kClear};
            CHECK(renderFrame(cfg, band, RW * RH, 0, drawJob, &job) == 0);
            if (std::memcmp(g_fb, ref, sizeof ref) != 0)
              std::printf("reference differs: transparent=%d flags=%d x=%d y=%d band=%d\n", tr, f, x, y, b);
            CHECK(std::memcmp(g_fb, ref, sizeof ref) == 0);
          }
        }
    }
  return 0;
}

// ---- 3. atlas sheet: 2 columns x 2 rows of 3x2 frames ----
static int testAtlas() {
  static const uint8_t px[24] = {1, 1, 1, 2, 2, 2, 1, 9, 1, 2, 9, 2, 3, 3, 3, 4, 4, 4, 3, 9, 3, 4, 9, 4};
  static Color pal[10];
  for (int i = 0; i < 10; i++) pal[i] = (Color)(0x200 + i);
  SpriteSheet s = {px, pal, 3, 2, 4, false, 2};
  for (uint16_t f = 0; f < 4; f++) {
    Color buf[6] = {};
    Canvas c(buf, 3, 2, 0, 2, 0);
    c.sprite(s, f, 0, 0, None);
    const Color fill = pal[f + 1], mark = pal[9];
    const Color want[6] = {fill, fill, fill, fill, mark, fill};
    CHECK(std::memcmp(buf, want, sizeof buf) == 0);
  }
  Color buf[6] = {};
  Canvas c(buf, 3, 2, 0, 2, 0);
  c.sprite(s, 4, 0, 0, None);  // frame out of range: nothing drawn
  for (Color v : buf) CHECK(v == 0);
  return 0;
}

// ---- 4. animation helper ----
static int testAnim() {
  const Animation loop = {4, 3, 5, true}, once = {4, 3, 5, false};
  CHECK(animFrame(loop, 0) == 4 && animFrame(loop, 4) == 4 && animFrame(loop, 5) == 5);
  CHECK(animFrame(loop, 14) == 6 && animFrame(loop, 15) == 4 && animFrame(loop, 31) == 4 + (31 / 5) % 3);
  CHECK(animFrame(once, 14) == 6 && animFrame(once, 15) == 6 && animFrame(once, 4000000000u) == 6);
  CHECK(animFrame(Animation{9, 0, 5, true}, 77) == 9);
  CHECK(animFrame(Animation{2, 4, 0, true}, 3) == 2 + 3);  // ticks_per_frame 0 counts as 1
  return 0;
}

int main() {
  if (testGoldens() || testReference() || testAtlas() || testAnim()) return 1;
  std::printf("sprites OK\n");
  return 0;
}
