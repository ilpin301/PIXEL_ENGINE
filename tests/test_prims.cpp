// Primitives, text, palette, integer scale and drop-counter tests (own mock display HAL).
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include "pge/color.h"
#include "pge/gfx.h"
#include "pge/hal/display.h"

#define CHECK(c)                                                          \
  do {                                                                    \
    if (!(c)) {                                                           \
      std::printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); \
      return 1;                                                           \
    }                                                                     \
  } while (0)

using namespace pge;

// ---- mock display HAL: a plane of g_pw x g_ph pixels that flushed rects are copied into ----
enum { MAXPX = 320 * 240, MAXRECTS = 256 };
static Color g_fb[MAXPX];
static int g_pw, g_ph;
static pge_rect g_rects[MAXRECTS];
static int g_nflush, g_nbegin;

static void mockReset(int pw, int ph) {
  std::memset(g_fb, 0xFF, sizeof g_fb);
  g_pw = pw;
  g_ph = ph;
  g_nflush = g_nbegin = 0;
}

extern "C" {
pge_error pge_hal_display_init(uint16_t, uint16_t, pge_display_info*) { return 0; }
pge_error pge_hal_display_flush(pge_rect r, uint16_t* px) {
  if (r.x + r.w > g_pw || r.y + r.h > g_ph || g_nflush >= MAXRECTS) return -2;
  g_rects[g_nflush++] = r;
  for (int y = 0; y < r.h; y++) std::memcpy(&g_fb[(r.y + y) * g_pw + r.x], px + y * r.w, r.w * sizeof(Color));
  return 0;
}
pge_error pge_hal_display_wait(void) { return 0; }
void pge_hal_display_frame_begin(void) { g_nbegin++; }
void pge_hal_display_frame_end(void) {}
}

static const Color kClear = 0x1234;  // not a palette colour

// ---- 1. golden ASCII art ----
enum { AW = 16, AH = 12 };
struct Golden { const char* name; void (*draw)(Canvas&); const char* art; };

static const Golden kGoldens[] = {
    {"hline", [](Canvas& c) { c.line(2, 3, 12, 3, 1); },
     "................\n"
     "................\n"
     "................\n"
     "..###########...\n"
     "................\n"
     "................\n"
     "................\n"
     "................\n"
     "................\n"
     "................\n"
     "................\n"
     "................\n"},
    {"vline", [](Canvas& c) { c.line(5, 1, 5, 10, 1); },
     "................\n"
     ".....#..........\n"
     ".....#..........\n"
     ".....#..........\n"
     ".....#..........\n"
     ".....#..........\n"
     ".....#..........\n"
     ".....#..........\n"
     ".....#..........\n"
     ".....#..........\n"
     ".....#..........\n"
     "................\n"},
    {"steep", [](Canvas& c) { c.line(2, 1, 6, 10, 1); },
     "................\n"
     "..#.............\n"
     "..#.............\n"
     "...#............\n"
     "...#............\n"
     "....#...........\n"
     "....#...........\n"
     ".....#..........\n"
     ".....#..........\n"
     "......#.........\n"
     "......#.........\n"
     "................\n"},
    {"shallow", [](Canvas& c) { c.line(1, 2, 14, 6, 1); },
     "................\n"
     "................\n"
     ".##.............\n"
     "...###..........\n"
     "......####......\n"
     "..........###...\n"
     ".............##.\n"
     "................\n"
     "................\n"
     "................\n"
     "................\n"
     "................\n"},
    {"reversed", [](Canvas& c) { c.line(14, 6, 1, 2, 1); },
     "................\n"
     "................\n"
     ".##.............\n"
     "...###..........\n"
     "......####......\n"
     "..........###...\n"
     ".............##.\n"
     "................\n"
     "................\n"
     "................\n"
     "................\n"
     "................\n"},
    {"dot", [](Canvas& c) { c.line(7, 5, 7, 5, 1); },
     "................\n"
     "................\n"
     "................\n"
     "................\n"
     "................\n"
     ".......#........\n"
     "................\n"
     "................\n"
     "................\n"
     "................\n"
     "................\n"
     "................\n"},
    {"rect5x4", [](Canvas& c) { c.rect(Rect{2, 2, 5, 4}, 1); },
     "................\n"
     "................\n"
     "..#####.........\n"
     "..#...#.........\n"
     "..#...#.........\n"
     "..#####.........\n"
     "................\n"
     "................\n"
     "................\n"
     "................\n"
     "................\n"
     "................\n"},
    {"rect_w1", [](Canvas& c) { c.rect(Rect{4, 2, 1, 6}, 1); },
     "................\n"
     "................\n"
     "....#...........\n"
     "....#...........\n"
     "....#...........\n"
     "....#...........\n"
     "....#...........\n"
     "....#...........\n"
     "................\n"
     "................\n"
     "................\n"
     "................\n"},
    {"circle0", [](Canvas& c) { c.circle(8, 6, 0, 1); },
     "................\n"
     "................\n"
     "................\n"
     "................\n"
     "................\n"
     "................\n"
     "........#.......\n"
     "................\n"
     "................\n"
     "................\n"
     "................\n"
     "................\n"},
    {"circle1", [](Canvas& c) { c.circle(8, 6, 1, 1); },
     "................\n"
     "................\n"
     "................\n"
     "................\n"
     "................\n"
     "........#.......\n"
     ".......#.#......\n"
     "........#.......\n"
     "................\n"
     "................\n"
     "................\n"
     "................\n"},
    {"circle3", [](Canvas& c) { c.circle(8, 6, 3, 1); },
     "................\n"
     "................\n"
     "................\n"
     ".......###......\n"
     "......#...#.....\n"
     ".....#.....#....\n"
     ".....#.....#....\n"
     ".....#.....#....\n"
     "......#...#.....\n"
     ".......###......\n"
     "................\n"
     "................\n"},
    {"circle5", [](Canvas& c) { c.circle(8, 6, 5, 1); },
     "................\n"
     "......#####.....\n"
     ".....#.....#....\n"
     "....#.......#...\n"
     "...#.........#..\n"
     "...#.........#..\n"
     "...#.........#..\n"
     "...#.........#..\n"
     "...#.........#..\n"
     "....#.......#...\n"
     ".....#.....#....\n"
     "......#####.....\n"},
    {"fill3", [](Canvas& c) { c.fillCircle(8, 6, 3, 1); },
     "................\n"
     "................\n"
     "................\n"
     ".......###......\n"
     "......#####.....\n"
     ".....#######....\n"
     ".....#######....\n"
     ".....#######....\n"
     "......#####.....\n"
     ".......###......\n"
     "................\n"
     "................\n"},
    {"text_Hi", [](Canvas& c) { c.text(kFont5x7, 2, 2, "Hi", 1); },
     "................\n"
     "................\n"
     "..#...#...#.....\n"
     "..#...#.........\n"
     "..#####..##.....\n"
     "..#...#...#.....\n"
     "..#...#...#.....\n"
     "..#...#..###....\n"
     "................\n"
     "................\n"
     "................\n"
     "................\n"},
    {"text_A!", [](Canvas& c) { c.text(kFont5x7, 2, 2, "A!", 1); },
     "................\n"
     "................\n"
     "...###....#.....\n"
     "..#...#...#.....\n"
     "..#...#...#.....\n"
     "..#####...#.....\n"
     "..#...#.........\n"
     "..#...#...#.....\n"
     "................\n"
     "................\n"
     "................\n"
     "................\n"},
};

static int renderArt(const Golden& g, char* out) {
  static Color buf[AW * AH];
  std::memset(buf, 0, sizeof buf);
  Canvas c(buf, AW, AH, 0, AH, 0);
  g.draw(c);
  for (int y = 0; y < AH; y++) {
    for (int x = 0; x < AW; x++) *out++ = buf[y * AW + x] ? '#' : '.';
    *out++ = '\n';
  }
  *out = 0;
  return 0;
}

static int testGoldens() {
  char art[AW * AH + AH + 1];
  for (const Golden& g : kGoldens) {
    renderArt(g, art);
    if (std::strcmp(art, g.art) != 0) std::printf("golden %s differs, got:\n%s", g.name, art);
    CHECK(std::strcmp(art, g.art) == 0);
  }
  return 0;
}

// ---- 2. property: clipped draw == crop of the same draw on an oversized canvas, full and band ----
enum { SW = 40, SH = 30, OFF = 64, BW = SW + 2 * OFF, BH = SH + 2 * OFF, GUARD = 64, BANDY = 10, BANDH = 8 };
static Color g_small[GUARD + SW * SH + GUARD], g_big[GUARD + BW * BH + GUARD], g_bnd[GUARD + SW * BANDH + GUARD];
static const Color kBg = 0xAAAA, kFg = 0x1111;

static void fillBuf(Color* mem, int n) {
  for (int i = 0; i < GUARD + n + GUARD; i++) mem[i] = 0xDEAD;
  for (int i = 0; i < n; i++) mem[GUARD + i] = kBg;
}
static bool guardsOk(const Color* mem, int n) {
  for (int i = 0; i < GUARD; i++)
    if (mem[i] != 0xDEAD || mem[GUARD + n + i] != 0xDEAD) return false;
  return true;
}

enum Kind { KPixel, KLine, KRect, KCircle, KFill, KText };
static void drawKind(Canvas& c, int kind, int o, int x, int y, int p, int q) {
  switch (kind) {
    case KPixel: c.pixel(x + o, y + o, kFg); break;
    case KLine: c.line(x + o, y + o, p + o, q + o, kFg); break;
    case KRect: c.rect(Rect{x + o, y + o, p, q}, kFg); break;
    case KCircle: c.circle(x + o, y + o, p, kFg); break;
    case KFill: c.fillCircle(x + o, y + o, p, kFg); break;
    default: c.text(kFont5x7, x + o, y + o, q ? "W?~" : "Hi\nAb", kFg); break;
  }
}

static int testProperty() {
  static const int s15[] = {-30, -23, -16, -9, -2, 5, 12, 19, 26, 33, 40, 47, 54, 61, 68};
  static const int s8[] = {-30, -16, -2, 12, 26, 40, 54, 68};
  static const int sizes[] = {0, 1, 2, 9, 45};
  static const int radii[] = {0, 1, 2, 5, 12, 20};
  static const int zero[] = {0}, two[] = {0, 1};
#define SET(a) a, (int)(sizeof a / sizeof a[0])
  struct Sweep { int kind; const int* xs; int nx; const int* ps; int np; const int* qs; int nq; };
  const Sweep sweeps[] = {{KPixel, SET(s15), SET(zero), SET(zero)},  {KLine, SET(s8), SET(s8), SET(s8)},
                          {KRect, SET(s15), SET(sizes), SET(sizes)}, {KCircle, SET(s15), SET(radii), SET(zero)},
                          {KFill, SET(s15), SET(radii), SET(zero)},  {KText, SET(s15), SET(zero), SET(two)}};
#undef SET
  for (const Sweep& sw : sweeps)
    for (int xi = 0; xi < sw.nx; xi++)
      for (int yi = 0; yi < sw.nx; yi++)
        for (int pi = 0; pi < sw.np; pi++)
          for (int qi = 0; qi < sw.nq; qi++) {
            int x = sw.xs[xi], y = sw.xs[yi], p = sw.ps[pi], q = sw.qs[qi];
            fillBuf(g_small, SW * SH);
            fillBuf(g_big, BW * BH);
            fillBuf(g_bnd, SW * BANDH);
            Canvas a(g_small + GUARD, SW, SH, 0, SH, 0), b(g_big + GUARD, BW, BH, 0, BH, 0);
            Canvas d(g_bnd + GUARD, SW, SH, BANDY, BANDH, 0);
            drawKind(a, sw.kind, 0, x, y, p, q);
            drawKind(b, sw.kind, OFF, x, y, p, q);
            drawKind(d, sw.kind, 0, x, y, p, q);
            CHECK(guardsOk(g_small, SW * SH) && guardsOk(g_big, BW * BH) && guardsOk(g_bnd, SW * BANDH));
            for (int py = 0; py < SH; py++)
              for (int px = 0; px < SW; px++) {
                Color v = g_small[GUARD + py * SW + px];
                if (v != g_big[GUARD + (py + OFF) * BW + px + OFF] ||
                    (py >= BANDY && py < BANDY + BANDH && v != g_bnd[GUARD + (py - BANDY) * SW + px])) {
                  std::printf("kind %d x=%d y=%d p=%d q=%d differs at %d,%d\n", sw.kind, x, y, p, q, px, py);
                  return 1;
                }
              }
          }
  return 0;
}

// Every outline pixel of circle() lies inside fillCircle().
static int testCircleFill() {
  enum { N = 90 };
  static Color a[N * N], b[N * N];
  for (int r = 0; r <= 40; r++) {
    std::memset(a, 0, sizeof a);
    std::memset(b, 0, sizeof b);
    Canvas ca(a, N, N, 0, N, 0), cb(b, N, N, 0, N, 0);
    ca.circle(45, 45, r, 1);
    cb.fillCircle(45, 45, r, 1);
    int inner = 0;
    for (int i = 0; i < N * N; i++) {
      if (a[i]) CHECK(b[i]);
      inner += b[i] && !a[i];
    }
    CHECK(r < 2 || inner > 0);  // filled, not just an outline
  }
  return 0;
}

// ---- scene used by the band and scale tests; positions derive from the canvas size ----
static void sceneFn(Canvas& c, void*) {
  const int w = c.width(), h = c.height();
  for (int i = 0; i < 16; i++) c.fillRect(Rect{i * w / 16, 0, w / 16, h / 12 + 1}, kPalette16[i]);
  c.rect(Rect{w / 10, h / 5, w * 3 / 5, h * 3 / 5}, kPalette16[5]);
  c.line(0, h / 5, w - 1, h - 1, kPalette16[2]);
  c.line(w - 1, h / 4, w / 3, h - 1, kPalette16[3]);
  c.line(w / 2, h / 5, w / 2 + 3, h - 1, kPalette16[6]);
  c.circle(w / 2, h / 2, h / 4, kPalette16[4]);
  c.fillCircle(w * 3 / 4, h / 3, h / 6, kPalette16[11]);
  c.fillCircle(w / 5, h * 4 / 5, 3, kPalette16[7]);
  c.pixel(w / 3, h / 3, kPalette16[1]);
  c.text(kFont5x7, 2, h / 2, "Hi\nAb", kPalette16[1]);
  c.text(kFont5x7, w - 8, h - 5, "xy", kPalette16[10]);  // partly off screen
}

// ---- 3. band-vs-full through renderFrame ----
static Color g_band[MAXPX];

static int testBandVsFull() {
  static Color ref[MAXPX], cur[MAXPX];
  const int bands[] = {240, 16, 8, 7, 1};
  for (int i = 0; i < 5; i++) {
    mockReset(320, 240);
    RenderConfig cfg = {320, 240, (uint16_t)bands[i], kClear};
    CHECK(renderFrame(cfg, g_band, MAXPX, 0, sceneFn, nullptr) == 0);
    if (i == 0) {
      std::memcpy(ref, g_fb, sizeof ref);
      for (int k = 0; k < 16; k++) {  // every palette colour reached the frame
        bool seen = false;
        for (int p = 0; p < MAXPX; p++) seen = seen || ref[p] == kPalette16[k];
        CHECK(seen);
      }
    } else {
      std::memcpy(cur, g_fb, sizeof cur);
      CHECK(std::memcmp(ref, cur, sizeof ref) == 0);
    }
  }
  return 0;
}

// ---- 4. integer scale ----
static int testScale() {
  enum { LW = 40, LH = 30 };
  static Color stage[8 * 3 * LW * 3], ref[LW * LH];
  for (int band : {8, 7}) {
    int lines = band;
    for (int s = 1; s <= 3; s++) {
      mockReset(LW * s, LH * s);
      RenderConfig cfg = {LW, LH, (uint16_t)band, kClear, (uint8_t)s, s > 1 ? stage : nullptr,
                          (uint32_t)(lines * s * LW * s)};
      CHECK(renderFrame(cfg, g_band, MAXPX, 5, sceneFn, nullptr) == 0);
      if (s == 1) std::memcpy(ref, g_fb, sizeof ref);
      int drawn = 0;
      for (int y = 0; y < LH * s; y++)
        for (int x = 0; x < LW * s; x++) {
          CHECK(g_fb[y * g_pw + x] == ref[(y / s) * LW + x / s]);
          drawn += g_fb[y * g_pw + x] != kClear;
        }
      CHECK(drawn > 200 * s * s);
      int y = 0;
      CHECK(g_nflush == (LH + lines - 1) / lines);
      for (int i = 0; i < g_nflush; i++) {
        CHECK(g_rects[i].x == 0 && g_rects[i].w == LW * s && g_rects[i].y == y && g_rects[i].h > 0);
        y += g_rects[i].h;
      }
      CHECK(y == LH * s);
    }
    RenderConfig bad = {LW, LH, (uint16_t)band, kClear, 2, stage, (uint32_t)(lines * 2 * LW * 2)};
    const int bad_scale[] = {0, 4};
    for (int s : bad_scale) {
      mockReset(LW * 3, LH * 3);
      bad.scale = (uint8_t)s;
      CHECK(renderFrame(bad, g_band, MAXPX, 0, sceneFn, nullptr) == InvalidArg);
      CHECK(g_nbegin == 0 && g_nflush == 0);
    }
    bad.scale = 2;
    bad.stage = nullptr;
    CHECK(renderFrame(bad, g_band, MAXPX, 0, sceneFn, nullptr) == InvalidArg);
    bad.stage = stage;
    bad.stage_pixels--;
    CHECK(renderFrame(bad, g_band, MAXPX, 0, sceneFn, nullptr) == InvalidArg);
    CHECK(g_nbegin == 0 && g_nflush == 0);
  }
  return 0;
}

// ---- 5. drop counter ----
static int testDrops() {
  static Color buf[SW * SH];
  static const Font nofont = {nullptr, 5, 7, 32, 95, 1};
  typedef void (*Bad)(Canvas&);
  const Bad bad[] = {
      [](Canvas& c) { c.line(0, 0, 20000, 3, 1); },
      [](Canvas& c) { c.line(0, -20000, 5, 3, 1); },
      [](Canvas& c) { c.circle(5, 5, -1, 1); },
      [](Canvas& c) { c.circle(5, 5, 20000, 1); },
      [](Canvas& c) { c.fillCircle(5, 5, -1, 1); },
      [](Canvas& c) { c.text(kFont5x7, 1, 1, nullptr, 1); },
      [](Canvas& c) { c.text(nofont, 1, 1, "x", 1); },
  };
  const Bad good[] = {
      [](Canvas& c) { c.pixel(-100, -100, 1); },
      [](Canvas& c) { c.line(-50, -50, -20, -60, 1); },
      [](Canvas& c) { c.line(-16384, 0, 16384, 16384, 1); },  // the limit itself is valid
      [](Canvas& c) { c.rect(Rect{-100, -100, 5, 5}, 1); },
      [](Canvas& c) { c.circle(500, 500, 10, 1); },
      [](Canvas& c) { c.fillCircle(-500, 2, 7, 1); },
      [](Canvas& c) { c.text(kFont5x7, -300, -300, "abc\ndef", 1); },
  };
  for (Bad f : bad) {
    Canvas top(buf, SW, SH, 0, SH, 0), lower(buf, SW, SH, BANDY, BANDH, 0);
    f(top);
    f(lower);
    CHECK(top.dropped() == 1 && lower.dropped() == 0);
  }
  for (Bad f : good) {
    Canvas top(buf, SW, SH, 0, SH, 0), lower(buf, SW, SH, BANDY, BANDH, 0);
    f(top);
    f(lower);
    CHECK(top.dropped() == 0 && lower.dropped() == 0);
  }
  return 0;
}

// ---- 6. color.h ----
static int testColor() {
  CHECK(rgb(0, 0, 0) == 0 && rgb(255, 255, 255) == 0xFFFF && rgb(255, 0, 0) == 0xF800);
  CHECK(rgb(0, 255, 0) == 0x07E0 && rgb(0, 0, 255) == 0x001F);
  CHECK(red8(0xFFFF) == 255 && green8(0xFFFF) == 255 && blue8(0xFFFF) == 255);
  CHECK(red8(0) == 0 && green8(0) == 0 && blue8(0) == 0);
  CHECK(red8(0xF800) == 255 && green8(0x07E0) == 255 && blue8(0x001F) == 255 && red8(0x07FF) == 0);
  CHECK(kPalette16[0] == 0);
  for (int i = 0; i < 16; i++)
    for (int j = i + 1; j < 16; j++) CHECK(kPalette16[i] != kPalette16[j]);
  return 0;
}

// ---- 7. textWidth ----
static int testTextWidth() {
  CHECK(textWidth(kFont5x7, "") == 0 && textWidth(kFont5x7, "A") == 5 && textWidth(kFont5x7, "AB") == 11);
  CHECK(textWidth(kFont5x7, "AB\nC") == 11 && textWidth(kFont5x7, "A\nBCD") == 17 && textWidth(kFont5x7, nullptr) == 0);
  return 0;
}

// ---- 8. UTF-8 text: EN / RU / DE golden, glyphs counted per codepoint, bad input draws '?' ----
enum { UW = 39, UH = 27 };  // widest line "Привет" (35 px) + 4
static const char* const kUtf8Text = "Hi!\nПривет\nGröße";
static const char* const kUtf8Art =
    ".......................................\n"
    ".......................................\n"
    "..#...#...#.....#......................\n"
    "..#...#.........#......................\n"
    "..#####..##.....#......................\n"
    "..#...#...#.....#......................\n"
    "..#...#...#............................\n"
    "..#...#..###....#......................\n"
    ".......................................\n"
    ".......................................\n"
    "..#####................................\n"
    "..#...#.#.##..#...#.####...###..#####..\n"
    "..#...#.##..#.#..##.#...#.#...#...#....\n"
    "..#...#.#...#.#.#.#.####..#####...#....\n"
    "..#...#.##..#.##..#.#...#.#.......#....\n"
    "..#...#.#.##..#...#.####...###....#....\n"
    "........#..............................\n"
    ".......................................\n"
    "...###.........#.#...##................\n"
    "..#...#.#.##........#..#...###.........\n"
    "..#.....##..#..###..#..#..#...#........\n"
    "..#..##.#.....#...#.#.##..#####........\n"
    "..#...#.#.....#...#.#..#..#............\n"
    "...####.#......###..#.##...###.........\n"
    ".......................................\n"
    ".......................................\n"
    ".......................................\n";

// Draws s at (2, 2) on a UW x UH canvas and writes its '#'/'.' art to out; returns the lit pixel count.
static int artOf(const char* s, char* out) {
  static Color buf[UW * UH];
  std::memset(buf, 0, sizeof buf);
  Canvas c(buf, UW, UH, 0, UH, 0);
  c.text(kFont5x7, 2, 2, s, 1);
  int lit = 0;
  for (int y = 0; y < UH; y++) {
    for (int x = 0; x < UW; x++) {
      lit += buf[y * UW + x] != 0;
      *out++ = buf[y * UW + x] ? '#' : '.';
    }
    *out++ = '\n';
  }
  *out = 0;
  return lit;
}

static int testUtf8() {
  char art[UW * UH + UH + 1], want[UW * UH + UH + 1];
  CHECK(textWidth(kFont5x7, kUtf8Text) + 4 == UW);
  CHECK(artOf(kUtf8Text, art) > 100);
  if (std::strcmp(art, kUtf8Art) != 0) std::printf("utf8 golden differs, got:\n%s", art);
  CHECK(std::strcmp(art, kUtf8Art) == 0);
  // one glyph per codepoint, not per byte
  CHECK(textWidth(kFont5x7, "Ä") == 5 && textWidth(kFont5x7, "Привет") == 6 * 6 - 1 && textWidth(kFont5x7, "aÄb") == 17);
  CHECK(textWidth(kFont5x7, "Ä\nÖÖ") == 11);
  // whatever the font cannot show, and every bad byte, is drawn as '?'; an invalid byte eats exactly one byte
  struct Pair { const char* got; const char* want; };
  static const Pair pairs[] = {
      {"\xFF", "?"},                  // invalid byte
      {"a\x80" "b", "a?b"},           // stray continuation byte
      {"A\xD0", "A?"},                // truncated sequence at the NUL
      {"\xC0\x80", "??"},            // overlong NUL
      {"\xE2\x82", "??"},            // truncated 3-byte sequence
      {"\xED\xA0\x80", "???"},      // UTF-16 surrogate
      {"€", "?"},                      // valid, not in the font
      {"\xF0\x9F\x98\x80", "?"},   // valid 4-byte sequence, one glyph
  };
  for (const Pair& p : pairs) {
    CHECK(artOf(p.got, art) > 0 && artOf(p.want, want) > 0);
    CHECK(std::strcmp(art, want) == 0 && textWidth(kFont5x7, p.got) == textWidth(kFont5x7, p.want));
  }
  // the bytes after a NUL are never looked at
  static const char cut1[] = {'A', (char)0xD0, 0, (char)0x80, (char)0x80}, cut2[] = {(char)0xE2, (char)0x82, 0, (char)0xAC};
  CHECK(textWidth(kFont5x7, cut1) == 11 && textWidth(kFont5x7, cut2) == 11);
  // pin every extra glyph: Ä Ö Ü ß ä ö ü Ё А..я ё are glyphs 95..167 in order, drawn exactly as their table rows
  CHECK(kFont5x7.w == 5 && kFont5x7.h == 7);
  uint16_t cps[73] = {0xC4, 0xD6, 0xDC, 0xDF, 0xE4, 0xF6, 0xFC, 0x401};
  for (int i = 0; i < 64; i++) cps[8 + i] = (uint16_t)(0x410 + i);
  cps[72] = 0x451;
  static Color gbuf[5 * 7];
  for (int n = 0; n < 73; n++) {
    char s[3] = {(char)(0xC0 | (cps[n] >> 6)), (char)(0x80 | (cps[n] & 0x3F)), 0};  // all 73 are 2-byte sequences
    std::memset(gbuf, 0, sizeof gbuf);
    Canvas c(gbuf, 5, 7, 0, 7, 0);
    c.text(kFont5x7, 0, 0, s, 1);
    const uint8_t* rows = kFont5x7.rows + (95 + n) * 7;
    for (int y = 0; y < 7; y++)
      for (int x = 0; x < 5; x++) CHECK((gbuf[y * 5 + x] != 0) == (((rows[y] >> (4 - x)) & 1) != 0));
  }
  // a lowercase mark letter must not be the capital
  CHECK(artOf("Ö", art) > 0 && artOf("ö", want) > 0 && std::strcmp(art, want) != 0);
  CHECK(artOf("Й", art) > 0 && artOf("й", want) > 0 && std::strcmp(art, want) != 0);
  return 0;
}

int main() {
  int rc;
  if ((rc = testGoldens())) return rc;
  if ((rc = testProperty())) return rc;
  if ((rc = testCircleFill())) return rc;
  if ((rc = testBandVsFull())) return rc;
  if ((rc = testScale())) return rc;
  if ((rc = testDrops())) return rc;
  if ((rc = testColor())) return rc;
  if ((rc = testTextWidth())) return rc;
  if ((rc = testUtf8())) return rc;
  std::printf("prims: all checks passed\n");
  return 0;
}
