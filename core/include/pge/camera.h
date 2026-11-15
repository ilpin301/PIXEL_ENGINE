#pragma once
#include <stdint.h>
#include "pge/gfx.h"

namespace pge {

// Camera position on one axis for a view of `view` pixels in a world of `world` pixels. If the world is larger
// than the view, pos is clamped to [0, world - view]. Otherwise the world is centred: floor((world - view) / 2),
// which is <= 0 (the world is drawn with a margin; floor keeps odd differences deterministic).
int clampAxis(int pos, int view, int world);

// Top-left world pixel of the screen (x, y) plus the view and world sizes, all in pixels.
struct Camera {
  int x = 0, y = 0;
  int view_w = 0, view_h = 0;
  int world_w = 0, world_h = 0;
  void clamp();                   // clampAxis on both axes
  void moveTo(int nx, int ny);    // sets x, y, then clamps
  void centerOn(int px, int py);  // puts the world point (px, py) at the view centre, then clamps
};

// floor(cam * factor_q8 / 256) for a Q8 factor: 256 = 1.0 (moves with the world), 0 = fixed background,
// 128 = half speed, > 256 = foreground that moves faster. Floors for negative cam too.
int parallaxOffset(int cam, int factor_q8);

// One tile layer with its parallax factors (Q8, see parallaxOffset).
struct MapLayer {
  TileLayer tiles;
  uint16_t px_q8 = 256, py_q8 = 256;
};

// Draws layers[0] (back) to layers[count - 1] (front); a layer drawn over another needs a transparent tileset.
// Null `layers` or count <= 0 draws nothing. The caller keeps count <= PGE_PARALLAX_LAYERS (not enforced).
void drawLayers(Canvas& c, const Camera& cam, const MapLayer* layers, int count);

}  // namespace pge
