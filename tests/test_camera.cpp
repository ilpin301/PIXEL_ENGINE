// Camera tests: clampAxis / Camera / parallaxOffset unit cases, an ASCII golden for two parallax layers, and
// an independent reference model compared with full and band rendering (own mock display HAL). S8.4a adds the
// orientation tests (orient 0..3): turnRect and Camera units, the reference model and the golden scene under every
// orient, the gravity-to-orient anchor, sprite == tile, and the eight orientations of a sprite.
// Canvas::tilemapRotated is checked against tilemap at quarter turns, a double-precision model at other angles, bands,
// edges and drops.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "pge/camera.h"
#include "pge/collision.h"
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

// ---- orientation helpers, written from the contract only (nothing here calls the engine's turn code) ----
// "orient" = number of clockwise quarter turns applied to the whole world picture before it is shown, 0..3 (only the
// low 2 bits count). Same numbering as the rotation field in bits 2-3 of SpriteFlags: 0 none, 1 = Rot90,
// 2 = Rot180, 3 = Rot270. Turning a picture clockwise by one quarter moves its top-left corner to the top-right, its
// top-right to the bottom-right, its bottom-right to the bottom-left: pixel (x, y) of a w x h picture becomes
// (h - 1 - y, x) of the h x w result. So after 1 turn the world's left edge is the screen's top edge, after 2 turns
// it is the right edge, after 3 turns the bottom edge.
// Camera x,y = top-left corner of the axis-aligned WORLD rect that the screen shows: view_w x view_h world pixels
// for even orient, view_h x view_w for odd orient (visW x visH).
// turnRect(r, view_w, view_h, quarter_turns): r is relative to the top-left of a view_w x view_h picture; the result is
// the same rect after that picture is turned clockwise by quarter_turns (masked with 3), relative to the top-left of
// the turned picture (view_h x view_w for odd turns); w and h swap for odd turns.
static void turnPt(int& x, int& y, int w, int h, int k) {
  for (int i = 0; i < (k & 3); i++) {
    const int nx = h - 1 - y;
    y = x;
    x = nx;
    const int t = w;
    w = h;
    h = t;
  }
}

// A rect of a w x h picture after k quarter turns, from its two opposite corner pixels.
static Rect turnRef(Rect r, int w, int h, int k) {
  int x0 = r.x, y0 = r.y, x1 = r.x + r.w - 1, y1 = r.y + r.h - 1;
  turnPt(x0, y0, w, h, k);
  turnPt(x1, y1, w, h, k);
  return Rect{std::min(x0, x1), std::min(y0, y1), std::abs(x1 - x0) + 1, std::abs(y1 - y0) + 1};
}

// The whole w x h picture turned clockwise by k quarters (result is h x w for odd k), plain loops.
static void turnPic(const Color* src, int w, int h, int k, Color* out) {
  static Color a[MAXPX], b[MAXPX];
  std::memcpy(a, src, (size_t)w * h * sizeof(Color));
  for (int i = 0; i < (k & 3); i++) {
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++) b[x * h + (h - 1 - y)] = a[y * w + x];
    std::memcpy(a, b, (size_t)w * h * sizeof(Color));
    const int t = w;
    w = h;
    h = t;
  }
  std::memcpy(out, a, (size_t)w * h * sizeof(Color));
}

// Inverse of the turn for a screen of sw x sh: the view-relative world offset (u, v) shown at screen pixel (sx, sy).
static void invTurn(int o, int sx, int sy, int sw, int sh, int& u, int& v) {
  switch (o & 3) {
    case 0: u = sx; v = sy; break;
    case 1: u = sy; v = sw - 1 - sx; break;
    case 2: u = sw - 1 - sx; v = sh - 1 - sy; break;
    default: u = sh - 1 - sy; v = sx; break;
  }
}

static bool sameRect(Rect a, Rect b) {
  if (a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h) return true;
  std::printf("rect got (%d,%d,%d,%d) want (%d,%d,%d,%d)\n", a.x, a.y, a.w, a.h, b.x, b.y, b.w, b.h);
  return false;
}

// True if equal; else prints want | got side by side (one glyph per distinct colour, legend below) and returns false.
static bool samePic(const char* tag, const Color* want, const Color* got, int w, int h) {
  if (std::memcmp(want, got, (size_t)w * h * sizeof(Color)) == 0) return true;
  static const char kGlyph[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
  Color seen[62] = {};
  int n = 0;
  std::printf("%s: %dx%d pictures differ, want | got\n", tag, w, h);
  for (int y = 0; y < h; y++)
    for (int side = 0; side < 2; side++) {
      const Color* p = side ? got : want;
      for (int x = 0; x < w; x++) {
        int i = 0;
        while (i < n && seen[i] != p[y * w + x]) i++;
        if (i == n && n < 62) seen[n++] = p[y * w + x];
        std::putchar(i < 62 ? kGlyph[i] : '?');
      }
      std::fputs(side ? "\n" : " | ", stdout);
    }
  for (int i = 0; i < n; i++) std::printf("%c=0x%04X ", kGlyph[i], (unsigned)seen[i]);
  std::printf("\n");
  return false;
}

// ---- 1. clampAxis, Camera, parallaxOffset ----
static int testUnits() {
  CHECK(clampAxis(5, 16, 64) == 5);
  CHECK(clampAxis(-3, 16, 64) == 0);
  CHECK(clampAxis(-1000, 16, 64) == 0);
  CHECK(clampAxis(48, 16, 64) == 48);
  CHECK(clampAxis(49, 16, 64) == 48);
  CHECK(clampAxis(60, 16, 64) == 48);
  CHECK(clampAxis(0, 16, 17) == 0 && clampAxis(5, 16, 17) == 1);
  CHECK(clampAxis(5, 16, 16) == 0 && clampAxis(-5, 16, 16) == 0);  // world == view
  CHECK(clampAxis(0, 16, 10) == -3 && clampAxis(99, 16, 10) == -3);  // even difference: centred
  CHECK(clampAxis(0, 16, 9) == -4 && clampAxis(-99, 16, 9) == -4);   // odd difference: floor(-3.5)
  CHECK(clampAxis(0, 16, 0) == -8);

  Camera cam{0, 0, 16, 12, 32, 24};
  cam.moveTo(5, 6);
  CHECK(cam.x == 5 && cam.y == 6);
  cam.moveTo(-4, 100);
  CHECK(cam.x == 0 && cam.y == 12);
  cam.moveTo(99, -1);
  CHECK(cam.x == 16 && cam.y == 0);
  cam.centerOn(16, 12);
  CHECK(cam.x == 8 && cam.y == 6);
  cam.centerOn(0, 0);
  CHECK(cam.x == 0 && cam.y == 0);
  cam.centerOn(31, 23);
  CHECK(cam.x == 16 && cam.y == 12);
  cam.x = 99;
  cam.y = -99;
  cam.clamp();
  CHECK(cam.x == 16 && cam.y == 0);

  Camera flat{0, 0, 16, 12, 32, 8};  // world shorter than the view on y only
  flat.moveTo(5, 50);
  CHECK(flat.x == 5 && flat.y == -2);
  flat.centerOn(20, 4);
  CHECK(flat.x == 12 && flat.y == -2);
  flat.world_h = 7;
  flat.clamp();
  CHECK(flat.y == -3);

  const int cams[] = {0, 3, 7, 8, -3, -4, -7, -8, 1000};
  for (int c : cams) {
    CHECK(parallaxOffset(c, 0) == 0);
    CHECK(parallaxOffset(c, 256) == c);
    CHECK(parallaxOffset(c, 512) == 2 * c);
    CHECK(parallaxOffset(c, 64) == (int)std::floor(c / 4.0));
    CHECK(parallaxOffset(c, 128) == (int)std::floor(c / 2.0));
    CHECK(parallaxOffset(c, 384) == (int)std::floor(c * 1.5));
  }
  CHECK(parallaxOffset(-3, 128) == -2);  // floor, not truncation toward zero
  CHECK(parallaxOffset(-7, 64) == -2);
  CHECK(parallaxOffset(2000000000, 128) == 1000000000);  // 64-bit intermediate
  return 0;
}

// ---- 2. golden: far opaque layer (parallax 1/2) under a near transparent layer, 4x4 tiles on a 16x12 canvas ----
enum { GW = 16, GH = 12, MW = 8, MH = 6 };
static const Color kFarPal[2] = {0x10, 0x11};      // '.' '-'
static const Color kNearPal[3] = {0x20, 0x21, 0x22};  // (transparent) '#' 'o'
static uint8_t g_farPx[2 * 16];
static const uint8_t kNearPx[3 * 16] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // tile 0: empty
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // tile 1: solid
    1, 2, 0, 0, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0,  // tile 2: asymmetric
};
static uint8_t g_farMap[MW * MH];
static const uint8_t kNearMap[MW * MH] = {
    2, 0, 0, 0, 0, 0, 0, 0,
    0, 1, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 2, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 1, 0, 0,
    0, 0, 0, 0, 0, 0, 2, 0,
    0, 0, 0, 0, 1, 0, 0, 0,
};

static const char* const kGoldenA =  // camera (0, 0)
    "#o--....----....\n"
    "##--....----....\n"
    "####....----....\n"
    "----....----....\n"
    "....####....----\n"
    "....####....----\n"
    "....####....----\n"
    "....####....----\n"
    "----....----#o..\n"
    "----....----##..\n"
    "----....----####\n"
    "----....----....\n";
static const char* const kGoldenB =  // moveTo(100, 100) clamps to the world edge (16, 12)
    "....####....----\n"
    "....####....----\n"
    "----####----....\n"
    "----####----....\n"
    "----....#o--....\n"
    "----....##--....\n"
    "....----####----\n"
    "....----....----\n"
    "####----....----\n"
    "####----....----\n"
    "####....----....\n"
    "####....----....\n";

static int testGolden() {
  for (int i = 0; i < 16; i++) g_farPx[16 + i] = 1;  // far tile 1 solid, tile 0 stays 0
  for (int ty = 0; ty < MH; ty++)
    for (int tx = 0; tx < MW; tx++) g_farMap[ty * MW + tx] = (tx + ty) % 2 == 0 ? 1 : 0;
  const Tileset far_ts{g_farPx, kFarPal, 4, 4, false}, near_ts{kNearPx, kNearPal, 4, 4, true};
  const MapLayer layers[2] = {{{g_farMap, MW, MH, &far_ts}, 128, 128}, {{kNearMap, MW, MH, &near_ts}}};
  Camera cam{0, 0, GW, GH, MW * 4, MH * 4};
  const char* want[2] = {kGoldenA, kGoldenB};
  for (int k = 0; k < 2; k++) {
    if (k == 0) cam.moveTo(0, 0);
    else cam.moveTo(100, 100);
    CHECK(k == 0 || (cam.x == 16 && cam.y == 12));
    static Color buf[GW * GH];
    Canvas c(buf, GW, GH, 0, GH, 0);
    c.clear(kClear);
    drawLayers(c, cam, layers, 2);
    char art[GW * GH + GH + 1], *o = art;
    for (int y = 0; y < GH; y++) {
      for (int x = 0; x < GW; x++) {
        Color v = buf[y * GW + x];
        *o++ = v == 0x10 ? '.' : v == 0x11 ? '-' : v == 0x21 ? '#' : v == 0x22 ? 'o' : '?';
      }
      *o++ = '\n';
    }
    *o = 0;
    if (std::strcmp(art, want[k]) != 0) std::printf("golden %d differs, got:\n%s", k, art);
    CHECK(std::strcmp(art, want[k]) == 0);
  }
  // null / empty input draws nothing
  static Color buf[GW * GH];
  for (Color& v : buf) v = kClear;
  Canvas c(buf, GW, GH, 0, GH, 0);
  drawLayers(c, cam, nullptr, 2);
  drawLayers(c, cam, layers, 0);
  drawLayers(c, cam, layers, -1);
  for (Color v : buf) CHECK(v == kClear);
  return 0;
}

// ---- 3. three layers (opaque back, two transparent, parallax factors 0.5/0.25, 1, 2/0.75): bands and reference ----
enum { RW = 16, RH = 12, LW = 9, LH = 7, NL = 3 };
static uint8_t g_px[NL][4 * 16];
static Color g_pal[NL][4];
static uint8_t g_map[NL][LW * LH];
static Tileset g_ts[NL];
static MapLayer g_layers[NL];
static const int kFx[NL] = {128, 256, 512}, kFy[NL] = {64, 256, 192};

static void buildLayers() {
  for (int l = 0; l < NL; l++) {
    for (int t = 0; t < 4; t++)
      for (int i = 0; i < 16; i++) g_px[l][t * 16 + i] = (uint8_t)((t * 5 + i * 3 + i / 4 + l) % (l == 0 ? 4 : 3));
    for (int i = 0; i < 4; i++) g_pal[l][i] = (Color)(0x100 * (l + 1) + i);
    for (int i = 0; i < LW * LH; i++) g_map[l][i] = (uint8_t)((i * 7 + i / LW + l * 3) % 4);
    g_ts[l] = Tileset{g_px[l], g_pal[l], 4, 4, l != 0};
    g_layers[l] = MapLayer{{g_map[l], LW, LH, &g_ts[l]}, (uint16_t)kFx[l], (uint16_t)kFy[l]};
  }
}

static void drawJob(Canvas& c, void* p) { drawLayers(c, *static_cast<const Camera*>(p), g_layers, NL); }

static int fl(int v, int d) { return (int)std::floor((double)v / d); }

// Pixel of the scene at screen (sx, sy), computed straight from the tile / pixel formula. The screen is
// cam.view_w x cam.view_h; (u, v) is the view-relative world offset that the turn put at (sx, sy).
static Color refPixel(const Camera& cam, int sx, int sy) {
  int u, v;
  invTurn(cam.orient, sx, sy, cam.view_w, cam.view_h, u, v);
  Color out = kClear;
  for (int l = 0; l < NL; l++) {
    const int wx = u + (int)std::floor((double)cam.x * kFx[l] / 256.0);
    const int wy = v + (int)std::floor((double)cam.y * kFy[l] / 256.0);
    const int tx = fl(wx, 4), ty = fl(wy, 4);
    if (tx < 0 || tx >= LW || ty < 0 || ty >= LH) continue;
    const int idx = g_px[l][g_map[l][ty * LW + tx] * 16 + (wy - ty * 4) * 4 + (wx - tx * 4)];
    if (l != 0 && idx == 0) continue;
    out = g_pal[l][idx];
  }
  return out;
}

// Every orient 0..3; the screen is 16 x 12 for even orient and 12 x 16 for odd orient.
static int testBandsAndReference() {
  buildLayers();
  for (int o = 0; o < 4; o++) {  // invTurn (used by refPixel) agrees with the corner rule, hence with the contract numbers
    const int sw = (o & 1) ? RH : RW, sh = (o & 1) ? RW : RH, vw = (o & 1) ? sh : sw, vh = (o & 1) ? sw : sh;
    for (int sy = 0; sy < sh; sy++)
      for (int sx = 0; sx < sw; sx++) {
        int u, v;
        invTurn(o, sx, sy, sw, sh, u, v);
        CHECK(u >= 0 && u < vw && v >= 0 && v < vh);
        CHECK(sameRect(turnRef(Rect{u, v, 1, 1}, vw, vh, o), Rect{sx, sy, 1, 1}));
      }
  }
  static const int pos[][2] = {{0, 0},   {1, 1},    {3, 5},    {4, 4},   {7, -3},   {-5, 2},  {-40, -40},
                               {100, 100}, {16, 12}, {33, 9},   {-1, -1}, {63, 41},  {12, -17}, {200, -300},
                               {8, 8},   {-8, -8}};
  static Color band[RW * RH], full[RW * RH], ref[RW * RH];
  for (int o = 0; o < 4; o++) {
    const int sw = (o & 1) ? RH : RW, sh = (o & 1) ? RW : RH;
    const int bands[] = {sh, 5, 3, 1};  // the full frame first
    for (const auto& p : pos) {
      Camera cam{p[0], p[1], sw, sh, 0, 0};
      cam.orient = (uint8_t)o;
      for (int y = 0; y < sh; y++)
        for (int x = 0; x < sw; x++) ref[y * sw + x] = refPixel(cam, x, y);
      for (int b : bands) {
        mockReset(sw, sh);
        RenderConfig cfg = {(uint16_t)sw, (uint16_t)sh, (uint16_t)b, kClear};
        CHECK(renderFrame(cfg, band, RW * RH, 0, drawJob, &cam) == 0);
        if (b == sh) std::memcpy(full, g_fb, sizeof full);
        char tag[96];
        std::snprintf(tag, sizeof tag, "orient=%d cam=(%d,%d) band=%d vs full", o, p[0], p[1], b);
        CHECK(samePic(tag, full, g_fb, sw, sh));
        std::snprintf(tag, sizeof tag, "orient=%d cam=(%d,%d) band=%d vs reference", o, p[0], p[1], b);
        CHECK(samePic(tag, ref, g_fb, sw, sh));
      }
    }
  }
  return 0;
}


// ---- 4. orientation units: turnRect, Camera with orient, toScreen, spriteFlags, orientForGravity ----
static int testOrientUnits() {
  // (a) turnRect: hand-checked numbers, a 3 x 2 rect at (1, 4) in a 10 x 6 view
  const Rect r{1, 4, 3, 2};
  CHECK(sameRect(turnRect(r, 10, 6, 0), r) && sameRect(turnRect(r, 10, 6, 4), r));
  CHECK(sameRect(turnRect(r, 10, 6, 1), Rect{0, 1, 2, 3}));
  CHECK(sameRect(turnRect(r, 10, 6, 2), Rect{6, 0, 3, 2}));
  CHECK(sameRect(turnRect(r, 10, 6, 3), Rect{4, 6, 2, 3}));
  CHECK(sameRect(turnRect(r, 10, 6, 5), Rect{0, 1, 2, 3}));  // the turn count is masked
  for (int k = 1; k < 4; k++)  // the corner rule gives the same numbers
    CHECK(sameRect(turnRef(r, 10, 6, k), turnRect(r, 10, 6, k)));
  // corner pixels of the 10 x 6 view: top-left -> top-right -> bottom-right -> bottom-left -> top-left
  const Rect tl{0, 0, 1, 1}, tr{9, 0, 1, 1}, br{9, 5, 1, 1}, bl{0, 5, 1, 1};
  CHECK(sameRect(turnRect(tl, 10, 6, 1), Rect{5, 0, 1, 1}));  // top-right of the 6 x 10 result
  CHECK(sameRect(turnRect(tr, 10, 6, 1), Rect{5, 9, 1, 1}));  // bottom-right
  CHECK(sameRect(turnRect(br, 10, 6, 1), Rect{0, 9, 1, 1}));  // bottom-left
  CHECK(sameRect(turnRect(bl, 10, 6, 1), Rect{0, 0, 1, 1}));  // top-left
  CHECK(sameRect(turnRect(tl, 10, 6, 2), br));
  CHECK(sameRect(turnRect(tl, 10, 6, 3), Rect{0, 9, 1, 1}));  // one turn the other way: bottom-left
  CHECK(sameRect(turnRect(Rect{0, 0, 10, 6}, 10, 6, 1), Rect{0, 0, 6, 10}));
  // properties and the corner-pixel reference over a grid of views and rects
  static const int dims[][2] = {{10, 6}, {7, 7}, {1, 5}, {16, 12}};
  for (const auto& d : dims)
    for (int y = 0; y < d[1]; y++)
      for (int x = 0; x < d[0]; x++)
        for (int h = 1; y + h <= d[1]; h++)
          for (int w = 1; x + w <= d[0]; w++) {
            const Rect q{x, y, w, h};
            for (int k = 0; k < 8; k++) {
              const Rect t = turnRect(q, d[0], d[1], k);
              CHECK(sameRect(t, turnRef(q, d[0], d[1], k)));
              CHECK(sameRect(turnRect(t, (k & 1) ? d[1] : d[0], (k & 1) ? d[0] : d[1], 4 - (k & 3)), q));  // k then 4 - k
            }
            Rect t = q;
            int a = d[0], b = d[1];
            for (int i = 0; i < 4; i++) {  // four single turns, the view dims swap each time
              t = turnRect(t, a, b, 1);
              const int s = a;
              a = b;
              b = s;
            }
            CHECK(sameRect(t, q));
            CHECK(sameRect(turnRect(turnRect(q, d[0], d[1], 1), d[1], d[0], 1), turnRect(q, d[0], d[1], 2)));
          }

  // (b) Camera: visW / visH and clamp / centerOn use the swapped sizes; view 16 x 12 in a 40 x 30 world
  Camera cam{0, 0, 16, 12, 40, 30};
  CHECK(cam.orient == 0 && cam.visW() == 16 && cam.visH() == 12);
  cam.moveTo(100, 100);
  CHECK(cam.x == 24 && cam.y == 18);
  for (int o = 0; o < 4; o++) {
    const bool odd = (o & 1) != 0;
    cam.orient = (uint8_t)o;
    CHECK(cam.visW() == (odd ? 12 : 16) && cam.visH() == (odd ? 16 : 12));
    const int mx = odd ? 28 : 24, my = odd ? 14 : 18;  // 40 - visW, 30 - visH
    cam.moveTo(100, 100);
    CHECK(cam.x == mx && cam.y == my);
    cam.moveTo(-5, -5);
    CHECK(cam.x == 0 && cam.y == 0);
    cam.x = 99;
    cam.y = 99;
    cam.clamp();
    CHECK(cam.x == mx && cam.y == my);
    cam.centerOn(20, 15);
    CHECK(cam.x == (odd ? 14 : 12) && cam.y == (odd ? 7 : 9));  // 20 - visW / 2, 15 - visH / 2
    cam.centerOn(39, 29);
    CHECK(cam.x == mx && cam.y == my);
  }
  cam.orient = 5;  // only the low 2 bits count
  CHECK(cam.visW() == 12 && cam.visH() == 16);
  cam.orient = 6;
  CHECK(cam.visW() == 16 && cam.visH() == 12);
  Camera narrow{0, 0, 16, 12, 10, 30};  // world narrower than the visible width: centred, floor((world - vis) / 2)
  narrow.moveTo(5, 50);
  CHECK(narrow.x == -3 && narrow.y == 18);
  narrow.orient = 1;
  narrow.moveTo(5, 50);
  CHECK(narrow.x == -1 && narrow.y == 14);

  // (c) toScreen: camera at world (10, 20); world rect (13, 24, 3, 2) is (3, 4, 3, 2) from the camera corner
  Camera tc{10, 20, 16, 12, 64, 48};
  const Rect wr{13, 24, 3, 2};
  const Rect want[4] = {{3, 4, 3, 2}, {10, 3, 2, 3}, {10, 6, 3, 2}, {4, 6, 2, 3}};
  const Rect corner[4] = {{0, 0, 1, 1}, {15, 0, 1, 1}, {15, 11, 1, 1}, {0, 11, 1, 1}};  // TL -> TR -> BR -> BL
  for (int o = 0; o < 4; o++) {
    tc.orient = (uint8_t)o;
    CHECK(sameRect(tc.toScreen(wr), want[o]));
    CHECK(sameRect(turnRef(Rect{3, 4, 3, 2}, (o & 1) ? 12 : 16, (o & 1) ? 16 : 12, o), want[o]));
    CHECK(sameRect(tc.toScreen(Rect{10, 20, 1, 1}), corner[o]));  // the camera's own top-left world pixel
  }

  // (d) spriteFlags: the flips stay, the rotation field gains orient
  Camera sf{0, 0, 16, 12, 40, 30};
  CHECK(sf.spriteFlags(None) == None && sf.spriteFlags(FlipX | Rot90) == (FlipX | Rot90));  // orient 0
  sf.orient = 3;
  CHECK(sf.spriteFlags(None) == Rot270);
  sf.orient = 1;
  CHECK(sf.spriteFlags(Rot270) == None);
  CHECK(sf.spriteFlags(Rot90) == Rot180);
  CHECK(sf.spriteFlags(FlipX | Rot90) == (FlipX | Rot180));
  CHECK(sf.spriteFlags(FlipY) == (FlipY | Rot90));
  sf.orient = 2;
  CHECK(sf.spriteFlags(Rot180) == None);
  for (int o = 0; o < 4; o++)
    for (int fl = 0; fl < 4; fl++)
      for (int rot = 0; rot < 4; rot++) {
        sf.orient = (uint8_t)o;
        CHECK(sf.spriteFlags(static_cast<SpriteFlags>(fl | (rot << 2))) ==
              static_cast<SpriteFlags>(fl | (((rot + o) & 3) << 2)));
      }

  // (e) gravity: the picture is turned so that gravity points down on screen
  CHECK(orientForGravity(GravityDir::Down) == 0 && orientForGravity(GravityDir::Right) == 1);
  CHECK(orientForGravity(GravityDir::Up) == 2 && orientForGravity(GravityDir::Left) == 3);
  return 0;
}

// ---- 5. the golden parallax scene turned: orient k on a swapped canvas == the orient 0 picture turned by k ----
static char goldenGlyph(Color v) { return v == 0x10 ? '.' : v == 0x11 ? '-' : v == 0x21 ? '#' : v == 0x22 ? 'o' : '?'; }

// The two-layer golden scene at camera (cx, cy) (clamped) on a 16 x 12 canvas, 12 x 16 for odd orient; returns the camera.
static Camera renderGolden(int orient, int cx, int cy, Color* buf) {
  for (int i = 0; i < 16; i++) g_farPx[16 + i] = 1;  // same set-up as testGolden
  for (int ty = 0; ty < MH; ty++)
    for (int tx = 0; tx < MW; tx++) g_farMap[ty * MW + tx] = (tx + ty) % 2 == 0 ? 1 : 0;
  const Tileset far_ts{g_farPx, kFarPal, 4, 4, false}, near_ts{kNearPx, kNearPal, 4, 4, true};
  const MapLayer layers[2] = {{{g_farMap, MW, MH, &far_ts}, 128, 128}, {{kNearMap, MW, MH, &near_ts}}};
  const int sw = (orient & 1) ? GH : GW, sh = (orient & 1) ? GW : GH;
  Camera cam{0, 0, sw, sh, MW * 4, MH * 4};
  cam.orient = (uint8_t)orient;
  cam.moveTo(cx, cy);
  Canvas c(buf, sw, sh, 0, sh, 0);
  c.clear(kClear);
  drawLayers(c, cam, layers, 2);
  return cam;
}

static int testTurnedGolden() {
  static Color p[GW * GH], q[GW * GH], want[GW * GH];
  const int cams[2][2] = {{0, 0}, {100, 100}};  // the second clamps to the far corner (16, 12)
  for (int k = 1; k < 4; k++)
    for (const auto& cp : cams) {
      const Camera pc = renderGolden(0, cp[0], cp[1], p), qc = renderGolden(k, cp[0], cp[1], q);
      CHECK(pc.x == qc.x && pc.y == qc.y);
      CHECK(cp[0] == 0 ? (qc.x == 0 && qc.y == 0) : (qc.x == 16 && qc.y == 12));
      const int qw = (k & 1) ? GH : GW, qh = (k & 1) ? GW : GH;
      if (k == 1 && cp[0] == 0) {  // for the human eye: the golden scene turned once, camera (0, 0)
        std::printf("orient 1, camera (0,0), %dx%d:\n", qw, qh);
        for (int y = 0; y < qh; y++) {
          for (int x = 0; x < qw; x++) std::putchar(goldenGlyph(q[y * qw + x]));
          std::putchar('\n');
        }
      }
      turnPic(p, GW, GH, k, want);
      char tag[64];
      std::snprintf(tag, sizeof tag, "turned golden k=%d cam=(%d,%d)", k, qc.x, qc.y);
      CHECK(samePic(tag, want, q, qw, qh));
    }
  return 0;
}

// ---- 6. gravity anchor: orientForGravity(g) puts the strip on the edge gravity pulls toward at the screen bottom ----
static int testGravityAnchor() {
  static const Color pal[2] = {0x30, 0x31};  // air, solid
  static uint8_t px[2 * 16];
  for (int i = 0; i < 16; i++) px[16 + i] = 1;
  const Tileset ts{px, pal, 4, 4, false};
  const GravityDir dirs[4] = {GravityDir::Down, GravityDir::Left, GravityDir::Up, GravityDir::Right};
  const int mark[4][2] = {{2, 1}, {1, 0}, {0, 2}, {2, 3}};  // marker tile (tx, ty): ends in the middle screen rows
  static Color world[16 * 16], want[16 * 16], got[16 * 16];
  for (int i = 0; i < 4; i++) {
    uint8_t tiles[16] = {};
    for (int t = 0; t < 4; t++) switch (dirs[i]) {  // the one-tile strip on the world edge gravity pulls toward
        case GravityDir::Down: tiles[12 + t] = 1; break;
        case GravityDir::Left: tiles[t * 4] = 1; break;
        case GravityDir::Up: tiles[t] = 1; break;
        case GravityDir::Right: tiles[t * 4 + 3] = 1; break;
      }
    tiles[mark[i][1] * 4 + mark[i][0]] = 1;
    for (int y = 0; y < 16; y++)
      for (int x = 0; x < 16; x++) world[y * 16 + x] = pal[tiles[(y / 4) * 4 + x / 4]];
    Camera cam{0, 0, 16, 16, 16, 16};
    cam.orient = orientForGravity(dirs[i]);
    const MapLayer ml{{tiles, 4, 4, &ts}};
    Canvas c(got, 16, 16, 0, 16, 0);
    c.clear(kClear);
    drawLayers(c, cam, &ml, 1);
    int middle = 0;
    for (int y = 0; y < 16; y++)
      for (int x = 0; x < 16; x++) {
        if (y < 4) CHECK(got[y * 16 + x] == pal[0]);    // top four rows: air
        if (y >= 12) CHECK(got[y * 16 + x] == pal[1]);  // bottom four rows: solid
        if (y >= 4 && y < 12) middle += got[y * 16 + x] == pal[1];
      }
    CHECK(middle == 16);  // only the marker tile is solid in between
    turnPic(world, 16, 16, cam.orient, want);
    char tag[64];
    std::snprintf(tag, sizeof tag, "gravity case %d (orient %d)", i, cam.orient);
    CHECK(samePic(tag, want, got, 16, 16));
  }
  return 0;
}

// ---- 7. a sprite drawn at toScreen(tile box) with spriteFlags() equals the tile drawn by the tilemap ----
static int testSpriteEqualsTile() {
  static const uint8_t img[16] = {1, 2, 2, 0, 3, 0, 1, 0, 3, 3, 0, 2, 1, 0, 0, 3};  // asymmetric, index 0 transparent
  static const Color pal[4] = {0x40, 0x41, 0x42, 0x43};
  static uint8_t two[32], mir[32], tiles[20];  // tile 0 stays empty; tile 1 = the image / its mirror image
  for (int y = 0; y < 4; y++)
    for (int x = 0; x < 4; x++) {
      two[16 + y * 4 + x] = img[y * 4 + x];
      mir[16 + y * 4 + x] = img[y * 4 + 3 - x];
    }
  tiles[2 * 5 + 1] = 1;  // the tile at (1, 2) of a 5 x 4 tile world (20 x 16 px)
  const Tileset ts{two, pal, 4, 4, true}, tsm{mir, pal, 4, 4, true};
  const SpriteSheet sp{img, pal, 4, 4, 1, true};
  int opaque = 0;
  for (uint8_t v : img) opaque += v != 0;
  static Color a[20 * 16], b[20 * 16];
  for (int o = 0; o < 4; o++) {
    const int sw = (o & 1) ? 16 : 20, sh = (o & 1) ? 20 : 16;
    Camera cam{0, 0, sw, sh, 20, 16};
    cam.orient = (uint8_t)o;
    CHECK(cam.visW() == 20 && cam.visH() == 16);
    const Rect box = cam.toScreen(Rect{4, 8, 4, 4});
    for (int mirrored = 0; mirrored < 2; mirrored++) {
      const MapLayer ml{{tiles, 5, 4, mirrored ? &tsm : &ts}};
      Canvas ca(a, sw, sh, 0, sh, 0), cb(b, sw, sh, 0, sh, 0);
      ca.clear(kClear);
      cb.clear(kClear);
      drawLayers(ca, cam, &ml, 1);
      cb.sprite(sp, 0, box.x, box.y, cam.spriteFlags(mirrored ? FlipX : None));
      int drawn = 0;
      for (int i = 0; i < sw * sh; i++) drawn += a[i] != kClear;
      CHECK(drawn == opaque);  // the tile really is on screen, all of it
      char tag[64];
      std::snprintf(tag, sizeof tag, "sprite vs tile o=%d mirrored=%d", o, mirrored);
      CHECK(samePic(tag, a, b, sw, sh));
    }
  }
  return 0;
}

// ---- 8. four flips x four rotations of an asymmetric sprite give exactly 8 images, none needing a transpose ----
static int testEightOrientations() {
  static const uint8_t px[6] = {0, 1, 2, 3, 4, 5};
  static const Color pal[6] = {0x50, 0x51, 0x52, 0x53, 0x54, 0x55};
  const SpriteSheet sp{px, pal, 3, 2, 1, false};  // opaque, all six pixels differ (index 0 is drawn too)
  static Color img[16][16], want[16];
  for (int fl = 0; fl < 4; fl++)
    for (int r = 0; r < 4; r++) {
      Canvas c(img[fl * 4 + r], 4, 4, 0, 4, 0);
      c.clear(kClear);
      c.sprite(sp, 0, 0, 0, static_cast<SpriteFlags>(fl | (r << 2)));
      Color m[6], t[6];  // model: flips first (FlipX = 1, FlipY = 2), then r clockwise quarter turns
      for (int y = 0; y < 2; y++)
        for (int x = 0; x < 3; x++) m[y * 3 + x] = pal[px[((fl & 2) ? 1 - y : y) * 3 + ((fl & 1) ? 2 - x : x)]];
      turnPic(m, 3, 2, r, t);
      for (Color& v : want) v = kClear;
      const int tw = (r & 1) ? 2 : 3, th = (r & 1) ? 3 : 2;
      for (int y = 0; y < th; y++)
        for (int x = 0; x < tw; x++) want[y * 4 + x] = t[y * tw + x];
      char tag[64];
      std::snprintf(tag, sizeof tag, "sprite flips=%d rot=%d", fl, r);
      CHECK(samePic(tag, want, img[fl * 4 + r], 4, 4));
    }
  int distinct = 0;
  for (int i = 0; i < 16; i++) {
    bool fresh = true, reach = false;
    for (int j = 0; j < i; j++) fresh = fresh && std::memcmp(img[i], img[j], sizeof img[i]) != 0;
    for (int j = 0; j < 8; j++) reach = reach || std::memcmp(img[i], img[j], sizeof img[i]) == 0;  // FlipX-or-nothing + rotation
    distinct += fresh;
    CHECK(reach);
  }
  CHECK(distinct == 8);
  return 0;
}

// ---- 9. tilemapRotated: any-angle draw of a tile layer ----
// Layer: 5 x 4 tiles of 4 x 4 px (20 x 16 px). Tile t pixel i has index t * 16 + i, so every layer pixel differs;
// tile 0 instead has index 0 on every fifth pixel (4 per cell), so a transparent tileset has holes.
enum { RTW = 5, RTH = 4, RPW = 20, RPH = 16, RTILES = 6 };
static uint8_t g_rpx[RTILES * 16];
static Color g_rpal[256];
static uint8_t g_rmap[RTW * RTH];
static Tileset g_rts, g_rtsT;  // opaque and transparent

static void buildRotLayer() {
  for (int t = 0; t < RTILES; t++)
    for (int i = 0; i < 16; i++) g_rpx[t * 16 + i] = (uint8_t)(t == 0 && i % 5 == 0 ? 0 : t * 16 + i);
  for (int i = 0; i < 256; i++) g_rpal[i] = (Color)(0x300 + i);
  for (int i = 0; i < RTW * RTH; i++) g_rmap[i] = (uint8_t)((i * 7 + i / RTW) % RTILES);
  g_rts = Tileset{g_rpx, g_rpal, 4, 4, false};
  g_rtsT = Tileset{g_rpx, g_rpal, 4, 4, true};
}

// Layer pixel (x, y), 0 <= x < RPW, 0 <= y < RPH, as an index
static int layerIdx(int x, int y) { return g_rpx[g_rmap[(y / 4) * RTW + x / 4] * 16 + (y % 4) * 4 + x % 4]; }

struct RotArgs { TileLayer l; int ax, ay, sx, sy, c, s; };
static void rotJob(Canvas& cv, void* p) {
  const RotArgs& a = *static_cast<const RotArgs*>(p);
  cv.tilemapRotated(a.l, a.ax, a.ay, a.sx, a.sy, a.c, a.s);
}
struct MapArgs { TileLayer l; int cx, cy, o; };
static void mapJob(Canvas& cv, void* p) {
  const MapArgs& a = *static_cast<const MapArgs*>(p);
  cv.tilemap(a.l, a.cx, a.cy, (uint8_t)a.o);
}

// One frame of w x h through renderFrame with bands of "band" rows, into out.
static int renderTo(Color* out, int w, int h, int band, RenderFn fn, void* ctx) {
  static Color bb[MAXPX];
  mockReset(w, h);
  RenderConfig cfg = {(uint16_t)w, (uint16_t)h, (uint16_t)band, kClear};
  if (renderFrame(cfg, bb, MAXPX, 0, fn, ctx) != 0) return 1;
  std::memcpy(out, g_fb, (size_t)w * h * sizeof(Color));
  return 0;
}

// Anchor that makes angle o * 90 degrees equal tilemap(cam, orient o) on a sw x sh screen with the anchor at (sx, sy).
// tilemap shows the world pixel (cam + (u, v)) at screen pixel (px, py), (u, v) = invTurn: o1 (py, sw - 1 - px),
// o2 (sw - 1 - px, sh - 1 - py), o3 (sh - 1 - py, px). tilemapRotated samples A + R^-1 (P + 1/2 - S) with the clockwise
// turn R^-1 (dx, dy) = (dx c + dy s, -dx s + dy c): o1 (c, s) = (0, 1) gives (dy, -dx), o2 (-1, 0) gives (-dx, -dy),
// o3 (0, -1) gives (-dy, dx). Matching the two, term by term, gives these anchors.
static void anchorFor(int o, int cam_x, int cam_y, int sx, int sy, int sw, int sh, int& ax, int& ay) {
  switch (o & 3) {
    case 0: ax = cam_x + sx; ay = cam_y + sy; break;
    case 1: ax = cam_x + sy; ay = cam_y + sw - sx; break;
    case 2: ax = cam_x + sw - sx; ay = cam_y + sh - sy; break;
    default: ax = cam_x + sh - sy; ay = cam_y + sx; break;
  }
}

static int testRotatedCardinal() {
  buildRotLayer();
  static const int cs[4][2] = {{16384, 0}, {0, 16384}, {-16384, 0}, {0, -16384}};
  static const int pos[][4] = {{0, 0, 0, 0}, {3, -2, 5, 4},  {-6, 7, -3, 2},
                              {0, 0, 9, 1}, {20, 16, 0, 0}, {-30, 40, 7, 7}};
  static Color want[MAXPX], got[MAXPX];
  for (int o = 0; o < 4; o++) {
    const int sw = (o & 1) ? 16 : 20, sh = (o & 1) ? 20 : 16;
    for (const auto& p : pos) {
      int ax, ay;
      anchorFor(o, p[0], p[1], p[2], p[3], sw, sh, ax, ay);
      for (const Tileset* ts : {&g_rts, &g_rtsT}) {
        MapArgs m{TileLayer{g_rmap, RTW, RTH, ts}, p[0], p[1], o};
        RotArgs r{TileLayer{g_rmap, RTW, RTH, ts}, ax, ay, p[2], p[3], cs[o][0], cs[o][1]};
        CHECK(renderTo(want, sw, sh, sh, mapJob, &m) == 0);
        CHECK(renderTo(got, sw, sh, sh, rotJob, &r) == 0);
        char tag[96];
        std::snprintf(tag, sizeof tag, "rotated %d deg vs tilemap orient %d cam=(%d,%d) S=(%d,%d) transparent=%d",
                      o * 90, o, p[0], p[1], p[2], p[3], ts == &g_rtsT);
        CHECK(samePic(tag, want, got, sw, sh));
      }
    }
  }
  return 0;
}

// Straight from the contract in doubles: the layer point under the centre of screen pixel P is A + R^-1 (P + 1/2 - S).
// All numbers are dyadic and small, so the doubles are exact and floor() agrees with the engine bit for bit.
static void rotRef(const Tileset& ts, int ax, int ay, int sx, int sy, int c, int s, int w, int h, Color* out) {
  const double cc = c / 16384.0, ss = s / 16384.0;
  for (int py = 0; py < h; py++)
    for (int px = 0; px < w; px++) {
      const double dx = px + 0.5 - sx, dy = py + 0.5 - sy;
      const int lx = (int)std::floor(ax + dx * cc + dy * ss), ly = (int)std::floor(ay - dx * ss + dy * cc);
      Color v = kClear;
      if (lx >= 0 && lx < RPW && ly >= 0 && ly < RPH) {
        const int idx = layerIdx(lx, ly);
        if (idx || !ts.transparent) v = g_rpal[idx];
      }
      out[py * w + px] = v;
    }
}

enum { RSW = 32, RSH = 24 };

static int testRotatedReference() {
  static const int cs[][2] = {{9830, 13107},  {13107, 9830},  {-9830, 13107}, {-13107, -9830}, {9830, -13107},
                              {16384, 0},     {0, -16384},    {12, -16383},   {-16383, 12},    {11585, 11585}};
  static const int pos[][4] = {{10, 8, 16, 12}, {0, 0, 20, 3}, {25, -4, 5, 30}, {10, 8, -6, 9}, {200, 150, 3, 3}};
  static Color want[MAXPX], got[MAXPX];
  int hit = 0, miss = 0;  // frames that draw something / that miss the layer completely
  for (const Tileset* ts : {&g_rts, &g_rtsT})
    for (const auto& a : cs)
      for (const auto& p : pos) {
        RotArgs r{TileLayer{g_rmap, RTW, RTH, ts}, p[0], p[1], p[2], p[3], a[0], a[1]};
        rotRef(*ts, p[0], p[1], p[2], p[3], a[0], a[1], RSW, RSH, want);
        CHECK(renderTo(got, RSW, RSH, RSH, rotJob, &r) == 0);
        char tag[96];
        std::snprintf(tag, sizeof tag, "rotated (%d,%d) A=(%d,%d) S=(%d,%d) transparent=%d", a[0], a[1], p[0], p[1],
                      p[2], p[3], ts == &g_rtsT);
        CHECK(samePic(tag, want, got, RSW, RSH));
        int drawn = 0;
        for (int i = 0; i < RSW * RSH; i++) drawn += got[i] != kClear;
        hit += drawn > 0;
        miss += drawn == 0;
      }
  CHECK(hit >= 60 && miss >= 20);  // the anchor {200, 150} is far outside the layer, so some frames draw nothing
  return 0;
}

static int testRotatedBands() {
  const int c = 9830, s = 13107;  // (3, 4) / 5 in Q14
  RotArgs r{TileLayer{g_rmap, RTW, RTH, &g_rtsT}, 10, 8, 16, 12, c, s};
  static Color full[MAXPX], got[MAXPX];
  CHECK(renderTo(full, RSW, RSH, RSH, rotJob, &r) == 0);
  int drawn = 0;
  for (int i = 0; i < RSW * RSH; i++) drawn += full[i] != kClear;
  CHECK(drawn > 100 && drawn < RSW * RSH);
  for (int b : {1, 7, 16}) {
    CHECK(renderTo(got, RSW, RSH, b, rotJob, &r) == 0);
    char tag[64];
    std::snprintf(tag, sizeof tag, "rotated band=%d vs full", b);
    CHECK(samePic(tag, full, got, RSW, RSH));
  }
  return 0;
}

static int testRotatedEdges() {
  static Color got[MAXPX], want[MAXPX];
  // transparent: the angle-0 picture at S == A is the layer itself; index 0 leaves the clear colour, opaque draws it
  for (const Tileset* ts : {&g_rts, &g_rtsT}) {
    RotArgs r{TileLayer{g_rmap, RTW, RTH, ts}, 0, 0, 0, 0, 16384, 0};
    CHECK(renderTo(got, RPW, RPH, RPH, rotJob, &r) == 0);
    int holes = 0;
    for (int y = 0; y < RPH; y++)
      for (int x = 0; x < RPW; x++) {
        const int idx = layerIdx(x, y);
        const bool hole = ts->transparent && idx == 0;
        holes += hole;
        CHECK(got[y * RPW + x] == (hole ? kClear : g_rpal[idx]));
      }
    CHECK(ts->transparent ? holes > 0 : holes == 0);
  }
  // outside: the 20 x 16 layer at screen (6, 5) on a 32 x 24 screen; everything else stays the clear colour
  RotArgs r{TileLayer{g_rmap, RTW, RTH, &g_rts}, 0, 0, 6, 5, 16384, 0};
  CHECK(renderTo(got, RSW, RSH, RSH, rotJob, &r) == 0);
  for (int y = 0; y < RSH; y++)
    for (int x = 0; x < RSW; x++) {
      const bool in = x >= 6 && x < 6 + RPW && y >= 5 && y < 5 + RPH;
      CHECK(got[y * RSW + x] == (in ? g_rpal[layerIdx(x - 6, y - 5)] : kClear));
    }
  // huge coordinates: anchor and screen point a billion pixels away, offset by the same amount, equal the plain case
  r = RotArgs{r.l, 10, 8, 0, 0, 16384, 0};
  CHECK(renderTo(want, RSW, RSH, RSH, rotJob, &r) == 0);
  r = RotArgs{r.l, 10 - 1500000000, 8 + 1500000000, -1500000000, 1500000000, 16384, 0};
  CHECK(renderTo(got, RSW, RSH, RSH, rotJob, &r) == 0);
  CHECK(samePic("far anchor", want, got, RSW, RSH));
  r = RotArgs{r.l, 10, 8, 2000000000, -2000000000, 9830, 13107};  // far away on screen: nothing hits the layer
  CHECK(renderTo(got, RSW, RSH, RSH, rotJob, &r) == 0);
  for (int i = 0; i < RSW * RSH; i++) CHECK(got[i] == kClear);

  // drops and silent returns, on a canvas that starts at row 0
  static Color buf[RSW * RSH];
  const TileLayer ok{g_rmap, RTW, RTH, &g_rts};
  const Tileset odd_w{g_rpx, g_rpal, 3, 4, false}, odd_h{g_rpx, g_rpal, 4, 6, false}, zero{g_rpx, g_rpal, 0, 4, false};
  const Tileset big{g_rpx, g_rpal, 128, 128, false};
  const TileLayer wide{g_rmap, 300, 1, &big}, tall{g_rmap, 1, 300, &big};  // 38,400 px: over 32767
  struct Case { TileLayer l; int c, s; int drops; };
  const Case cases[] = {
      {ok, 16384, 0, 0},
      {TileLayer{g_rmap, RTW, RTH, &odd_w}, 16384, 0, 1},
      {TileLayer{g_rmap, RTW, RTH, &odd_h}, 16384, 0, 1},
      {wide, 16384, 0, 1},
      {tall, 16384, 0, 1},
      {ok, 16385, 0, 1},
      {ok, 0, -16385, 1},
      {TileLayer{g_rmap, RTW, RTH, &zero}, 16384, 0, 0},
      {TileLayer{g_rmap, RTW, RTH, nullptr}, 16384, 0, 0},
      {TileLayer{nullptr, RTW, RTH, &g_rts}, 16384, 0, 0},
  };
  for (const Case& k : cases) {
    for (Color& v : buf) v = kClear;
    Canvas cv(buf, RSW, RSH, 0, RSH, 0);
    cv.tilemapRotated(k.l, 0, 0, 0, 0, k.c, k.s);
    CHECK(cv.dropped() == k.drops);
    int drawn = 0;
    for (Color v : buf) drawn += v != kClear;
    CHECK(&k == cases ? drawn > 0 : drawn == 0);  // only the first case draws
  }
  // a band below row 0 does not count the drop (the frame counts it once)
  Canvas lower(buf, RSW, RSH, 8, 8, 0);
  lower.tilemapRotated(TileLayer{g_rmap, RTW, RTH, &odd_w}, 0, 0, 0, 0, 16384, 0);
  CHECK(lower.dropped() == 0);
  return 0;
}

int main() {
  if (testUnits() || testOrientUnits() || testGolden() || testBandsAndReference() || testTurnedGolden() ||
      testGravityAnchor() || testSpriteEqualsTile() || testEightOrientations() || testRotatedCardinal() ||
      testRotatedReference() || testRotatedBands() || testRotatedEdges())
    return 1;
  std::printf("camera OK\n");
  return 0;
}
