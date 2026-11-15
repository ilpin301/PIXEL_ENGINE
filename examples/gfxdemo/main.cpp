// pge_gfxdemo [--scale S] [--frames N] [--shot out.bmp] [--band L]   (S 1..3 default 2, N = 0: until the window closes)
// Primitives and text on a (320/S) x (240/S) logical screen; calls renderFrame directly, not the Engine.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "pge/color.h"
#include "pge/gfx.h"
#include "pge/sprite.h"
#include "pge/hal/display.h"

extern "C" int pge_host_pump(void);
extern "C" int pge_host_save_bmp(const char* path);

static pge::Color g_band[320 * 240];   // worst case: scale 1, full-height band
static pge::Color g_stage[320 * 240];  // worst case: logical * scale never exceeds the 320x240 plane

// 8x8 player sprite, 4 frames in a 2x2 atlas (16x16 px), right-pointing arrow with a growing wing. Index 0 = transparent.
static uint8_t g_spx[16 * 16];
static const pge::SpriteSheet g_player = {g_spx, pge::kPalette16, 8, 8, 4, true, 2};
static const pge::Animation g_walk = {0, 4, 6, true};

static void makeSprite() {
  for (int f = 0; f < 4; f++) {
    uint8_t* o = g_spx + (f / 2) * 8 * 16 + (f % 2) * 8;  // top-left pixel of frame f in the atlas
    for (int c = 0; c < 6; c++) { o[3 * 16 + c] = 1; o[4 * 16 + c] = 1; }  // body
    for (int r = 2; r <= 5; r++) o[r * 16 + 6] = 2;                       // head
    o[3 * 16 + 7] = 2; o[4 * 16 + 7] = 2;                                 // tip
    for (int r = (2 - f < 0 ? 0 : 2 - f); r <= 2; r++) o[r * 16 + 2] = 3; // wing, taller each frame
  }
}

// Designed for 160x120; pure function of the frame number (renderFrame calls it once per band).
static void sceneRender(pge::Canvas& c, void* p) {
  using namespace pge;
  const int f = *static_cast<int*>(p);
  const Color* P = kPalette16;
  for (int i = 0; i < 16; i++) c.fillRect(Rect{2 + i * 9, 2, 8, 8}, P[i]);
  c.rect(Rect{1 + ((f / 4) % 16) * 9, 1, 10, 10}, P[1]);  // walks along the swatches
  for (int i = 0; i < 16; i++) c.pixel(2 + i * 3, 14, P[i]);
  for (int i = 0; i < 16; i++) c.pixel(2 + i * 9 + (f & 7), 17, P[i]);

  static const int fan[][2] = {{58, 20}, {58, 32}, {56, 44}, {50, 56}, {40, 62}, {26, 64}, {10, 62}, {4, 54}, {3, 40}, {6, 26}};
  for (int i = 0; i < 10; i++) c.line(30, 42, fan[i][0], fan[i][1], P[2 + i]);

  c.rect(Rect{80, 22, 40, 26}, P[5]);
  c.fillRect(Rect{84, 26, 14, 8}, P[11]);
  c.fillRect(Rect{100, 36, 16, 8}, P[12]);
  c.circle(140, 36, 14, P[6]);
  c.fillCircle(110, 62, 10, P[13]);
  c.circle(110, 62, 10, P[3]);

  int t = f % 64, dx = t < 32 ? t : 64 - t;
  c.fillCircle(20 + dx * 3, 112, 6, P[2]);
  c.circle(20 + dx * 3, 112, 8, P[15]);

  c.text(kFont5x7, 2, 70, "The quick brown fox", P[1]);
  c.text(kFont5x7, 2, 79, "0123456789 !?.,;:", P[5]);
  c.text(kFont5x7, 2, 88, "UPPER lower MiXeD gjpqy", P[6]);
  c.text(kFont5x7, 2, 97, "{[(<>)]} +-*/=%#@&", P[11]);
  c.text(kFont5x7, 124, 70, "two\nlines", P[3]);
  c.text(kFont5x7, 132, 98, "end", P[15]);
  const uint16_t pf = animFrame(g_walk, (uint32_t)f);
  c.sprite(g_player, pf, 124, 106, None);
  c.sprite(g_player, pf, 134, 106, FlipX);
  c.sprite(g_player, pf, 144, 106, static_cast<SpriteFlags>(((f / 16) & 3) << 2));  // cycles through the rotations
  c.sprite(g_player, pf, 156, 106, FlipX | Rot90);                                   // partly off the right edge
}

int main(int argc, char** argv) {
  int scale = 2, frames = 0, band = 16;
  const char* shot = nullptr;
  for (int i = 1; i + 1 < argc; i += 2) {
    if (!std::strcmp(argv[i], "--scale")) scale = std::atoi(argv[i + 1]);
    else if (!std::strcmp(argv[i], "--frames")) frames = std::atoi(argv[i + 1]);
    else if (!std::strcmp(argv[i], "--shot")) shot = argv[i + 1];
    else if (!std::strcmp(argv[i], "--band")) band = std::atoi(argv[i + 1]);
  }
  if (scale < 1 || scale > pge::kMaxScale || band < 1) { std::printf("bad --scale or --band\n"); return 1; }
  const int lw = 320 / scale, lh = 240 / scale;
  makeSprite();
  pge_display_info info;
  if (pge_hal_display_init((uint16_t)(lw * scale), (uint16_t)(lh * scale), &info) != 0) {
    std::printf("display init failed\n");
    return 1;
  }
  pge::RenderConfig cfg = {(uint16_t)lw, (uint16_t)lh, (uint16_t)band, 0, (uint8_t)scale, g_stage, 320 * 240};
  for (int f = 0; !pge_host_pump(); f++) {
    int err = pge::renderFrame(cfg, g_band, 320 * 240, (uint32_t)f, sceneRender, &f);
    if (err != 0) { std::printf("renderFrame failed: %d\n", err); return 1; }
    if (frames && f + 1 >= frames) break;
  }
  if (shot && pge_host_save_bmp(shot) != 0) { std::printf("save_bmp failed\n"); return 1; }
  return 0;
}
