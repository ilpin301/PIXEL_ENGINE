#include "pge/collision.h"

namespace pge {

uint16_t tileFlags(const TileGrid& g, int tx, int ty) {
  if (tx < 0 || ty < 0 || tx >= g.w || ty >= g.h) return kTileSolid;
  return g.props[g.tiles[ty * g.w + tx]].flags;
}

namespace {

// Floor division for b > 0 (C truncation is wrong for negative a).
int fdiv(int a, int b) {
  int q = a / b;
  return (a % b != 0 && a < 0) ? q - 1 : q;
}

// Blocking tiles under a box: bounds of their columns and rows (valid when blocked).
struct Hit {
  bool blocked;
  int c0, c1, r0, r1;
};

// Scans every tile the box overlaps. `down` enables one-way tiles, which block only if prev_bottom is at or above
// the tile's top edge. Scanning the whole overlapped rectangle keeps this simple; boxes are a few tiles at most.
Hit scan(const TileGrid& g, int x, int y, int w, int h, bool down, int prev_bottom) {
  Hit r{false, 0, 0, 0, 0};
  const int tx0 = fdiv(x, g.tile_w), tx1 = fdiv(x + w - 1, g.tile_w);
  const int ty0 = fdiv(y, g.tile_h), ty1 = fdiv(y + h - 1, g.tile_h);
  for (int ty = ty0; ty <= ty1; ty++) {
    for (int tx = tx0; tx <= tx1; tx++) {
      const uint16_t f = tileFlags(g, tx, ty);
      const bool blk = (f & kTileSolid) || (down && (f & kTileOneWay) && prev_bottom <= ty * g.tile_h);
      if (!blk) continue;
      if (!r.blocked) {
        r = Hit{true, tx, tx, ty, ty};
      } else {
        if (tx < r.c0) r.c0 = tx;
        if (tx > r.c1) r.c1 = tx;
        if (ty < r.r0) r.r0 = ty;
        if (ty > r.r1) r.r1 = ty;
      }
    }
  }
  return r;
}

}  // namespace

MoveResult moveAndCollide(const TileGrid& g, Body& b, int dx, int dy) {
  MoveResult res{false, false, false, 0};
  if (g.tile_w == 0 || g.tile_h == 0) return res;  // invalid grid: nothing sensible to do

  // X axis: sub-steps of at most one tile width.
  int rem = dx < 0 ? -dx : dx;
  const int sx = dx < 0 ? -1 : 1;
  while (rem > 0) {
    const int step = rem < g.tile_w ? rem : g.tile_w;
    const int nx = b.x + sx * step;
    const Hit h = scan(g, nx, b.y, b.w, b.h, false, 0);
    if (h.blocked) {
      b.x = sx > 0 ? h.c0 * g.tile_w - b.w : (h.c1 + 1) * g.tile_w;
      res.hit_x = true;
      break;
    }
    b.x = nx;
    rem -= step;
  }

  // Y axis: sub-steps of at most one tile height; one-way tiles only matter while moving down.
  rem = dy < 0 ? -dy : dy;
  const int sy = dy < 0 ? -1 : 1;
  while (rem > 0) {
    const int step = rem < g.tile_h ? rem : g.tile_h;
    const int ny = b.y + sy * step;
    const Hit h = scan(g, b.x, ny, b.w, b.h, sy > 0, b.y + b.h);
    if (h.blocked) {
      b.y = sy > 0 ? h.r0 * g.tile_h - b.h : (h.r1 + 1) * g.tile_h;
      res.hit_y = true;
      break;
    }
    b.y = ny;
    rem -= step;
  }

  res.on_ground = scan(g, b.x, b.y + 1, b.w, b.h, true, b.y + b.h).blocked;

  const int tx0 = fdiv(b.x, g.tile_w), tx1 = fdiv(b.x + b.w - 1, g.tile_w);
  const int ty0 = fdiv(b.y, g.tile_h), ty1 = fdiv(b.y + b.h - 1, g.tile_h);
  for (int ty = ty0; ty <= ty1; ty++)
    for (int tx = tx0; tx <= tx1; tx++) res.touched_flags |= tileFlags(g, tx, ty);
  return res;
}

}  // namespace pge
