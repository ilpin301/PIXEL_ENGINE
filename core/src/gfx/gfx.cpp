#include "pge/gfx.h"
#include "pge/hal/display.h"

namespace pge {

static int floorDiv(int a, int b) { int q = a / b; return (a % b < 0) ? q - 1 : q; }

Canvas::Canvas(Color* buf, int w, int h, int band_y, int band_h, uint32_t seed)
    : buf_(buf), w_(w), h_(h), by_(band_y), bh_(band_h), seed_(seed) {}

bool Canvas::clip(Rect& r) const {
  int x0 = r.x < 0 ? 0 : r.x;
  int y0 = r.y < by_ ? by_ : r.y;
  // 64-bit sums: caller rects may be huge
  long long x1 = (long long)r.x + r.w, y1 = (long long)r.y + r.h;
  if (x1 > w_) x1 = w_;
  if (y1 > by_ + bh_) y1 = by_ + bh_;
  if (r.w <= 0 || r.h <= 0 || x0 >= x1 || y0 >= y1) return false;
  r = Rect{x0, y0, (int)(x1 - x0), (int)(y1 - y0)};
  return true;
}

bool Canvas::visible(Rect r) const { return clip(r); }

void Canvas::fillRect(Rect r, Color c) {
  if (!clip(r)) return;
  for (int y = r.y; y < r.y + r.h; y++) {
    Color* row = buf_ + (y - by_) * w_ + r.x;
    for (int x = 0; x < r.w; x++) row[x] = c;
  }
}

void Canvas::clear(Color c) { fillRect(Rect{0, by_, w_, bh_}, c); }

void Canvas::blit(const uint8_t* src, const Color* pal, int sw, int sh, int x, int y, uint8_t flags,
                  bool transparent) {
  Rect r{x, y, sw, sh};
  if (!clip(r)) return;
  for (int dy = r.y; dy < r.y + r.h; dy++) {
    int sy = dy - y;
    if (flags & FlipY) sy = sh - 1 - sy;
    const uint8_t* srow = src + sy * sw;
    Color* drow = buf_ + (dy - by_) * w_;
    for (int dx = r.x; dx < r.x + r.w; dx++) {
      int sx = dx - x;
      if (flags & FlipX) sx = sw - 1 - sx;
      uint8_t idx = srow[sx];
      if (transparent && idx == 0) continue;
      drow[dx] = pal[idx];
    }
  }
}

void Canvas::sprite(const SpriteSheet& s, uint16_t frame, int x, int y, SpriteFlags f) {
  if (frame >= s.frames) return;
  blit(s.px + (uint32_t)frame * s.w * s.h, s.palette, s.w, s.h, x, y, f, s.transparent);
}

void Canvas::tilemap(const TileLayer& l, int cam_x, int cam_y) {
  const Tileset& ts = *l.set;
  int tw = ts.tile_w, th = ts.tile_h;
  if (tw == 0 || th == 0) return;
  // tile rows / columns that touch the band and the screen
  int ty0 = floorDiv(by_ + cam_y, th), ty1 = floorDiv(by_ + bh_ - 1 + cam_y, th);
  int tx0 = floorDiv(cam_x, tw), tx1 = floorDiv(w_ - 1 + cam_x, tw);
  for (int ty = ty0; ty <= ty1; ty++) {
    if (ty < 0 || ty >= l.h) continue;
    for (int tx = tx0; tx <= tx1; tx++) {
      if (tx < 0 || tx >= l.w) continue;
      uint8_t id = l.tiles[ty * l.w + tx];
      blit(ts.px + (uint32_t)id * tw * th, ts.palette, tw, th, tx * tw - cam_x, ty * th - cam_y, 0, false);
    }
  }
}

int renderFrame(const RenderConfig& cfg, Color* band_buf, uint32_t band_buf_pixels, uint32_t seed,
                RenderFn fn, void* ctx) {
  uint32_t lines = cfg.band_lines > cfg.logical_h ? cfg.logical_h : cfg.band_lines;
  if (lines == 0 || cfg.logical_w == 0 || !band_buf || lines * cfg.logical_w > band_buf_pixels)
    return InvalidArg;
  pge_hal_display_frame_begin();
  for (uint32_t y = 0; y < cfg.logical_h; y += lines) {
    uint32_t h = cfg.logical_h - y < lines ? cfg.logical_h - y : lines;
    Canvas canvas(band_buf, cfg.logical_w, cfg.logical_h, (int)y, (int)h, seed);
    canvas.clear(cfg.clear_color);
    fn(canvas, ctx);
    pge_rect r = {0, (uint16_t)y, cfg.logical_w, (uint16_t)h};
    int err = pge_hal_display_flush(r, band_buf);
    if (err == 0) err = pge_hal_display_wait();
    if (err != 0) return err;
  }
  pge_hal_display_frame_end();
  return Ok;
}

}  // namespace pge
