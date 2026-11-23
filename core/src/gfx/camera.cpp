#include "pge/camera.h"

namespace pge {

int clampAxis(int pos, int view, int world) {
  if (world <= view) return (world - view) >> 1;  // arithmetic shift = floor division
  const int max = world - view;
  return pos < 0 ? 0 : pos > max ? max : pos;
}

int Camera::visW() const { return (orient & 1) ? view_h : view_w; }
int Camera::visH() const { return (orient & 1) ? view_w : view_h; }

void Camera::clamp() {
  x = clampAxis(x, visW(), world_w);
  y = clampAxis(y, visH(), world_h);
}

void Camera::moveTo(int nx, int ny) {
  x = nx;
  y = ny;
  clamp();
}

void Camera::centerOn(int px, int py) { moveTo(px - visW() / 2, py - visH() / 2); }

Rect Camera::toScreen(Rect world) const {
  return turnRect(Rect{world.x - x, world.y - y, world.w, world.h}, visW(), visH(), orient);
}

SpriteFlags Camera::spriteFlags(SpriteFlags f) const {
  const int rot = (((f >> 2) & 3) + orient) & 3;  // add, not OR: Rot90 + 3 turns is None
  return static_cast<SpriteFlags>((f & (FlipX | FlipY)) | (rot << 2));
}

int parallaxOffset(int cam, int factor_q8) {
  return static_cast<int>((static_cast<int64_t>(cam) * factor_q8) >> 8);
}

void drawLayers(Canvas& c, const Camera& cam, const MapLayer* layers, int count) {
  if (!layers) return;
  for (int i = 0; i < count; i++)
    c.tilemap(layers[i].tiles, parallaxOffset(cam.x, layers[i].px_q8), parallaxOffset(cam.y, layers[i].py_q8),
              cam.orient);
}

}  // namespace pge
