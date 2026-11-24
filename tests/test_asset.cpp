// Asset blob tests (S5.4, S8.7b): golden sprites / tileset / tilemap / font / strings blobs, rejection of every
// malformed variant, and (when the Python tool is available) byte-identity of the generated blobs with the goldens.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <utility>
#include "pge/asset.h"
#include "pge/color.h"
#include "pge/gfx.h"
#include "pge/memory.h"
#include "pge/settings.h"

#ifdef PGE_HAVE_GEN
#include "sample_font.h"
#include "sample_sprites.h"
#include "sample_strings.h"
#include "sample_tilemap.h"
#endif

#define CHECK(c)                                                          \
  do {                                                                    \
    if (!(c)) {                                                           \
      std::printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); \
      return 1;                                                           \
    }                                                                     \
  } while (0)

using namespace pge;

alignas(4) static const uint8_t kSprites[38] = {
    0x50, 0x47, 0x45, 0x41, 0x00, 0x01, 0x00, 0x00, 0x1A, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x02, 0x00, 0x02, 0x00, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x00, 0xF8, 0xE0, 0x07,
    0x00, 0x01, 0x01, 0x00, 0x02, 0x02, 0x00, 0x01};
alignas(4) static const uint8_t kTilemap[22] = {
    0x50, 0x47, 0x45, 0x41, 0x06, 0x01, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00,
    0x03, 0x00, 0x02, 0x00, 0x00, 0x01, 0x02, 0x03, 0x00, 0xFE};

// Font blob (same bytes as the Python tool's GOLDEN_FONT): 3x2 glyphs, base block 'A'..'B' = glyphs 0..1, one range
// U+0416 (a Cyrillic letter) = glyph 2. Payload offsets from the blob start: w 12, h 13, first 14, count 15, spacing 16,
// nranges 17, range 18..23 (first, count, glyph as u16), glyph rows 24..29.
alignas(4) static const uint8_t kFont[30] = {
    0x50, 0x47, 0x45, 0x41, 0x02, 0x01, 0x00, 0x00, 0x12, 0x00, 0x00, 0x00,
    0x03, 0x02, 0x41, 0x02, 0x01, 0x01, 0x16, 0x04, 0x01, 0x00, 0x02, 0x00,
    0x05, 0x07, 0x02, 0x05, 0x07, 0x01};
// Two ranges, 5 glyphs: U+0416 = glyph 2, U+0430..U+0431 = glyphs 3..4.
alignas(4) static const uint8_t kFont2[40] = {
    0x50, 0x47, 0x45, 0x41, 0x02, 0x01, 0x00, 0x00, 0x1C, 0x00, 0x00, 0x00,
    0x03, 0x02, 0x41, 0x02, 0x01, 0x02, 0x16, 0x04, 0x01, 0x00, 0x02, 0x00, 0x30, 0x04, 0x02, 0x00, 0x03, 0x00,
    0x05, 0x07, 0x02, 0x05, 0x07, 0x01, 0x04, 0x03, 0x06, 0x02};

// Strings blob (same bytes as the Python tool's GOLDEN_STRINGS): languages "en", "ru"; strings 0 = "Hi" / U+0414
// U+0430, 1 = "Yo" / missing in "ru". Offsets from the blob start: nlang 12, reserved 13, nstr 14, codes 16..19, pool
// offsets 20..35 (language-major: en 0, 1; ru 0, 1), pool 36..46.
alignas(4) static const uint8_t kStrings[47] = {
    0x50, 0x47, 0x45, 0x41, 0x07, 0x01, 0x00, 0x00, 0x23, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x02, 0x00, 0x65, 0x6E, 0x72, 0x75,
    0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
    0x48, 0x69, 0x00, 0x59, 0x6F, 0x00, 0xD0, 0x94, 0xD0, 0xB0, 0x00};

// Mutable aligned copy of a blob with spare room.
struct Blob {
  alignas(4) uint8_t d[64];
  uint32_t n;
  Blob(const uint8_t* src, uint32_t len) : n(len) {
    std::memset(d, 0, sizeof(d));
    std::memcpy(d, src, len);
  }
  void setSize(uint32_t s) {
    d[8] = (uint8_t)s;
    d[9] = (uint8_t)(s >> 8);
    d[10] = (uint8_t)(s >> 16);
    d[11] = (uint8_t)(s >> 24);
  }
};

// kind 0: sprites, 1: tileset, 2: tilemap, 3: font.
static Error parse(int kind, const void* p, uint32_t n) {
  SpriteSheet s;
  Tileset t;
  TilemapView m;
  Font f;
  if (kind == 0) return assetSprites(p, n, s);
  if (kind == 1) return assetTileset(p, n, t);
  if (kind == 3) return assetFont(p, n, f);
  return assetTilemap(p, n, m);
}

static int testGolden() {
  SpriteSheet s;
  CHECK(assetSprites(kSprites, sizeof(kSprites), s) == Ok);
  CHECK(s.w == 2 && s.h == 2 && s.frames == 2 && s.cols == 0 && s.transparent);
  CHECK(s.palette[0] == 0 && s.palette[1] == 0xF800 && s.palette[1] == rgb(255, 0, 0));
  CHECK(s.palette[2] == rgb(0, 255, 0));
  const uint8_t px[8] = {0, 1, 1, 0, 2, 2, 0, 1};
  CHECK(std::memcmp(s.px, px, 8) == 0);
  CHECK(s.palette == reinterpret_cast<const Color*>(kSprites + 24) && s.px == kSprites + 30);  // zero copy

  Blob t(kSprites, 38);
  t.d[4] = 1;  // Tileset
  Tileset ts;
  CHECK(assetTileset(t.d, t.n, ts) == Ok);
  CHECK(ts.tile_w == 2 && ts.tile_h == 2 && ts.transparent && ts.palette[1] == 0xF800 && ts.px == t.d + 30);
  CHECK(parse(0, t.d, t.n) == Corrupt);
  CHECK(parse(2, t.d, t.n) == Corrupt);

  TilemapView m;
  CHECK(assetTilemap(kTilemap, sizeof(kTilemap), m) == Ok);
  const uint8_t tiles[6] = {0, 1, 2, 3, 0, 254};
  CHECK(m.w == 3 && m.h == 2 && std::memcmp(m.tiles, tiles, 6) == 0);
  return 0;
}

// Header-level rejections that apply to every kind.
static int testHeaderRejects() {
  const uint8_t* src[4] = {kSprites, kSprites, kTilemap, kFont};
  const uint32_t len[4] = {38, 38, 22, 30};
  for (int k = 0; k < 4; k++) {
    Blob b(src[k], len[k]);
    if (k == 1) b.d[4] = 1;
    CHECK(parse(k, b.d, b.n) == Ok);
    CHECK(parse(k, nullptr, b.n) == InvalidArg);
    for (uint32_t i = 0; i < 4; i++) {  // any magic byte wrong
      Blob m = b;
      m.d[i] ^= 0x20;
      CHECK(parse(k, m.d, m.n) == Corrupt);
    }
    Blob v = b;
    v.d[5] = 2;
    CHECK(parse(k, v.d, v.n) == Corrupt);
    Blob r = b;
    r.d[6] = 1;
    CHECK(parse(k, r.d, r.n) == Corrupt);
    r = b;
    r.d[7] = 1;
    CHECK(parse(k, r.d, r.n) == Corrupt);
    Blob big = b;  // size field larger than len allows
    big.setSize(big.n - 12 + 1);
    CHECK(parse(k, big.d, big.n) == Corrupt);
    for (uint32_t n = 0; n < 12; n++) CHECK(parse(k, b.d, n) == Corrupt);
    CHECK(parse(k, b.d, b.n - 1) == Corrupt);  // truncated copy
    Blob lng = b;  // payload size one byte too long, with the byte present
    lng.setSize(b.n - 12 + 1);
    lng.n = b.n + 1;
    CHECK(parse(k, lng.d, lng.n) == Corrupt);
    Blob sh = b;  // one byte too short, len kept
    sh.setSize(b.n - 12 - 1);
    CHECK(parse(k, sh.d, sh.n) == Corrupt);
    Blob tr = b;  // trailing bytes beyond the payload are fine
    tr.n = b.n + 1;
    CHECK(parse(k, tr.d, tr.n) == Ok);
    Blob wt = b;  // wrong type byte
    wt.d[4] = 3;
    CHECK(parse(k, wt.d, wt.n) == Corrupt);
  }
  return 0;
}

static int testSheetRejects() {
  for (int k = 0; k < 2; k++) {
    Blob b(kSprites, 38);
    if (k == 1) b.d[4] = 1;
    // Sub-header field offsets: w 12, h 14, frames 16, cols 18, ncolors 20, transparent 22, pad 23.
    Blob m = b;
    m.d[20] = 0;  // ncolors 0 (size no longer matches either)
    CHECK(parse(k, m.d, m.n) == Corrupt);
    m = b;
    m.d[20] = 1;  // ncolors 1 with palette bytes kept: size mismatch
    CHECK(parse(k, m.d, m.n) == Corrupt);
    m = b;
    m.d[20] = 1;  // 257 colors
    m.d[21] = 1;
    CHECK(parse(k, m.d, m.n) == Corrupt);
    m = b;
    m.d[37] = 3;  // pixel index == ncolors
    CHECK(parse(k, m.d, m.n) == Corrupt);
    m = b;
    m.d[37] = 2;  // largest valid index
    CHECK(parse(k, m.d, m.n) == Ok);
    m = b;
    m.d[22] = 2;  // transparent not 0/1
    CHECK(parse(k, m.d, m.n) == Corrupt);
    m = b;
    m.d[23] = 1;  // pad
    CHECK(parse(k, m.d, m.n) == Corrupt);
    m = b;
    m.d[12] = 0;  // w == 0
    CHECK(parse(k, m.d, m.n) == Corrupt);
    m = b;
    m.d[14] = 0;  // h == 0
    CHECK(parse(k, m.d, m.n) == Corrupt);
    m = b;
    m.d[16] = 0;  // frames == 0
    CHECK(parse(k, m.d, m.n) == Corrupt);
    m = b;
    m.d[13] = 0xFF;  // huge w: 64-bit size math must not wrap into a match
    m.d[15] = 0xFF;
    m.d[17] = 0xFF;
    CHECK(parse(k, m.d, m.n) == Corrupt);
    Blob lt(kSprites, 14);  // payload shorter than the sub-header
    lt.setSize(2);
    CHECK(parse(k, lt.d, lt.n) == Corrupt);
  }
  // Odd-aligned sprite blob: valid bytes, palette would be misaligned.
  alignas(4) uint8_t raw[64] = {};
  std::memcpy(raw + 1, kSprites, 38);
  CHECK(parse(0, raw + 1, 38) == InvalidArg);
  CHECK(parse(0, raw, 38) == Corrupt);
  return 0;
}

static int testAtlas() {
  // cols=2 frames=3 w=1 h=1 ncolors=2: pixel bytes (2*1)*(ceil(3/2)*1) = 4.
  const uint8_t atlas[32] = {0x50, 0x47, 0x45, 0x41, 0x00, 0x01, 0x00, 0x00, 0x14, 0x00, 0x00, 0x00,
                             0x01, 0x00, 0x01, 0x00, 0x03, 0x00, 0x02, 0x00, 0x02, 0x00, 0x00, 0x00,
                             0x00, 0x00, 0xFF, 0xFF, 0x00, 0x01, 0x01, 0x00};
  Blob b(atlas, 32);
  SpriteSheet s;
  CHECK(assetSprites(b.d, b.n, s) == Ok);
  CHECK(s.cols == 2 && s.frames == 3 && s.w == 1 && s.h == 1 && !s.transparent);
  CHECK(s.px[0] == 0 && s.px[1] == 1 && s.px[2] == 1 && s.px[3] == 0);
  Blob w = b;  // three pixel bytes (the stacked count) is wrong for an atlas
  w.setSize(19);
  w.n = 31;
  CHECK(assetSprites(w.d, w.n, s) == Corrupt);
  w = b;  // five pixel bytes
  w.setSize(21);
  w.n = 33;
  CHECK(assetSprites(w.d, w.n, s) == Corrupt);
  // Tilesets cannot be atlases.
  w = b;
  w.d[4] = 1;
  Tileset ts;
  CHECK(assetTileset(w.d, w.n, ts) == Corrupt);
  return 0;
}

// Draws s at (0, 0) on a 12x2 canvas and writes the 24 '#'/'.' pixels (row 0, then row 1) to out.
static void artOf(const Font& f, const char* s, char* out) {
  Color buf[12 * 2] = {};
  Canvas c(buf, 12, 2, 0, 2, 0);
  c.text(f, 0, 0, s, 1);
  for (int i = 0; i < 24; i++) out[i] = buf[i] ? '#' : '.';
  out[24] = 0;
}

static Error parseFont(const Blob& b) { return parse(3, b.d, b.n); }

static int testFont() {
  Font f;
  CHECK(assetFont(kFont, sizeof(kFont), f) == Ok);
  CHECK(f.w == 3 && f.h == 2 && f.first == 'A' && f.count == 2 && f.spacing == 1 && f.nranges == 1);
  CHECK(f.rows == kFont + 24 && f.ranges == reinterpret_cast<const FontRange*>(kFont + 18));  // zero copy
  CHECK(f.ranges[0].first == 0x416 && f.ranges[0].count == 1 && f.ranges[0].glyph == 2);
  CHECK(textWidth(f, "AB\xD0\x96") == 11);
  char art[25];
  artOf(f, "AB\xD0\x96", art);  // A, B from the base block, U+0416 from the range
  CHECK(std::strcmp(art, "#.#..#..###." "###.#.#...#.") == 0);

  Font g;  // two ranges: the second one is reached after the first does not match
  CHECK(assetFont(kFont2, sizeof(kFont2), g) == Ok);
  CHECK(g.nranges == 2 && g.ranges[1].first == 0x430 && g.ranges[1].count == 2 && g.ranges[1].glyph == 3);
  artOf(g, "\xD0\xB1", art);
  CHECK(std::strcmp(art, "##.........." ".#..........") == 0);

  const Blob b(kFont, 30);
  Blob m = b;
  m.d[12] = 0;  // w == 0
  CHECK(parseFont(m) == Corrupt);
  m = b;
  m.d[12] = 9;
  CHECK(parseFont(m) == Corrupt);
  m = b;
  m.d[12] = 8;
  CHECK(parseFont(m) == Ok);
  m = b;
  m.d[13] = 0;  // h == 0
  CHECK(parseFont(m) == Corrupt);
  m = b;
  m.d[13] = 4;  // 6 glyph bytes do not divide into 4-byte glyphs
  CHECK(parseFont(m) == Corrupt);
  m = b;
  m.d[13] = 3;  // 2 glyphs of 3 bytes: the range's glyph 2 is out of bounds
  CHECK(parseFont(m) == Corrupt);
  m = b;
  m.d[13] = 1;  // 6 glyphs of 1 byte
  CHECK(parseFont(m) == Ok);
  m = b;
  m.d[17] = 0;  // no ranges: the table bytes become glyph bytes (6 glyphs)
  CHECK(parseFont(m) == Ok);
  m = b;
  m.d[17] = 4;  // table longer than the payload
  CHECK(parseFont(m) == Corrupt);
  m = b;
  m.d[17] = 255;
  CHECK(parseFont(m) == Corrupt);
  m = b;
  m.d[15] = 3;  // base block as large as the glyph count
  CHECK(parseFont(m) == Ok);
  m = b;
  m.d[15] = 4;  // more base codepoints than glyphs
  CHECK(parseFont(m) == Corrupt);
  m = b;
  m.d[15] = 0;  // empty base block: a range may start anywhere
  CHECK(parseFont(m) == Ok);
  m.d[18] = 0x41;
  m.d[19] = 0;
  CHECK(parseFont(m) == Ok);
  m = b;
  m.d[20] = 0;  // range count 0
  CHECK(parseFont(m) == Corrupt);
  m = b;
  m.d[22] = 3;  // glyph + count > glyphs
  CHECK(parseFont(m) == Corrupt);
  m = b;
  m.d[22] = 1;
  CHECK(parseFont(m) == Ok);
  m = b;
  m.d[20] = 2;  // glyph 2 + 2 > 3
  CHECK(parseFont(m) == Corrupt);
  m = b;
  m.d[18] = 0xFF;  // U+FFFF, count 1: the last codepoint
  m.d[19] = 0xFF;
  CHECK(parseFont(m) == Ok);
  m.d[20] = 2;  // runs past U+FFFF (glyph 0 keeps the glyph bound satisfied)
  m.d[22] = 0;
  CHECK(parseFont(m) == Corrupt);
  const uint8_t base_overlap[][3] = {  // range first lo, hi, count (base block is 0x41..0x42)
      {0x41, 0, 1}, {0x42, 0, 1}, {0x40, 0, 2}, {0x43, 0, 1}, {0x40, 0, 1}, {0x41, 0, 2}};
  const bool base_ok[] = {false, false, false, true, true, false};
  for (int i = 0; i < 6; i++) {
    m = b;
    m.d[18] = base_overlap[i][0];
    m.d[19] = base_overlap[i][1];
    m.d[20] = base_overlap[i][2];
    m.d[22] = 0;
    CHECK(parseFont(m) == (base_ok[i] ? Ok : Corrupt));
  }

  const Blob b2(kFont2, 40);  // range 0 is 0x416..0x416, range 1 is 0x430..0x431
  CHECK(parseFont(b2) == Ok);
  m = b2;
  for (int i = 0; i < 6; i++) std::swap(m.d[18 + i], m.d[24 + i]);  // not sorted
  CHECK(parseFont(m) == Corrupt);
  m = b2;
  m.d[24] = 0x16;  // second range starts where the first does
  m.d[25] = 0x04;
  CHECK(parseFont(m) == Corrupt);
  m.d[24] = 0x17;  // right behind the first range: touching is allowed
  CHECK(parseFont(m) == Ok);
  m = b2;
  m.d[24] = 0x15;  // [0x415, 0x417) overlaps the first range
  CHECK(parseFont(m) == Corrupt);

  Blob lt(kFont, 17);  // payload shorter than the 6-byte sub-header
  lt.setSize(5);
  CHECK(parseFont(lt) == Corrupt);
  alignas(4) uint8_t raw[64] = {};  // odd-aligned blob: the range table would be misaligned
  std::memcpy(raw + 1, kFont, 30);
  CHECK(parse(3, raw + 1, 30) == InvalidArg);
  CHECK(parse(3, raw, 30) == Corrupt);

  Font s;  // a rejected blob leaves out untouched, also when the violation is found last
  s.rows = reinterpret_cast<const uint8_t*>(1);
  s.w = 77;
  s.nranges = 9;
  m = b;
  m.d[22] = 3;
  CHECK(assetFont(m.d, m.n, s) == Corrupt);
  CHECK(s.rows == reinterpret_cast<const uint8_t*>(1) && s.w == 77 && s.nranges == 9);
  return 0;
}

static Error parseStrings(const Blob& b) {
  StringTable t;
  return assetStrings(b.d, b.n, t);
}

static bool is(const char* a, const char* b) { return std::strcmp(a, b) == 0; }

static int testStrings() {
  StringTable t;
  CHECK(assetStrings(kStrings, sizeof(kStrings), t) == Ok);
  CHECK(t.nlang == 2 && t.count == 2 && t.pool_size == 11);
  CHECK(t.codes == kStrings + 16 && t.offs == kStrings + 20 && t.pool == reinterpret_cast<const char*>(kStrings + 36));
  CHECK(is(t.get(0, 0), "Hi") && is(t.get(0, 1), "Yo"));
  CHECK(is(t.get(1, 0), "\xD0\x94\xD0\xB0"));
  uint8_t cur = 0;  // the current language: switched at run time, the same ids give the other texts
  CHECK(is(t.get(cur, 0), "Hi"));
  cur = 1;
  CHECK(is(t.get(cur, 0), "\xD0\x94\xD0\xB0"));
  cur = 0;
  CHECK(is(t.get(cur, 0), "Hi"));
  CHECK(is(t.get(1, 1), "Yo"));  // missing in "ru": the first language
  CHECK(is(t.get(0, 2), "") && is(t.get(2, 0), "") && is(t.get(255, 65535), ""));  // bad id / language: "", not nullptr
  const StringTable none;
  CHECK(none.get(0, 0) != nullptr && is(none.get(0, 0), "") && none.langIndex("en") == -1);
  CHECK(t.langIndex("en") == 0 && t.langIndex("ru") == 1 && t.langIndex("de") == -1);
  CHECK(t.langIndex(nullptr) == -1 && t.langIndex("e") == -1 && t.langIndex("") == -1 && t.langIndex("eng") == -1);
  CHECK(t.langIndex("EN") == -1 && t.langIndex("ex") == -1 && t.langIndex("xn") == -1);

  alignas(8) uint8_t abuf[512];  // a settings file picks the language: "lang = ru"
  Arena arena;
  arena.init(abuf, sizeof(abuf));
  Settings st;
  CHECK(st.init(arena, 4) == Ok);
  st.parse("lang = ru\n", 10);
  CHECK(t.langIndex(st.getStr("lang", "en")) == 1);
  CHECK(t.langIndex(st.getStr("other", "en")) == 0);

  const Blob b(kStrings, 47);
  CHECK(parseStrings(b) == Ok);
  Blob m = b;
  m.d[4] = 6;  // type Tilemap
  CHECK(parseStrings(m) == Corrupt);
  CHECK(assetStrings(nullptr, 47, t) == InvalidArg);
  m = b;
  m.d[12] = 0;  // no language
  CHECK(parseStrings(m) == Corrupt);
  m.d[12] = 17;  // more than 16
  CHECK(parseStrings(m) == Corrupt);
  m.d[12] = 1;  // one language: the offsets would start in the second code and run into the pool
  CHECK(parseStrings(m) == Corrupt);
  m = b;
  m.d[14] = 0;  // no strings
  CHECK(parseStrings(m) == Corrupt);
  m = b;
  m.d[14] = 255;  // table larger than the payload
  m.d[15] = 255;
  CHECK(parseStrings(m) == Corrupt);
  m = b;
  m.d[13] = 1;  // reserved
  CHECK(parseStrings(m) == Corrupt);
  const uint8_t codes[][2] = {{16, 'E'}, {16, 0x60}, {16, 0x7B}, {19, '0'}, {17, 0}};  // not 'a'..'z'
  for (const auto& c : codes) {
    m = b;
    m.d[c[0]] = c[1];
    CHECK(parseStrings(m) == Corrupt);
  }
  m = b;
  m.d[16] = 'a';  // the limits of the range are fine
  m.d[17] = 'z';
  CHECK(parseStrings(m) == Ok);
  for (int i = 20; i < 28; i += 4) {  // language 0 must have no missing entry (id 0, then id 1)
    m = b;
    for (int k = 0; k < 4; k++) m.d[i + k] = 0xFF;
    CHECK(parseStrings(m) == Corrupt);
  }
  m = b;
  m.d[28] = 11;  // offset == pool size
  CHECK(parseStrings(m) == Corrupt);
  m.d[28] = 10;  // the last byte: an empty string
  CHECK(parseStrings(m) == Ok);
  CHECK(assetStrings(m.d, m.n, t) == Ok && is(t.get(1, 0), ""));
  m = b;
  m.d[31] = 0x80;  // a huge offset
  CHECK(parseStrings(m) == Corrupt);
  m = b;
  m.d[46] = 1;  // the pool does not end with NUL
  CHECK(parseStrings(m) == Corrupt);
  m = b;
  m.setSize(0);  // no payload at all
  m.n = 12;
  CHECK(parseStrings(m) == Corrupt);
  m.setSize(4);  // header only: no pool byte
  m.n = 16;
  CHECK(parseStrings(m) == Corrupt);
  m = b;
  m.setSize(20);  // the offset table ends inside its bytes
  m.n = 32;
  CHECK(parseStrings(m) == Corrupt);
  m = b;
  m.setSize(24);  // all tables and no pool
  m.n = 36;
  CHECK(parseStrings(m) == Corrupt);
  m = b;
  m.n = 46;  // truncated copy: the payload is cut
  CHECK(parseStrings(m) == Corrupt);
  m = b;
  m.n = 48;  // trailing bytes beyond the payload are fine
  CHECK(parseStrings(m) == Ok);
  alignas(4) uint8_t raw[64] = {};  // odd address: byte reads only, no alignment needed
  std::memcpy(raw + 1, kStrings, 47);
  CHECK(assetStrings(raw + 1, 47, t) == Ok && is(t.get(0, 1), "Yo"));

  StringTable s;  // a rejected blob leaves out untouched, also when the violation is found last
  s.pool = reinterpret_cast<const char*>(1);
  s.nlang = 77;
  s.count = 9;
  m = b;
  m.d[46] = 1;
  CHECK(assetStrings(m.d, m.n, s) == Corrupt);
  m = b;
  m.d[28] = 11;
  CHECK(assetStrings(m.d, m.n, s) == Corrupt);
  CHECK(s.pool == reinterpret_cast<const char*>(1) && s.nlang == 77 && s.count == 9 && s.codes == nullptr);
  return 0;
}

static int testTilemapRejects() {
  Blob b(kTilemap, 22);
  Blob m = b;
  m.d[12] = 4;  // w*h = 8 but 6 tiles present
  CHECK(parse(2, m.d, m.n) == Corrupt);
  m = b;
  m.d[12] = 0;  // w == 0
  CHECK(parse(2, m.d, m.n) == Corrupt);
  m = b;
  m.d[14] = 0;  // h == 0
  CHECK(parse(2, m.d, m.n) == Corrupt);
  Blob lt(kTilemap, 14);  // payload shorter than w,h
  lt.setSize(2);
  CHECK(parse(2, lt.d, lt.n) == Corrupt);
  // Tilemaps have no alignment requirement.
  alignas(4) uint8_t raw[32] = {};
  std::memcpy(raw + 1, kTilemap, 22);
  CHECK(parse(2, raw + 1, 22) == Ok);
  return 0;
}

static int testGenerated() {
#ifdef PGE_HAVE_GEN
  CHECK(kSampleSprites_size == sizeof(kSprites));
  CHECK(std::memcmp(kSampleSprites, kSprites, sizeof(kSprites)) == 0);
  CHECK(kSampleTilemap_size == sizeof(kTilemap));
  CHECK(std::memcmp(kSampleTilemap, kTilemap, sizeof(kTilemap)) == 0);
  SpriteSheet s;
  CHECK(assetSprites(kSampleSprites, (uint32_t)kSampleSprites_size, s) == Ok);
  CHECK(s.w == 2 && s.h == 2 && s.frames == 2);
  TilemapView m;
  CHECK(assetTilemap(kSampleTilemap, (uint32_t)kSampleTilemap_size, m) == Ok);
  CHECK(m.w == 3 && m.h == 2);
  CHECK(kSampleFont_size == sizeof(kFont));
  CHECK(std::memcmp(kSampleFont, kFont, sizeof(kFont)) == 0);
  Font f;
  CHECK(assetFont(kSampleFont, (uint32_t)kSampleFont_size, f) == Ok);
  CHECK(f.w == 3 && f.h == 2 && f.first == 'A' && f.count == 2 && f.nranges == 1);
  char art[25];
  artOf(f, "AB\xD0\x96", art);
  CHECK(std::strcmp(art, "#.#..#..###." "###.#.#...#.") == 0);
  CHECK(kSampleStrings_size == sizeof(kStrings));
  CHECK(std::memcmp(kSampleStrings, kStrings, sizeof(kStrings)) == 0);
  StringTable t;
  CHECK(assetStrings(kSampleStrings, (uint32_t)kSampleStrings_size, t) == Ok);
  CHECK(t.nlang == 2 && t.count == 2 && t.langIndex("ru") == 1 && is(t.get(1, 1), "Yo"));
#else
  std::printf("test_asset: PGE_HAVE_GEN not defined, cross-language (Python tool) part skipped\n");
#endif
  return 0;
}

int main() {
  if (testGolden()) return 1;
  if (testHeaderRejects()) return 1;
  if (testSheetRejects()) return 1;
  if (testAtlas()) return 1;
  if (testTilemapRejects()) return 1;
  if (testFont()) return 1;
  if (testStrings()) return 1;
  if (testGenerated()) return 1;
  std::printf("test_asset: all passed\n");
  return 0;
}
