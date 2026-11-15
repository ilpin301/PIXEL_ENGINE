// Canvas primitives. Every pixel goes through Canvas::put or fillRect, so a band renders exactly the same
// pixels as the full frame; each shape is walked from fixed start values, never from the band.
#include "pge/gfx.h"

namespace pge {

static bool tooBig(int v) { return v < -kMaxCoord || v > kMaxCoord; }
static int clampI(long long v) { return v > (1 << 30) ? (1 << 30) : v < -(1 << 30) ? -(1 << 30) : (int)v; }

static int isqrt(int n) {  // digit-by-digit, n >= 0
  int r = 0, b = 1 << 30;
  while (b > n) b >>= 2;
  while (b) {
    if (n >= r + b) { n -= r + b; r = (r >> 1) + b; }
    else r >>= 1;
    b >>= 2;
  }
  return r;
}

void Canvas::line(int x0, int y0, int x1, int y1, Color c) {
  if (tooBig(x0) || tooBig(y0) || tooBig(x1) || tooBig(y1)) { drop(); return; }
  int minx = x0 < x1 ? x0 : x1, miny = y0 < y1 ? y0 : y1;
  if (!visible(Rect{minx, miny, (x0 < x1 ? x1 - x0 : x0 - x1) + 1, (y0 < y1 ? y1 - y0 : y0 - y1) + 1})) return;
  // shortcut: per-band cost is O(line length) even when most of it is off-band, upgrade to Liang-Barsky if profiling shows it.
  int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
  int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;  // dy <= 0
  int err = dx + dy;
  for (;;) {
    put(x0, y0, c);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

void Canvas::rect(Rect r, Color c) {
  if (r.w <= 0 || r.h <= 0) return;
  int xr = clampI((long long)r.x + r.w - 1), yb = clampI((long long)r.y + r.h - 1);
  fillRect(Rect{r.x, r.y, r.w, 1}, c);
  fillRect(Rect{r.x, yb, r.w, 1}, c);
  fillRect(Rect{r.x, r.y, 1, r.h}, c);
  fillRect(Rect{xr, r.y, 1, r.h}, c);
}

void Canvas::circle(int cx, int cy, int r, Color c) {
  if (r < 0 || r > kMaxCoord || tooBig(cx) || tooBig(cy)) { drop(); return; }
  if (!visible(Rect{cx - r, cy - r, 2 * r + 1, 2 * r + 1})) return;
  int x = r, y = 0, err = 1 - r;
  while (x >= y) {
    put(cx + x, cy + y, c); put(cx - x, cy + y, c); put(cx + x, cy - y, c); put(cx - x, cy - y, c);
    put(cx + y, cy + x, c); put(cx - y, cy + x, c); put(cx + y, cy - x, c); put(cx - y, cy - x, c);
    y++;
    if (err < 0) err += 2 * y + 1;
    else { x--; err += 2 * (y - x + 1); }
  }
}

void Canvas::fillCircle(int cx, int cy, int r, Color c) {
  if (r < 0 || r > kMaxCoord || tooBig(cx) || tooBig(cy)) { drop(); return; }
  int d0 = by_ - cy < -r ? -r : by_ - cy, d1 = by_ + bh_ - 1 - cy > r ? r : by_ + bh_ - 1 - cy;
  for (int dy = d0; dy <= d1; dy++) {
    int hw = isqrt(r * r + r - dy * dy);
    fillRect(Rect{cx - hw, cy + dy, 2 * hw + 1, 1}, c);
  }
}

void Canvas::text(const Font& f, int x, int y, const char* s, Color c) {
  if (!s || !f.rows || tooBig(x) || tooBig(y)) { drop(); return; }
  const int x0 = x;
  const bool has_q = '?' >= f.first && '?' < f.first + f.count;
  for (; *s; s++) {
    if (*s == '\n') { x = x0; y += f.h + f.spacing; continue; }
    int ch = (unsigned char)*s;
    if (ch < f.first || ch >= f.first + f.count) ch = '?';
    if ((ch != '?' || has_q) && visible(Rect{x, y, f.w, f.h})) {
      const uint8_t* g = f.rows + (ch - f.first) * f.h;
      for (int gy = 0; gy < f.h; gy++)
        for (int gx = 0; gx < f.w; gx++)
          if (g[gy] & (1 << (f.w - 1 - gx))) put(x + gx, y + gy, c);
    }
    x += f.w + f.spacing;
  }
}

int textWidth(const Font& f, const char* s) {
  if (!s) return 0;
  int best = 0, n = 0;
  for (;; s++) {
    if (*s == '\n' || *s == 0) {
      int w = n ? n * (f.w + f.spacing) - f.spacing : 0;
      if (w > best) best = w;
      n = 0;
      if (!*s) break;
    } else {
      n++;
    }
  }
  return best;
}

}  // namespace pge
