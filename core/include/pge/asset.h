#pragma once
// Asset blob format (docs/architecture/03 section 4.4): 12-byte header + payload, little-endian, written by
// tools/pge_assets.py. The view functions are zero-copy: the engine structs point into the blob, which must outlive
// them. Blobs may come from Storage, so everything is validated (Corrupt on any violation).
#include <stdint.h>
#include "pge/error.h"
#include "pge/gfx.h"
#include "pge/resource.h"

namespace pge {

constexpr uint32_t kAssetMagic = 0x41454750;  // bytes "PGEA"
constexpr uint8_t kAssetVersion = 1;
constexpr uint32_t kAssetHeaderSize = 12;  // magic u32, type u8, version u8, reserved u16 (0), payload size u32

struct AssetView { AssetType type; const uint8_t* payload; uint32_t size; };

// nullptr blob -> InvalidArg. Bad magic / version / reserved / type != expect / len < 12 / 12 + size > len -> Corrupt.
// Trailing bytes after the payload are allowed. Alignment is not checked here.
Error assetCheck(const void* blob, uint32_t len, AssetType expect, AssetView& out);

// Payload: u16 w, h, frames, cols, ncolors; u8 transparent (0/1), u8 pad (0); ncolors RGB565 colors; 8-bit pixels
// (cols == 0: w*h*frames bytes stacked; cols > 0: atlas of (cols*w) x (ceil(frames/cols)*h) bytes). The blob must be
// 2-byte aligned (the palette is used in place), else InvalidArg. Every pixel index must be < ncolors.
Error assetSprites(const void* blob, uint32_t len, SpriteSheet& out);
// Same payload with cols == 0 and w, h = tile size in 1..255; `frames` is the tile count (not used by Tileset).
Error assetTileset(const void* blob, uint32_t len, Tileset& out);

// Payload: u8 w (1..8), u8 h (>= 1), u8 first, u8 count, u8 spacing, u8 nranges; nranges x {u16 first, u16 count, u16 glyph}
// (the FontRange table, used in place); then N glyphs of h bytes, N = (size - 6 - 6 * nranges) / h, which must divide
// exactly. Codepoints first..first + count - 1 (the base block, count may be 0) are glyphs 0..count - 1, count <= N. Ranges
// are sorted by first and do not overlap each other or the base block; each has count >= 1, first + count <= 0x10000 and
// glyph + count <= N. The blob must be 2-byte aligned (the table is used in place), else InvalidArg; the table is read in
// native byte order, so little-endian targets only, like the sprite palette. `out` is untouched on error.
Error assetFont(const void* blob, uint32_t len, Font& out);

// Payload: u16 w, u16 h, then w*h u8 tile ids (w, h >= 1, size exact).
struct TilemapView { uint16_t w, h; const uint8_t* tiles; };
Error assetTilemap(const void* blob, uint32_t len, TilemapView& out);

// Text table (type 7): the same strings in nlang languages, selected at run time. Zero-copy: every pointer points into
// the blob, which must outlive the table. Language 0 is the fallback. The table never returns nullptr.
struct StringTable {
  const uint8_t* codes = nullptr;  // nlang x 2 bytes, lowercase letters ("en")
  const uint8_t* offs = nullptr;   // nlang * count little-endian u32 pool offsets, language-major; 0xFFFFFFFF = missing
  const char* pool = nullptr;      // NUL-terminated strings
  uint32_t pool_size = 0;
  uint8_t nlang = 0;
  uint16_t count = 0;
  // String id in language lang; missing there -> language 0. id >= count, lang >= nlang or an empty table -> "".
  const char* get(uint8_t lang, uint16_t id) const;
  // Index of a 2-letter language code (exact match), -1 for unknown, nullptr or a length other than 2.
  int langIndex(const char* code) const;
};

// Payload: u8 nlang (1..16), u8 reserved (0), u16 nstr (>= 1); nlang x 2 code bytes ('a'..'z'); nlang * nstr x u32
// pool offsets, language-major (index lang * nstr + id), 0xFFFFFFFF = missing (never in language 0); then the pool:
// >= 1 byte, last byte 0, every offset < pool size. Byte reads only, so no alignment is needed. `out` is untouched on
// error.
Error assetStrings(const void* blob, uint32_t len, StringTable& out);

}  // namespace pge
