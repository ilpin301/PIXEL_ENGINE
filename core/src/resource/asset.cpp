#include "pge/asset.h"

namespace pge {

namespace {

uint16_t rd16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t rd32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

struct Sheet { const uint8_t* px; const Color* pal; uint16_t w, h, frames, cols; bool transparent; };

Error parseSheet(const void* blob, uint32_t len, AssetType type, Sheet& s) {
  AssetView v;
  Error e = assetCheck(blob, len, type, v);
  if (e != Ok) return e;
  PGE_CHECK((reinterpret_cast<uintptr_t>(blob) & 1) == 0, InvalidArg);
  PGE_CHECK(v.size >= 12, Corrupt);
  const uint8_t* p = v.payload;
  const uint16_t w = rd16(p), h = rd16(p + 2), frames = rd16(p + 4), cols = rd16(p + 6), ncolors = rd16(p + 8);
  PGE_CHECK(w >= 1 && h >= 1 && frames >= 1 && ncolors >= 1 && ncolors <= 256 && p[10] <= 1 && p[11] == 0, Corrupt);
  uint64_t pix = static_cast<uint64_t>(w) * h * frames;
  if (cols) pix = static_cast<uint64_t>(cols) * w * (((static_cast<uint64_t>(frames) + cols - 1) / cols) * h);
  PGE_CHECK(v.size == 12 + 2 * static_cast<uint64_t>(ncolors) + pix, Corrupt);
  s.pal = reinterpret_cast<const Color*>(p + 12);
  s.px = p + 12 + 2 * static_cast<uint32_t>(ncolors);
  for (uint64_t i = 0; i < pix; i++) PGE_CHECK(s.px[i] < ncolors, Corrupt);  // blobs from Storage are untrusted
  s.w = w;
  s.h = h;
  s.frames = frames;
  s.cols = cols;
  s.transparent = p[10] != 0;
  return Ok;
}

}  // namespace

Error assetCheck(const void* blob, uint32_t len, AssetType expect, AssetView& out) {
  PGE_CHECK(blob != nullptr, InvalidArg);
  PGE_CHECK(len >= kAssetHeaderSize, Corrupt);
  const uint8_t* b = static_cast<const uint8_t*>(blob);
  const uint32_t size = rd32(b + 8);
  PGE_CHECK(rd32(b) == kAssetMagic && b[5] == kAssetVersion && rd16(b + 6) == 0, Corrupt);
  PGE_CHECK(b[4] == static_cast<uint8_t>(expect), Corrupt);
  PGE_CHECK(size <= len - kAssetHeaderSize, Corrupt);
  out.type = expect;
  out.payload = b + kAssetHeaderSize;
  out.size = size;
  return Ok;
}

Error assetSprites(const void* blob, uint32_t len, SpriteSheet& out) {
  Sheet s;
  Error e = parseSheet(blob, len, AssetType::Sprites, s);
  if (e != Ok) return e;
  out.px = s.px;
  out.palette = s.pal;
  out.w = s.w;
  out.h = s.h;
  out.frames = s.frames;
  out.transparent = s.transparent;
  out.cols = s.cols;
  return Ok;
}

Error assetTileset(const void* blob, uint32_t len, Tileset& out) {
  Sheet s;
  Error e = parseSheet(blob, len, AssetType::Tileset, s);
  if (e != Ok) return e;
  PGE_CHECK(s.cols == 0 && s.w <= 255 && s.h <= 255, Corrupt);
  out.px = s.px;
  out.palette = s.pal;
  out.tile_w = static_cast<uint8_t>(s.w);
  out.tile_h = static_cast<uint8_t>(s.h);
  out.transparent = s.transparent;
  return Ok;
}

Error assetTilemap(const void* blob, uint32_t len, TilemapView& out) {
  AssetView v;
  Error e = assetCheck(blob, len, AssetType::Tilemap, v);
  if (e != Ok) return e;
  PGE_CHECK(v.size >= 4, Corrupt);
  const uint16_t w = rd16(v.payload), h = rd16(v.payload + 2);
  PGE_CHECK(w >= 1 && h >= 1 && v.size == 4 + static_cast<uint32_t>(w) * h, Corrupt);
  out.w = w;
  out.h = h;
  out.tiles = v.payload + 4;
  return Ok;
}

}  // namespace pge
