// Camera tests: clampAxis / Camera / parallaxOffset unit cases, an ASCII golden for two parallax layers, and
// an independent reference model compared with full and band rendering (own mock display HAL).
#include <cmath>
#include <cstdio>
#include <cstring>
#include "pge/camera.h"
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

// Pixel of the scene at screen (sx, sy), computed straight from the tile / pixel formula.
static Color refPixel(const Camera& cam, int sx, int sy) {
  Color out = kClear;
  for (int l = 0; l < NL; l++) {
    const int wx = sx + (int)std::floor((double)cam.x * kFx[l] / 256.0);
    const int wy = sy + (int)std::floor((double)cam.y * kFy[l] / 256.0);
    const int tx = fl(wx, 4), ty = fl(wy, 4);
    if (tx < 0 || tx >= LW || ty < 0 || ty >= LH) continue;
    const int idx = g_px[l][g_map[l][ty * LW + tx] * 16 + (wy - ty * 4) * 4 + (wx - tx * 4)];
    if (l != 0 && idx == 0) continue;
    out = g_pal[l][idx];
  }
  return out;
}

static int testBandsAndReference() {
  buildLayers();
  static const int pos[][2] = {{0, 0},   {1, 1},    {3, 5},    {4, 4},   {7, -3},   {-5, 2},  {-40, -40},
                               {100, 100}, {16, 12}, {33, 9},   {-1, -1}, {63, 41},  {12, -17}, {200, -300},
                               {8, 8},   {-8, -8}};
  static Color band[RW * RH], full[RW * RH], ref[RW * RH];
  const int bands[] = {RH, 5, 3, 1};
  for (const auto& p : pos) {
    Camera cam{p[0], p[1], RW, RH, 0, 0};
    for (int y = 0; y < RH; y++)
      for (int x = 0; x < RW; x++) ref[y * RW + x] = refPixel(cam, x, y);
    for (int b : bands) {
      mockReset(RW, RH);
      RenderConfig cfg = {RW, RH, (uint16_t)b, kClear};
      CHECK(renderFrame(cfg, band, RW * RH, 0, drawJob, &cam) == 0);
      if (b == RH) std::memcpy(full, g_fb, sizeof full);
      if (std::memcmp(g_fb, full, sizeof full) != 0) std::printf("band differs: cam=(%d,%d) band=%d\n", p[0], p[1], b);
      CHECK(std::memcmp(g_fb, full, sizeof full) == 0);
      if (std::memcmp(g_fb, ref, sizeof ref) != 0) std::printf("reference differs: cam=(%d,%d) band=%d\n", p[0], p[1], b);
      CHECK(std::memcmp(g_fb, ref, sizeof ref) == 0);
    }
  }
  return 0;
}

int main() {
  if (testUnits() || testGolden() || testBandsAndReference()) return 1;
  std::printf("camera OK\n");
  return 0;
}
