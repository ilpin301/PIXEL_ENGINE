#include <string.h>
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

// Draws src (sw x sh pixels, row stride `stride`): flips first, then a clockwise rotation from flags bits 2-3.
void Canvas::blit(const uint8_t* src, const Color* pal, int sw, int sh, int stride, int x, int y, uint8_t flags,
                  bool transparent) {
  const int rot = (flags >> 2) & 3;
  Rect r{x, y, (rot & 1) ? sh : sw, (rot & 1) ? sw : sh};
  if (!clip(r)) return;
  // Source index of output pixel (u, v), counted from the output's top-left corner. Affine in u and v, so the
  // loop below steps by constants; the formula is also evaluated one pixel outside, only to get the step.
  auto at = [&](int u, int v) {
    int sx, sy;
    switch (rot) {
      case 0: sx = u; sy = v; break;
      case 1: sx = v; sy = sh - 1 - u; break;
      case 2: sx = sw - 1 - u; sy = sh - 1 - v; break;
      default: sx = sw - 1 - v; sy = u; break;
    }
    if (flags & FlipX) sx = sw - 1 - sx;
    if (flags & FlipY) sy = sh - 1 - sy;
    return sy * stride + sx;
  };
  const int u0 = r.x - x, v0 = r.y - y;
  const int base = at(u0, v0), du = at(u0 + 1, v0) - base, dv = at(u0, v0 + 1) - base;
  for (int j = 0; j < r.h; j++) {
    Color* drow = buf_ + (r.y + j - by_) * w_ + r.x;
    int o = base + j * dv;
    for (int i = 0; i < r.w; i++, o += du) {
      uint8_t idx = src[o];
      if (transparent && idx == 0) continue;
      drow[i] = pal[idx];
    }
  }
}

void Canvas::sprite(const SpriteSheet& s, uint16_t frame, int x, int y, SpriteFlags f) {
  if (frame >= s.frames) return;
  if (s.cols == 0) {
    blit(s.px + (uint32_t)frame * s.w * s.h, s.palette, s.w, s.h, s.w, x, y, f, s.transparent);
    return;
  }
  const uint32_t stride = (uint32_t)s.cols * s.w;
  const uint8_t* src = s.px + (uint32_t)(frame / s.cols) * s.h * stride + (uint32_t)(frame % s.cols) * s.w;
  blit(src, s.palette, s.w, s.h, (int)stride, x, y, f, s.transparent);
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
      blit(ts.px + (uint32_t)id * tw * th, ts.palette, tw, th, tw, tx * tw - cam_x, ty * th - cam_y, 0, ts.transparent);
    }
  }
}

int renderFrame(const RenderConfig& cfg, Color* band_buf, uint32_t band_buf_pixels, uint32_t seed,
                RenderFn fn, void* ctx) {
  uint32_t lines = cfg.band_lines > cfg.logical_h ? cfg.logical_h : cfg.band_lines;
  if (lines == 0 || cfg.logical_w == 0 || !band_buf || lines * cfg.logical_w > band_buf_pixels)
    return InvalidArg;
  uint32_t s = cfg.scale;
  if (s < 1 || s > (uint32_t)kMaxScale || cfg.logical_w * s > 0xFFFFu || cfg.logical_h * s > 0xFFFFu)
    return InvalidArg;
  if (s > 1 && (!cfg.stage || (uint64_t)lines * s * cfg.logical_w * s > cfg.stage_pixels)) return InvalidArg;
  uint32_t dw = cfg.logical_w * s;
  pge_hal_display_frame_begin();
  for (uint32_t y = 0; y < cfg.logical_h; y += lines) {
    uint32_t h = cfg.logical_h - y < lines ? cfg.logical_h - y : lines;
    Canvas canvas(band_buf, cfg.logical_w, cfg.logical_h, (int)y, (int)h, seed);
    canvas.clear(cfg.clear_color);
    fn(canvas, ctx);
    Color* out = band_buf;
    if (s > 1) {  // one scaled row, then copies of it
      out = cfg.stage;
      Color* d = out;
      for (uint32_t row = 0; row < h; row++) {
        const Color* src = band_buf + row * cfg.logical_w;
        Color* first = d;
        for (uint32_t x = 0; x < cfg.logical_w; x++)
          for (uint32_t k = 0; k < s; k++) *d++ = src[x];
        for (uint32_t k = 1; k < s; k++, d += dw) memcpy(d, first, dw * sizeof(Color));
      }
    }
    pge_rect r = {0, (uint16_t)(y * s), (uint16_t)dw, (uint16_t)(h * s)};
    int err = pge_hal_display_flush(r, out);
    if (err == 0) err = pge_hal_display_wait();
    if (err != 0) return err;
  }
  pge_hal_display_frame_end();
  return Ok;
}

}  // namespace pge
