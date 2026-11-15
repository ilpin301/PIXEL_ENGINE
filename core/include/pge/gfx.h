#pragma once
#include <stdint.h>

namespace pge {

typedef uint16_t Color;  // RGB565
struct Rect { int x, y, w, h; };

enum Error : int { Ok = 0, InvalidArg = -1 };
enum SpriteFlags : uint8_t { None = 0, FlipX = 1, FlipY = 2 };

// 8-bit indexed; frame f at px + f*w*h; index 0 is transparent when `transparent` is true.
struct SpriteSheet { const uint8_t* px; const Color* palette; uint16_t w, h, frames; bool transparent; };
// Tile t at px + t*tile_w*tile_h; indexed, no transparency. Tile ids are not range-checked (no tile count yet).
struct Tileset { const uint8_t* px; const Color* palette; uint8_t tile_w, tile_h; };
struct TileLayer { const uint8_t* tiles; uint16_t w, h; const Tileset* set; };

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
  void sprite(const SpriteSheet& s, uint16_t frame, int x, int y, SpriteFlags f);
  // Prototype: integer camera offsets, not the Fix / Camera struct of the 02 spec.
  void tilemap(const TileLayer& l, int cam_x, int cam_y);

 private:
  bool clip(Rect& r) const;  // r -> intersection with band and screen; false if empty
  void blit(const uint8_t* src, const Color* pal, int sw, int sh, int x, int y, uint8_t flags, bool transparent);
  Color* buf_;
  int w_, h_, by_, bh_;
  uint32_t seed_;
};

struct RenderConfig { uint16_t logical_w, logical_h; uint16_t band_lines; Color clear_color; };
typedef void (*RenderFn)(Canvas&, void* ctx);

// Draws one frame band by band through the display HAL (K2). Returns Ok, InvalidArg, or a nonzero
// flush/wait result. band_lines > logical_h is clamped; the last band may be shorter.
int renderFrame(const RenderConfig& cfg, Color* band_buf, uint32_t band_buf_pixels, uint32_t seed,
                RenderFn fn, void* ctx);

}  // namespace pge
