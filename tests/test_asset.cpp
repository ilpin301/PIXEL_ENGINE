// Asset blob tests (S5.4): golden sprites / tileset / tilemap blobs, rejection of every malformed variant, and (when
// the Python tool is available) byte-identity of the generated blobs with the goldens.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "pge/asset.h"
#include "pge/color.h"

#ifdef PGE_HAVE_GEN
#include "sample_sprites.h"
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

// kind 0: sprites, 1: tileset, 2: tilemap.
static Error parse(int kind, const void* p, uint32_t n) {
  SpriteSheet s;
  Tileset t;
  TilemapView m;
  if (kind == 0) return assetSprites(p, n, s);
  if (kind == 1) return assetTileset(p, n, t);
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
  const uint8_t* src[3] = {kSprites, kSprites, kTilemap};
  const uint32_t len[3] = {38, 38, 22};
  for (int k = 0; k < 3; k++) {
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
  if (testGenerated()) return 1;
  std::printf("test_asset: all passed\n");
  return 0;
}
