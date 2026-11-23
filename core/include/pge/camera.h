#pragma once
#include <stdint.h>
#include "pge/gfx.h"
#include "pge/collision.h"

namespace pge {

// Camera position on one axis for a view of `view` pixels in a world of `world` pixels. If the world is larger
// than the view, pos is clamped to [0, world - view]. Otherwise the world is centred: floor((world - view) / 2),
// which is <= 0 (the world is drawn with a margin; floor keeps odd differences deterministic).
int clampAxis(int pos, int view, int world);

// (x, y) = top-left corner of the axis-aligned WORLD rect the screen shows, plus the screen size (view_w x view_h) and
// the world size, all in pixels. "orient" = number of clockwise quarter turns applied to the whole world picture
// before it is shown, 0..3 (only the low 2 bits count); same numbering as the rotation field in bits 2-3 of
// SpriteFlags (0 none, 1 = Rot90, 2 = Rot180, 3 = Rot270). A clockwise turn moves the top-left corner to the top-right,
// the top-right to the bottom-right, the bottom-right to the bottom-left: after 1 turn the world's left edge is the
// screen's top edge, after 2 the screen's right edge, after 3 the screen's bottom edge. The world rect shown is
// visW() x visH(): view_w x view_h for even orient, view_h x view_w for odd.
struct Camera {
  int x = 0, y = 0;
  int view_w = 0, view_h = 0;
  int world_w = 0, world_h = 0;
  uint8_t orient = 0;  // last, so aggregate initialisers without it still compile
  int visW() const;  // visible world size
  int visH() const;
  void clamp();                   // clampAxis on both axes, against the visible world size
  void moveTo(int nx, int ny);    // sets x, y, then clamps
  void centerOn(int px, int py);  // puts the world point (px, py) at the view centre, then clamps
  // Screen rect of a world rect: the rect relative to (x, y), then turned by orient. Pass the rect the sprite covers in
  // the world including the sprite's own rotation; for a single point pass a 1x1 rect.
  Rect toScreen(Rect world) const;
  // f with its rotation field (bits 2-3, read as 0..3) increased by orient modulo 4 (Rot90 + 3 turns = None); flips kept.
  SpriteFlags spriteFlags(SpriteFlags f) const;
};

// The orient that makes gravity point down on the screen: Down 0, Right 1, Up 2, Left 3 (the enum runs clockwise on
// screen, so it is (4 - gd) masked with 3).
inline uint8_t orientForGravity(GravityDir gd) { return static_cast<uint8_t>((4 - static_cast<uint8_t>(gd)) & 3); }

// floor(cam * factor_q8 / 256) for a Q8 factor: 256 = 1.0 (moves with the world), 0 = fixed background,
// 128 = half speed, > 256 = foreground that moves faster. Floors for negative cam too.
int parallaxOffset(int cam, int factor_q8);

// One tile layer with its parallax factors (Q8, see parallaxOffset).
struct MapLayer {
  TileLayer tiles;
  uint16_t px_q8 = 256, py_q8 = 256;
};

// Draws layers[0] (back) to layers[count - 1] (front); a layer drawn over another needs a transparent tileset.
// Parallax factors apply per world axis: each layer's top-left is shifted by its factor, nothing else changes with
// cam.orient.
// Null `layers` or count <= 0 draws nothing. The caller keeps count <= PGE_PARALLAX_LAYERS (not enforced).
void drawLayers(Canvas& c, const Camera& cam, const MapLayer* layers, int count);

}  // namespace pge
