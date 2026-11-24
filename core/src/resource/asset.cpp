#include "pge/asset.h"

namespace pge {

namespace {

constexpr uint32_t kStringMissing = 0xFFFFFFFFu;

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

Error assetFont(const void* blob, uint32_t len, Font& out) {
  static_assert(sizeof(FontRange) == 6 && alignof(FontRange) == 2, "the blob's range table is used as FontRange[]");
  AssetView v;
  Error e = assetCheck(blob, len, AssetType::Font, v);
  if (e != Ok) return e;
  PGE_CHECK((reinterpret_cast<uintptr_t>(blob) & 1) == 0, InvalidArg);
  PGE_CHECK(v.size >= 6, Corrupt);
  const uint8_t* p = v.payload;
  const uint8_t w = p[0], h = p[1], first = p[2], count = p[3], nr = p[5];
  const uint32_t table = 6u * nr;
  PGE_CHECK(w >= 1 && w <= 8 && h >= 1 && v.size - 6 >= table && (v.size - 6 - table) % h == 0, Corrupt);
  const uint32_t glyphs = (v.size - 6 - table) / h;
  PGE_CHECK(count <= glyphs, Corrupt);
  const FontRange* r = reinterpret_cast<const FontRange*>(p + 6);
  uint32_t prev_end = 0;  // end of the previous range: the next one must start at or after it
  for (uint32_t i = 0; i < nr; i++) {
    const uint32_t rf = r[i].first, rc = r[i].count, end = rf + rc;
    PGE_CHECK(rc >= 1 && rf >= prev_end && end <= 0x10000 && r[i].glyph + rc <= glyphs, Corrupt);
    PGE_CHECK(count == 0 || end <= first || rf >= static_cast<uint32_t>(first) + count, Corrupt);  // not in the base block
    prev_end = end;
  }
  out.rows = p + 6 + table;
  out.w = w;
  out.h = h;
  out.first = first;
  out.count = count;
  out.spacing = p[4];
  out.ranges = nr ? r : nullptr;
  out.nranges = nr;
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

Error assetStrings(const void* blob, uint32_t len, StringTable& out) {
  AssetView v;
  Error e = assetCheck(blob, len, AssetType::Strings, v);
  if (e != Ok) return e;
  PGE_CHECK(v.size >= 4, Corrupt);
  const uint8_t* p = v.payload;
  const uint32_t nlang = p[0], nstr = rd16(p + 2);
  PGE_CHECK(nlang >= 1 && nlang <= 16 && p[1] == 0 && nstr >= 1, Corrupt);
  const uint64_t head = 4 + 2 * static_cast<uint64_t>(nlang) + 4 * static_cast<uint64_t>(nlang) * nstr;
  PGE_CHECK(head < v.size, Corrupt);  // at least one pool byte
  const uint32_t pool_size = v.size - static_cast<uint32_t>(head);
  const uint8_t* offs = p + 4 + 2 * nlang;
  PGE_CHECK(p[v.size - 1] == 0, Corrupt);
  for (uint32_t i = 0; i < 2 * nlang; i++) PGE_CHECK(p[4 + i] >= 'a' && p[4 + i] <= 'z', Corrupt);
  for (uint32_t i = 0; i < nlang * nstr; i++) {
    const uint32_t o = rd32(offs + 4 * i);
    PGE_CHECK(o < pool_size || (o == kStringMissing && i >= nstr), Corrupt);  // language 0 (the first nstr) has no gaps
  }
  out.codes = p + 4;
  out.offs = offs;
  out.pool = reinterpret_cast<const char*>(p + head);
  out.pool_size = pool_size;
  out.nlang = static_cast<uint8_t>(nlang);
  out.count = static_cast<uint16_t>(nstr);
  return Ok;
}

const char* StringTable::get(uint8_t lang, uint16_t id) const {
  if (lang >= nlang || id >= count) return "";
  uint32_t o = rd32(offs + 4 * (static_cast<uint32_t>(lang) * count + id));
  if (o == kStringMissing) o = rd32(offs + 4 * id);  // fall back to language 0
  return o == kStringMissing ? "" : pool + o;
}

int StringTable::langIndex(const char* code) const {
  if (code == nullptr || code[0] == 0 || code[1] == 0 || code[2] != 0) return -1;
  for (int i = 0; i < nlang; i++)
    if (codes[2 * i] == static_cast<uint8_t>(code[0]) && codes[2 * i + 1] == static_cast<uint8_t>(code[1])) return i;
  return -1;
}

}  // namespace pge
