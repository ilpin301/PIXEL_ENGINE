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

// Next codepoint of s (*s != 0); advances s. Any stray, overlong, surrogate, out-of-range or truncated sequence gives '?'
// and eats exactly one byte, so s never moves past the NUL.
static uint32_t nextCp(const char*& s) {
  const uint8_t* p = (const uint8_t*)s++;
  uint32_t b = *p;
  if (b < 0x80) return b;
  if (b < 0xC2 || b > 0xF4) return '?';  // stray continuation byte, or a lead that only starts overlong / too large sequences
  int n = b >= 0xF0 ? 3 : b >= 0xE0 ? 2 : 1;  // continuation bytes still owed
  uint32_t cp = b & (0x3F >> n);
  for (int i = 1; i <= n; i++) {
    if ((p[i] & 0xC0) != 0x80) return '?';  // also true for the NUL
    cp = cp << 6 | (p[i] & 0x3F);
  }
  if ((n == 2 && cp < 0x800) || (n == 3 && cp < 0x10000) || cp > 0x10FFFF || (cp >= 0xD800 && cp < 0xE000)) return '?';
  s += n;
  return cp;
}

// Glyph index of codepoint cp in f, or -1.
static int glyphOf(const Font& f, uint32_t cp) {
  if (cp >= f.first && cp < (uint32_t)f.first + f.count) return (int)(cp - f.first);
  for (int i = 0; i < f.nranges && cp >= f.ranges[i].first; i++)  // sorted: nothing further can match
    if (cp < (uint32_t)f.ranges[i].first + f.ranges[i].count) return f.ranges[i].glyph + (int)(cp - f.ranges[i].first);
  return -1;
}

void Canvas::text(const Font& f, int x, int y, const char* s, Color c) {
  if (!s || !f.rows || tooBig(x) || tooBig(y)) { drop(); return; }
  const int x0 = x, q = glyphOf(f, '?');  // q < 0: unknown glyphs draw nothing but still advance
  while (*s) {
    if (*s == '\n') { s++; x = x0; y += f.h + f.spacing; continue; }
    int gi = glyphOf(f, nextCp(s));
    if (gi < 0) gi = q;
    if (gi >= 0 && visible(Rect{x, y, f.w, f.h})) {
      const uint8_t* g = f.rows + gi * f.h;
      for (int gy = 0; gy < f.h; gy++)
        for (int gx = 0; gx < f.w; gx++)
          if (g[gy] & (1 << (f.w - 1 - gx))) put(x + gx, y + gy, c);
    }
    x += f.w + f.spacing;
  }
}

int textWidth(const Font& f, const char* s) {
  if (!s) return 0;
  int best = 0, n = 0;  // n = glyphs on the current line
  for (;;) {
    const bool end = *s == 0;
    if (*s == '\n' || end) {
      int w = n ? n * (f.w + f.spacing) - f.spacing : 0;
      if (w > best) best = w;
      n = 0;
      if (end) break;
      s++;
    } else {
      nextCp(s);
      n++;
    }
  }
  return best;
}

}  // namespace pge
