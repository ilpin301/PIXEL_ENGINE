#include <cstdio>
#include <cstring>
#include <initializer_list>
#include "pge/gfx.h"
#include "pge/hal/display.h"
#include "scene.h"

#define PGE_CHECK(cond) \
  do { if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); return 1; } } while (0)

using pge::Color;

enum { W = 320, H = 240 };

// ---- mock display HAL: copies flushed rects into a full frame and records them ----
static Color g_fb[W * H];
static pge_rect g_rects[H + 1];
static int g_nflush, g_nwait, g_nbegin, g_nend, g_flush_err;

static void mockReset() {
  std::memset(g_fb, 0xFF, sizeof g_fb);
  g_nflush = g_nwait = g_nbegin = g_nend = 0;
}

extern "C" {
pge_error pge_hal_display_init(uint16_t, uint16_t, pge_display_info*) { return 0; }
pge_error pge_hal_display_flush(pge_rect r, uint16_t* px) {
  if (r.x + r.w > W || r.y + r.h > H || g_nflush > H) return -2;
  g_rects[g_nflush++] = r;
  for (int y = 0; y < r.h; y++) std::memcpy(&g_fb[(r.y + y) * W + r.x], px + y * r.w, r.w * sizeof(Color));
  return g_flush_err;
}
pge_error pge_hal_display_wait(void) { g_nwait++; return 0; }
void pge_hal_display_frame_begin(void) { g_nbegin++; }
void pge_hal_display_frame_end(void) { g_nend++; }
}

static Color g_band[W * H];
static const Color kClear = 0x1234;

static int renderTo(int frame, int band, Color* out) {
  mockReset();
  pge::RenderConfig cfg = {W, H, (uint16_t)band, kClear};
  scene::SceneCtx ctx = {frame};
  int err = pge::renderFrame(cfg, g_band, W * H, (uint32_t)frame, scene::sceneRender, &ctx);
  std::memcpy(out, g_fb, sizeof g_fb);
  return err;
}

static Color g_ref[W * H], g_cur[W * H];

static int testBandVsFull() {
  const int frames[] = {0, 37, 100};
  const int bands[] = {240, 16, 8, 7, 1};
  for (int f : frames) {
    PGE_CHECK(renderTo(f, H, g_ref) == 0);
    int drawn = 0;
    for (int i = 0; i < W * H; i++) drawn += g_ref[i] != kClear;
    PGE_CHECK(drawn > W * H / 2);  // the scene really draws something
    for (int b : bands) {
      PGE_CHECK(renderTo(f, b, g_cur) == 0);
      PGE_CHECK(std::memcmp(g_ref, g_cur, sizeof g_ref) == 0);
    }
  }
  return 0;
}

static int testCoverage() {
  const int bands[] = {240, 16, 7, 1, 500};
  for (int b : bands) {
    PGE_CHECK(renderTo(0, b, g_cur) == 0);
    int lines = b > H ? H : b, expect = (H + lines - 1) / lines, y = 0;
    PGE_CHECK(g_nflush == expect && g_nwait == expect && g_nbegin == 1 && g_nend == 1);
    for (int i = 0; i < g_nflush; i++) {
      PGE_CHECK(g_rects[i].x == 0 && g_rects[i].w == W && g_rects[i].y == y && g_rects[i].h > 0);
      y += g_rects[i].h;
    }
    PGE_CHECK(y == H);
  }
  return 0;
}

// ---- clipping: guard words around a band buffer, every pixel compared with an independent model ----
enum { GUARD = 64, BAND = 8 };
static Color g_mem[GUARD + W * BAND + GUARD];
static const Color kBg = 0xAAAA, kFg = 0x1111;

static int guardsIntact() {
  for (int i = 0; i < GUARD; i++)
    if (g_mem[i] != 0xDEAD || g_mem[GUARD + W * BAND + i] != 0xDEAD) return 0;
  return 1;
}

static void memReset() {
  for (int i = 0; i < GUARD + W * BAND + GUARD; i++) g_mem[i] = 0xDEAD;
  for (int i = 0; i < W * BAND; i++) g_mem[GUARD + i] = kBg;
}

static int testClipping() {
  static uint8_t px[64];
  for (uint8_t& v : px) v = 1;
  static const Color pal[2] = {0, kFg};
  const pge::SpriteSheet sheet = {px, pal, 8, 8, 1, false};
  const int xs[] = {-100, -9, -8, -3, 0, 100, 312, 313, 319, 320, 400};
  const int ys[] = {-100, 0, 8, 9, 10, 16, 20, 23, 24, 25, 231, 235, 239, 240, 300};
  for (int by : {16, 232}) {
    for (int mode = 0; mode < 2; mode++)
      for (int x : xs)
        for (int y : ys) {
          memReset();
          pge::Canvas c(g_mem + GUARD, W, H, by, BAND, 0);
          if (mode == 0) c.fillRect(pge::Rect{x, y, 8, 8}, kFg);
          else c.sprite(sheet, 0, x, y, pge::None);
          PGE_CHECK(guardsIntact());
          for (int py = 0; py < BAND; py++)
            for (int pxl = 0; pxl < W; pxl++) {
              bool in = pxl >= x && pxl < x + 8 && by + py >= y && by + py < y + 8;
              PGE_CHECK(g_mem[GUARD + py * W + pxl] == (in ? kFg : kBg));
            }
        }
    memReset();
    pge::Canvas c(g_mem + GUARD, W, H, by, BAND, 0);
    c.fillRect(pge::Rect{-5000, -5000, 10000, 10000}, kFg);  // covers everything, still no overrun
    c.fillRect(pge::Rect{0, by, 0, 5}, 0);                   // empty and negative sizes draw nothing
    c.fillRect(pge::Rect{0, by, -5, 5}, 0);
    PGE_CHECK(guardsIntact());
    for (int i = 0; i < W * BAND; i++) PGE_CHECK(g_mem[GUARD + i] == kFg);
  }
  memReset();
  pge::Canvas v(g_mem + GUARD, W, H, 16, BAND, 0);
  PGE_CHECK(!v.visible(pge::Rect{0, 0, 10, 16}) && v.visible(pge::Rect{0, 0, 10, 17}));
  PGE_CHECK(!v.visible(pge::Rect{-10, 20, 10, 5}) && !v.visible(pge::Rect{W, 20, 5, 5}));
  return 0;
}

static int testTransparency() {
  static const uint8_t px4[16] = {0, 1, 2, 0, 1, 2, 1, 2, 2, 1, 2, 1, 0, 2, 1, 0};
  static const uint8_t px2[4] = {1, 2, 3, 4};
  static const Color pal[5] = {0x0001, 0x00F0, 0x0F00, 0x1000, 0x2000};
  const pge::SpriteSheet tr = {px4, pal, 4, 4, 1, true}, op = {px4, pal, 4, 4, 1, false};
  const pge::SpriteSheet two = {px2, pal, 2, 2, 1, false};
  memReset();
  pge::Canvas c(g_mem + GUARD, W, H, 0, BAND, 0);
  const Color* row = g_mem + GUARD;
  c.sprite(tr, 0, 10, 0, pge::None);
  PGE_CHECK(row[10] == kBg && row[13] == kBg && row[11] == pal[1] && row[12] == pal[2]);  // index 0 keeps background
  c.sprite(op, 0, 30, 0, pge::None);
  PGE_CHECK(row[30] == pal[0] && row[33] == pal[0] && row[31] == pal[1]);                 // opaque overwrites
  c.sprite(tr, 1, 50, 0, pge::None);  // frame out of range: nothing drawn
  PGE_CHECK(row[51] == kBg);
  c.sprite(two, 0, 100, 0, pge::FlipX);
  PGE_CHECK(row[100] == pal[2] && row[101] == pal[1] && row[W + 100] == pal[4] && row[W + 101] == pal[3]);
  c.sprite(two, 0, 110, 0, pge::FlipY);
  PGE_CHECK(row[110] == pal[3] && row[111] == pal[4] && row[W + 110] == pal[1] && row[W + 111] == pal[2]);
  PGE_CHECK(guardsIntact());
  return 0;
}

static int testBadArgs() {
  pge::RenderConfig cfg = {W, H, 16, 0};
  scene::SceneCtx ctx = {0};
  mockReset();
  PGE_CHECK(pge::renderFrame(cfg, g_band, W * 16 - 1, 0, scene::sceneRender, &ctx) == pge::InvalidArg);
  cfg.band_lines = 500;  // clamped to 240, which needs the full frame
  PGE_CHECK(pge::renderFrame(cfg, g_band, W * 16, 0, scene::sceneRender, &ctx) == pge::InvalidArg);
  cfg.band_lines = 0;
  PGE_CHECK(pge::renderFrame(cfg, g_band, W * H, 0, scene::sceneRender, &ctx) == pge::InvalidArg);
  PGE_CHECK(g_nbegin == 0 && g_nflush == 0);  // rejected before touching the HAL
  cfg.band_lines = 16;
  PGE_CHECK(pge::renderFrame(cfg, g_band, W * 16, 0, scene::sceneRender, &ctx) == 0);  // exact size is fine
  g_flush_err = -7;
  PGE_CHECK(pge::renderFrame(cfg, g_band, W * 16, 0, scene::sceneRender, &ctx) == -7);
  g_flush_err = 0;
  return 0;
}

int main() {
  int rc;
  if ((rc = testBandVsFull())) return rc;
  if ((rc = testCoverage())) return rc;
  if ((rc = testClipping())) return rc;
  if ((rc = testTransparency())) return rc;
  if ((rc = testBadArgs())) return rc;
  std::printf("gfx: all checks passed\n");
  return 0;
}
