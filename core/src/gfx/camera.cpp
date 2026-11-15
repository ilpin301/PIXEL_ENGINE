#include "pge/camera.h"

namespace pge {

int clampAxis(int pos, int view, int world) {
  if (world <= view) return (world - view) >> 1;  // arithmetic shift = floor division
  const int max = world - view;
  return pos < 0 ? 0 : pos > max ? max : pos;
}

void Camera::clamp() {
  x = clampAxis(x, view_w, world_w);
  y = clampAxis(y, view_h, world_h);
}

void Camera::moveTo(int nx, int ny) {
  x = nx;
  y = ny;
  clamp();
}

void Camera::centerOn(int px, int py) { moveTo(px - view_w / 2, py - view_h / 2); }

int parallaxOffset(int cam, int factor_q8) {
  return static_cast<int>((static_cast<int64_t>(cam) * factor_q8) >> 8);
}

void drawLayers(Canvas& c, const Camera& cam, const MapLayer* layers, int count) {
  if (!layers) return;
  for (int i = 0; i < count; i++)
    c.tilemap(layers[i].tiles, parallaxOffset(cam.x, layers[i].px_q8), parallaxOffset(cam.y, layers[i].py_q8));
}

}  // namespace pge
