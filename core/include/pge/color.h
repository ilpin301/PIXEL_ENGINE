#pragma once
#include "pge/gfx.h"

namespace pge {

constexpr Color rgb(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<Color>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}
// Expand to 8 bits by replicating the high bits, so 0xFFFF -> 255.
constexpr uint8_t red8(Color c) { return static_cast<uint8_t>(((c >> 8) & 0xF8) | (c >> 13)); }
constexpr uint8_t green8(Color c) { return static_cast<uint8_t>(((c >> 3) & 0xFC) | ((c >> 9) & 3)); }
constexpr uint8_t blue8(Color c) { return static_cast<uint8_t>(((c << 3) & 0xF8) | ((c >> 2) & 7)); }

// A palette is just a `const Color[]` (SpriteSheet and Tileset take `const Color*`); there is no Palette class.
inline constexpr Color kPalette16[16] = {
    rgb(0, 0, 0),       rgb(255, 255, 255), rgb(255, 0, 0),   rgb(0, 255, 0),
    rgb(0, 0, 255),     rgb(255, 255, 0),   rgb(0, 255, 255), rgb(255, 0, 255),
    rgb(64, 64, 64),    rgb(128, 128, 128), rgb(192, 192, 192), rgb(255, 128, 0),
    rgb(128, 64, 0),    rgb(0, 128, 0),     rgb(0, 0, 128),   rgb(255, 128, 192)};

}  // namespace pge
