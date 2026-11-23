#include "b1.h"
#include <stdio.h>
#include "pge/hal/time.h"

using namespace pge;

namespace {

enum { kItems = 14, kSprites = 100 };
const char* const kNames[kItems] = {"clear",          "flush_only",        "sprites_opaque",    "sprites_transparent",
                                    "tiles8_static",   "tiles16_static",    "tiles8_scroll",     "tiles16_scroll",
                                    "tiles8_scroll_r90", "tiles8_scroll_r180", "tiles8_scroll_r270",
                                    "sprites_transparent_r90", "sprites_transparent_r180", "sprites_transparent_r270"};

// ---- static assets, filled once by initAssets ----
Color g_pal[16];
uint8_t g_tile8[4 * 8 * 8];
uint8_t g_tile16[4 * 16 * 16];
uint8_t g_sprite_o[16 * 16];
uint8_t g_sprite_t[16 * 16];
// square maps: a 320x240 view turned 90 degrees plus the scroll range must fit on both axes
uint8_t g_map8[2][48 * 48];
uint8_t g_map16[2][24 * 24];
Tileset g_ts8, g_ts16;
TileLayer g_layer8[2], g_layer16[2];
SpriteSheet g_sheet_o, g_sheet_t;

void initTiles(uint8_t* px, int n) {
  for (int t = 0; t < 4; t++)
    for (int y = 0; y < n; y++)
      for (int x = 0; x < n; x++) {
        int v;
        if (t == 0) v = 1 + (((x / 2) + (y / 2)) & 1);       // checker
        else if (t == 1) v = 3 + (y & 3);                    // horizontal stripes
        else if (t == 2) v = 7 + ((x + y) & 3);              // diagonal stripes
        else v = (x == 0 || y == 0) ? 11 : 12 + ((x ^ y) & 3);  // grid edge
        px[t * n * n + y * n + x] = (uint8_t)v;
      }
}

void initAssets() {
  for (int i = 0; i < 16; i++)
    g_pal[i] = (Color)((((i * 2 + 1) & 31) << 11) | (((i * 4 + 3) & 63) << 5) | ((31 - i * 2) & 31));
  initTiles(g_tile8, 8);
  initTiles(g_tile16, 16);
  for (int y = 0; y < 16; y++)
    for (int x = 0; x < 16; x++) {
      bool border = x == 0 || y == 0 || x == 15 || y == 15;
      g_sprite_o[y * 16 + x] = border ? 12 : (uint8_t)(((x + y) & 1) ? 14 : 15);
      int dx = 2 * x - 15, dy = 2 * y - 15, r2 = dx * dx + dy * dy;
      g_sprite_t[y * 16 + x] = r2 <= 196 ? (uint8_t)(r2 >= 100 ? 8 : 9) : 0;  // 0 = transparent
    }
  for (int l = 0; l < 2; l++) {
    for (int y = 0; y < 48; y++)
      for (int x = 0; x < 48; x++) g_map8[l][y * 48 + x] = (uint8_t)((x * 3 + y * 5 + l) & 3);
    for (int y = 0; y < 24; y++)
      for (int x = 0; x < 24; x++) g_map16[l][y * 24 + x] = (uint8_t)((x * 3 + y * 5 + l) & 3);
    g_layer8[l] = TileLayer{g_map8[l], 48, 48, &g_ts8};
    g_layer16[l] = TileLayer{g_map16[l], 24, 24, &g_ts16};
  }
  g_ts8 = Tileset{g_tile8, g_pal, 8, 8};
  g_ts16 = Tileset{g_tile16, g_pal, 16, 16};
  g_sheet_o = SpriteSheet{g_sprite_o, g_pal, 16, 16, 1, false};
  g_sheet_t = SpriteSheet{g_sprite_t, g_pal, 16, 16, 1, true};
}

const uint32_t kAssetBytes = sizeof g_pal + sizeof g_tile8 + sizeof g_tile16 + sizeof g_sprite_o +
                             sizeof g_sprite_t + sizeof g_map8 + sizeof g_map16;

struct Ctx { uint32_t frame; int item; };

void drawSprites(Canvas& c, const SpriteSheet& s, uint32_t frame, SpriteFlags fl = None) {
  int mx = c.width() - 16, my = c.height() - 16;
  for (int i = 0; i < kSprites; i++) {
    int x = (int)(((uint32_t)i * 37u + frame) % (uint32_t)mx);
    int y = (int)(((uint32_t)i * 23u + frame / 2u) % (uint32_t)my);
    c.sprite(s, 0, x, y, fl);
  }
}

// Two tile layers under one camera; scroll moves the camera across the map.
void drawTiles(Canvas& c, uint32_t frame, bool big, bool scroll, uint8_t orient) {
  int cx = scroll ? (int)(frame % 64u) : 0, cy = scroll ? (int)(frame % 48u) : 0;
  c.tilemap(big ? g_layer16[0] : g_layer8[0], cx, cy, orient);
  c.tilemap(big ? g_layer16[1] : g_layer8[1], cx, cy, orient);
}

const SpriteFlags kRot[3] = {Rot90, Rot180, Rot270};

void renderItem(Canvas& c, void* p) {
  const Ctx* ctx = static_cast<const Ctx*>(p);
  const int item = ctx->item;
  switch (item) {
    case 0: c.clear((Color)(0x3186 + ctx->frame)); break;
    case 1: break;
    case 2: drawSprites(c, g_sheet_o, ctx->frame); break;
    case 3: drawSprites(c, g_sheet_t, ctx->frame); break;
    default:
      if (item <= 7) drawTiles(c, ctx->frame, (item & 1) != 0, item >= 6, 0);           // 4..7
      else if (item <= 10) drawTiles(c, ctx->frame, false, true, (uint8_t)(item - 7));  // 8..10: orient 1..3
      else drawSprites(c, g_sheet_t, ctx->frame, kRot[item - 11]);                      // 11..13
  }
}

}  // namespace

int b1_run(const B1Config& cfg) {
  if (!cfg.bands || !cfg.nbands || !cfg.frames || !cfg.band_buf || !cfg.print || cfg.w < 17 || cfg.h < 17) return -1;
  for (int b = 0; b < cfg.nbands; b++)
    if (cfg.bands[b] == 0) return -1;
  initAssets();
  char line[200];
  for (int b = 0; b < cfg.nbands; b++) {
    uint32_t band = cfg.bands[b];
    if (band * cfg.w > cfg.band_buf_pixels) {
      snprintf(line, sizeof line, "band=%u skipped: band buffer too small", (unsigned)band);
      cfg.print(line, cfg.user);
      continue;
    }
    uint32_t eff = band > cfg.h ? cfg.h : band;
    uint32_t per_frame = (cfg.h + eff - 1) / eff;
    RenderConfig rc = {cfg.w, cfg.h, (uint16_t)band, 0};
    for (int item = 0; item < kItems; item++) {
      Ctx ctx = {0, item};
      int err = renderFrame(rc, cfg.band_buf, cfg.band_buf_pixels, 0, renderItem, &ctx);  // warm-up
      if (err) return err;
      uint32_t t0 = pge_hal_time_us();
      for (uint32_t f = 1; f <= cfg.frames; f++) {
        ctx.frame = f;
        err = renderFrame(rc, cfg.band_buf, cfg.band_buf_pixels, f, renderItem, &ctx);
        if (err) return err;
      }
      uint32_t us_frame = (pge_hal_time_us() - t0) / cfg.frames;
      snprintf(line, sizeof line,
               "band=%u item=%s frames=%u us_per_frame=%u us_per_band=%u bands_per_frame=%u band_buf_bytes=%u",
               (unsigned)band, kNames[item], (unsigned)cfg.frames, (unsigned)us_frame,
               (unsigned)(us_frame / per_frame), (unsigned)per_frame, (unsigned)(band * cfg.w * 2));
      cfg.print(line, cfg.user);
    }
  }
  snprintf(line, sizeof line, "ram: assets_bytes=%u (band buffers are caller-owned, see band_buf_bytes)",
           (unsigned)kAssetBytes);
  cfg.print(line, cfg.user);
  return 0;
}
