#pragma once
#include <stdint.h>
#include "pge/error.h"

namespace pge {

typedef uint16_t Color;  // RGB565
struct Rect { int x, y, w, h; };

// r is a rect relative to the top-left of a picture of view_w x view_h pixels. Returns the same rect after that picture
// is turned clockwise by quarter_turns (only the low 2 bits count), relative to the top-left of the turned picture
// (view_h x view_w for odd turns). w and h of the rect swap for odd turns. 1 turn: new x = view_h - (y + h), new y = x;
// 2 turns: new x = view_w - (x + w), new y = view_h - (y + h); 3 turns: new x = y, new y = view_w - (x + w).
Rect turnRect(Rect r, int view_w, int view_h, int quarter_turns);

// Flips apply to the stored image first, then the result is rotated clockwise by the field in bits 2-3
// (use at most one of Rot90 / Rot180 / Rot270). With Rot90 / Rot270 the drawn rectangle is h wide and w tall;
// (x, y) is always its top-left corner.
enum SpriteFlags : uint8_t { None = 0, FlipX = 1, FlipY = 2, Rot90 = 4, Rot180 = 8, Rot270 = 12 };
inline constexpr SpriteFlags operator|(SpriteFlags a, SpriteFlags b) {
  return static_cast<SpriteFlags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

// 8-bit indexed; index 0 is transparent when `transparent` is true. cols == 0: frames are stored one after another,
// frame f at px + f*w*h. cols > 0: an atlas with `cols` frames per row (row stride cols*w), frame f at column
// f % cols, row f / cols.
struct SpriteSheet { const uint8_t* px; const Color* palette; uint16_t w, h, frames; bool transparent; uint16_t cols = 0; };
// Tile t at px + t*tile_w*tile_h; indexed. With `transparent` pixel index 0 is skipped (layers drawn over others),
// otherwise every pixel is written. Tile ids are not range-checked (no tile count yet).
struct Tileset { const uint8_t* px; const Color* palette; uint8_t tile_w, tile_h; bool transparent = false; };
struct TileLayer { const uint8_t* tiles; uint16_t w, h; const Tileset* set; };

// Glyph (ch - first) is h bytes at rows + (ch - first) * h, one byte per glyph row; bit (w - 1 - x) is pixel x (w <= 8).
struct Font { const uint8_t* rows; uint8_t w, h, first, count, spacing; };
extern const Font kFont5x7;  // ASCII 32..126

constexpr int kMaxCoord = 16384;  // line / circle / text arguments beyond this are dropped and counted
constexpr int kMaxScale = 3;

// Valid only inside a RenderFn; made by renderFrame. All coordinates are logical-screen
// coordinates; everything is clipped to the current band and the screen.
class Canvas {
 public:
  Canvas(Color* buf, int w, int h, int band_y, int band_h, uint32_t seed);
  int width() const { return w_; }
  int height() const { return h_; }
  uint32_t frameSeed() const { return seed_; }
  bool visible(Rect r) const;  // intersects the current band and the screen
  void clear(Color c);
  void fillRect(Rect r, Color c);
  void pixel(int x, int y, Color c) { put(x, y, c); }
  void line(int x0, int y0, int x1, int y1, Color c);  // both ends inclusive
  void rect(Rect r, Color c);                          // 1 px outline inside r
  void circle(int cx, int cy, int r, Color c);
  void fillCircle(int cx, int cy, int r, Color c);
  void text(const Font& f, int x, int y, const char* s, Color c);  // transparent background, newline starts a line
  uint16_t dropped() const { return drops_; }  // invalid-argument draws in this canvas (saturating)
  void sprite(const SpriteSheet& s, uint16_t frame, int x, int y, SpriteFlags f);
  // Integer camera offsets: cam_x / cam_y are the top-left of the axis-aligned world rect the screen shows; see
  // camera.h for Camera and parallax layers. "orient" = clockwise quarter turns applied to the whole world picture
  // before it is shown, 0..3 (only the low 2 bits count); each tile is drawn turned by orient.
  void tilemap(const TileLayer& l, int cam_x, int cam_y, uint8_t orient = 0);

 private:
  void put(int x, int y, Color c) {  // the one clipped pixel write of every primitive
    if (x >= 0 && x < w_ && y >= by_ && y < by_ + bh_) buf_[(y - by_) * w_ + x] = c;
  }
  // Counts only on the band that starts at row 0, so one bad call per frame counts once, not once per band.
  void drop() { if (by_ == 0 && drops_ != 0xFFFF) drops_++; }
  bool clip(Rect& r) const;  // r -> intersection with band and screen; false if empty
  void blit(const uint8_t* src, const Color* pal, int sw, int sh, int stride, int x, int y, uint8_t flags, bool transparent);
  void blitPlain(const uint8_t* src, const Color* pal, int sw, int sh, int stride, int x, int y, bool transparent);
  Color* buf_;
  int w_, h_, by_, bh_;
  uint32_t seed_;
  uint16_t drops_ = 0;
};

// Widest line of s in pixels (no trailing spacing); 0 for null or empty.
int textWidth(const Font& f, const char* s);

// scale > 1 needs `stage` of at least band_lines*scale * logical_w*scale pixels. The window origin of the 02 spec
// is not implemented (deferred to the windowed flush, S5.6): the logical screen always fills the display.
struct RenderConfig {
  uint16_t logical_w, logical_h;
  uint16_t band_lines;
  Color clear_color;
  uint8_t scale = 1;
  Color* stage = nullptr;
  uint32_t stage_pixels = 0;
};
typedef void (*RenderFn)(Canvas&, void* ctx);

// Draws one frame band by band through the display HAL (K2). Returns Ok, InvalidArg, or a nonzero
// flush/wait result. band_lines > logical_h is clamped; the last band may be shorter. Flush rects are in
// display coordinates (logical * scale).
int renderFrame(const RenderConfig& cfg, Color* band_buf, uint32_t band_buf_pixels, uint32_t seed,
                RenderFn fn, void* ctx);

}  // namespace pge
