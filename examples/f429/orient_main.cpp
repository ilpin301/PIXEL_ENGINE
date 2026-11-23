// pge_f429_orient: one test picture (tiles + sprite F, a green edge at the top) shown in the 4 camera orientations,
// auto-cycling every 3 s. Check by eye that each step is a 90-degree clockwise turn, never a mirror.
#include "pge/camera.h"
#include "pge/gfx.h"
#include "pge/hal/display.h"
#include "pge/hal/time.h"
#include "board.h"

static pge::Color g_band[320 * 16];

static const pge::Color g_palette[16] = {
    0x0000, 0x4208, 0x07E0, 0xF800, 0x001F, 0xFFE0, 0xFFFF, 0xF81F, 0x2104, 0, 0, 0, 0, 0, 0, 0};

// Tile t is 8x8: its own top and left edge in palette index 8, the rest in the base index.
static const uint8_t kTileBase[7] = {1, 2, 3, 4, 5, 6, 7};
static uint8_t g_tiles[7 * 8 * 8];
static uint8_t g_map[30 * 30];
static uint8_t g_sprite[16 * 16];

static const pge::Tileset g_tileset = {g_tiles, g_palette, 8, 8, false};
static const pge::MapLayer g_layers[1] = {{pge::TileLayer{g_map, 30, 30, &g_tileset}}};
static const pge::SpriteSheet g_sheet = {g_sprite, g_palette, 16, 16, 1, true};

static const char* const kLine1[4] = {"ORIENT 0", "ORIENT 1", "ORIENT 2", "ORIENT 3"};
static const char* const kLine2[4] = {"GREEN EDGE AT TOP", "GREEN EDGE AT RIGHT", "GREEN EDGE AT BOTTOM",
                                      "GREEN EDGE AT LEFT"};
static const char kLine3[] = "TILE F + SPRITE F MUST MATCH";

// Fills the inclusive box x0..x1, y0..y1 of a buffer with the given row stride.
static void setBox(uint8_t* buf, int stride, int x0, int x1, int y0, int y1, uint8_t v) {
  for (int y = y0; y <= y1; y++)
    for (int x = x0; x <= x1; x++) buf[y * stride + x] = v;
}

static void buildScene() {
  static const uint8_t kTileIdx[7] = {0, 1, 2, 3, 4, 5, 6};
  for (int t = 0; t < 7; t++)
    for (int y = 0; y < 8; y++)
      for (int x = 0; x < 8; x++) g_tiles[t * 64 + y * 8 + x] = (x == 0 || y == 0) ? 8 : kTileBase[kTileIdx[t]];

  setBox(g_map, 30, 0, 29, 0, 0, 1);    // top row: green
  setBox(g_map, 30, 0, 29, 29, 29, 2);  // bottom row: red
  setBox(g_map, 30, 0, 0, 0, 29, 3);    // left column: blue
  setBox(g_map, 30, 29, 29, 0, 29, 4);  // right column: yellow
  g_map[0] = 6;                         // top-left corner: magenta
  setBox(g_map, 30, 11, 12, 8, 22, 5);  // big F: vertical bar
  setBox(g_map, 30, 11, 20, 8, 9, 5);   // big F: top bar
  setBox(g_map, 30, 11, 18, 14, 15, 5); // big F: middle bar

  setBox(g_sprite, 16, 0, 15, 0, 0, 7);  // frame: top row only
  setBox(g_sprite, 16, 0, 0, 0, 15, 7);  // frame: left column only
  setBox(g_sprite, 16, 4, 5, 3, 13, 6);  // sprite F: vertical bar
  setBox(g_sprite, 16, 4, 11, 3, 4, 6);  // sprite F: top bar
  setBox(g_sprite, 16, 4, 9, 7, 8, 6);   // sprite F: middle bar
}

struct RenderCtx {
  uint8_t orient;
};

static void render(pge::Canvas& c, void* ctx) {
  const RenderCtx& rc = *static_cast<const RenderCtx*>(ctx);
  pge::Camera cam;
  cam.view_w = 320;
  cam.view_h = 240;
  cam.world_w = 240;
  cam.world_h = 240;
  cam.orient = rc.orient;
  cam.centerOn(120, 120);

  pge::drawLayers(c, cam, g_layers, 1);
  pge::Rect sr = cam.toScreen(pge::Rect{192, 96, 16, 16});
  c.sprite(g_sheet, 0, sr.x, sr.y, cam.spriteFlags(pge::None));

  c.text(pge::kFont5x7, 4, 4, kLine1[rc.orient], 0xFFFF);
  c.text(pge::kFont5x7, 4, 14, kLine2[rc.orient], 0xFFFF);
  c.text(pge::kFont5x7, 4, 24, kLine3, 0xFFFF);
}

int main() {
  pge_board_init();
  pge_dbg_printf("# pge_f429_orient\n");
  pge_f429_set_opts(0, 1);

  pge_display_info info;
  if (pge_hal_display_init(320, 240, &info) != 0) {
    pge_dbg_puts("display init FAILED\n");
    pge_board_led(3, 1);
    for (;;) {}
  }
  pge_board_led(4, 1);
  buildScene();

  RenderCtx rc = {0};
  uint32_t last_change = pge_hal_time_us();
  pge_dbg_printf("orient=%d t_us=%lu\n", (int)rc.orient, (unsigned long)last_change);

  const pge::RenderConfig cfg = {320, 240, 16, 0};
  bool err_logged = false;
  for (uint32_t frame = 0;; frame++) {
    uint32_t now = pge_hal_time_us();
    pge_dbg_now_us = now;
    if (now - last_change >= 3000000u) {
      last_change = now;
      rc.orient = static_cast<uint8_t>((rc.orient + 1) & 3);
      pge_dbg_printf("orient=%d t_us=%lu\n", (int)rc.orient, (unsigned long)now);
    }
    int err = pge::renderFrame(cfg, g_band, 320 * 16, frame, render, &rc);
    if (err != 0) {
      pge_board_led(3, 1);
      if (!err_logged) pge_dbg_printf("renderFrame failed: %d (frame %lu)\n", err, (unsigned long)frame);
      err_logged = true;
    }
  }
}
