#pragma once
// Procedural tile + sprite scene shared by the demo and the tests (S3.1a prototype).
#include "pge/gfx.h"

namespace scene {

using namespace pge;

struct Data {
  Color pal[16];
  uint8_t tile_px[4 * 16 * 16];
  uint8_t map[64 * 32];
  uint8_t tsprite_px[4 * 16 * 16];
  uint8_t osprite_px[4 * 16 * 16];
  Tileset tiles;
  TileLayer layer;
  SpriteSheet tsprite, osprite;
};

inline Color rgb(int r, int g, int b) { return (Color)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)); }

inline const Data& data() {
  static Data d;
  static bool ready = false;
  if (ready) return d;
  const int c[16][3] = {{40, 0, 60}, {30, 90, 30}, {50, 130, 50}, {120, 90, 50}, {90, 60, 30}, {200, 200, 210},
                        {120, 120, 130}, {40, 70, 160}, {220, 60, 60}, {240, 200, 40}, {60, 200, 220},
                        {230, 120, 30}, {250, 250, 250}, {20, 20, 20}, {180, 60, 200}, {90, 200, 90}};
  for (int i = 0; i < 16; i++) d.pal[i] = rgb(c[i][0], c[i][1], c[i][2]);
  for (int t = 0; t < 4; t++)
    for (int y = 0; y < 16; y++)
      for (int x = 0; x < 16; x++) {
        uint8_t v;
        if (t == 0) v = ((x ^ y) & 4) ? 1 : 2;                               // checker grass
        else if (t == 1) v = (y % 8 == 0 || (x + (y / 8) * 8) % 16 == 0) ? 4 : 3;  // bricks
        else if (t == 2) v = ((x % 5 == 2) && (y % 5 == 2)) ? 10 : 7;        // dotted water
        else v = ((x + y) % 8 < 4) ? 5 : 6;                                  // diagonal stripes
        d.tile_px[t * 256 + y * 16 + x] = v;
      }
  for (int y = 0; y < 32; y++)
    for (int x = 0; x < 64; x++) d.map[y * 64 + x] = (uint8_t)(((x ^ y) + x / 8 + y / 4) & 3);
  for (int f = 0; f < 4; f++)
    for (int y = 0; y < 16; y++)
      for (int x = 0; x < 16; x++) {
        int dx = 2 * x - 15, dy = 2 * y - 15, r2 = dx * dx + dy * dy;
        int ex = 2 * x - (4 + 2 * f) * 2 + 1;  // "eye" moves with the frame
        uint8_t v = 0;                         // 0 = transparent
        if (r2 <= 196) v = (r2 >= 100) ? (uint8_t)(8 + f) : 12;
        if (v && ex * ex + (2 * y - 11) * (2 * y - 11) <= 8) v = 13;
        d.tsprite_px[f * 256 + y * 16 + x] = v;
        bool border = x == 0 || y == 0 || x == 15 || y == 15;
        d.osprite_px[f * 256 + y * 16 + x] = border ? 12 : (uint8_t)(((x + y + f * 4) & 8) ? 14 : 15);
      }
  d.tiles = Tileset{d.tile_px, d.pal, 16, 16};
  d.layer = TileLayer{d.map, 64, 32, &d.tiles};
  d.tsprite = SpriteSheet{d.tsprite_px, d.pal, 16, 16, 4, true};
  d.osprite = SpriteSheet{d.osprite_px, d.pal, 16, 16, 4, false};
  ready = true;
  return d;
}

inline uint32_t rnd(uint32_t& s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }

// Same pixels in every band: all state derives from `frame` and canvas.frameSeed().
inline void renderScene(Canvas& c, int frame) {
  const Data& d = data();
  c.tilemap(d.layer, (frame * 2) % 800 - 40, frame % 280 - 20);
  uint32_t s = c.frameSeed() * 2654435761u + 12345u;
  for (int i = 0; i < 20; i++) {  // smooth motion plus +-1 px seeded jitter
    int x = (i * 53 + frame * (1 + i % 3)) % (c.width() + 32) - 16 + (int)(rnd(s) % 3) - 1;
    int y = (i * 31 + frame * (1 + i % 2)) % (c.height() + 32) - 16 + (int)(rnd(s) % 3) - 1;
    c.sprite(d.tsprite, (uint16_t)((frame / 4 + i) & 3), x, y, (SpriteFlags)((i & 1) | ((i & 2) ? 2 : 0)));
  }
  for (int i = 0; i < 5; i++) {
    int x = (i * 71 + frame * 3) % (c.width() + 32) - 16;
    int y = (i * 47 + 100 + frame) % (c.height() + 32) - 16;
    c.sprite(d.osprite, (uint16_t)((frame / 8 + i) & 3), x, y, i & 1 ? FlipX : None);
  }
  c.fillRect(Rect{8, c.height() - 20, 120 + (frame % 40), 12}, rgb(20, 20, 20));
}

struct SceneCtx { int frame; };
inline void sceneRender(Canvas& c, void* ctx) { renderScene(c, static_cast<SceneCtx*>(ctx)->frame); }

}  // namespace scene
