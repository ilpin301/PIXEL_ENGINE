#include "pge/worldgen.h"

namespace pge {

namespace {

enum : uint32_t { kSaltH64 = 1, kSaltH16 = 2, kSaltH4 = 3, kSaltDirt = 4, kSaltCave32 = 5, kSaltCave16 = 6, kSaltOre = 7 };

uint32_t fmix(uint32_t h) {  // murmur3 finalizer
  h ^= h >> 16;
  h *= 0x85EBCA6Bu;
  h ^= h >> 13;
  h *= 0xC2B2AE35u;
  h ^= h >> 16;
  return h;
}

uint32_t hash(uint32_t seed, uint32_t salt, int a, int b) {
  uint32_t h = fmix(seed ^ (salt * 0x9E3779B1u));
  h = fmix(h ^ ((uint32_t)a * 0x85EBCA77u));
  return fmix(h ^ ((uint32_t)b * 0xC2B2AE3Du));
}

int smooth(int t) { return (t * t * (768 - 2 * t)) >> 16; }  // t 0..256 -> 0..256

// Value noise 0..255 with lattice spacing 1 << shift tiles. (x - f) is an exact multiple, so the division is exact
// for negative x as well.
int noise1(int x, int shift, uint32_t seed, uint32_t salt) {
  const int f = x & ((1 << shift) - 1);
  const int i = (x - f) / (1 << shift);
  const int s = smooth((f << 8) >> shift);
  const int a = (int)(hash(seed, salt, i, 0) & 255), b = (int)(hash(seed, salt, i + 1, 0) & 255);
  return (a * (256 - s) + b * s) >> 8;
}

int noise2(int x, int y, int shift, uint32_t seed, uint32_t salt) {
  const int m = (1 << shift) - 1;
  const int fx = x & m, fy = y & m;
  const int ix = (x - fx) / (1 << shift), iy = (y - fy) / (1 << shift);
  const int sx = smooth((fx << 8) >> shift), sy = smooth((fy << 8) >> shift);
  const int a = (int)(hash(seed, salt, ix, iy) & 255), b = (int)(hash(seed, salt, ix + 1, iy) & 255);
  const int c = (int)(hash(seed, salt, ix, iy + 1) & 255), d = (int)(hash(seed, salt, ix + 1, iy + 1) & 255);
  const int top = a * (256 - sx) + b * sx, bot = c * (256 - sx) + d * sx;
  return (top * (256 - sy) + bot * sy) >> 16;
}

int dirtDepth(int tx, uint32_t seed) { return 1 + (int)(hash(seed, kSaltDirt, tx, 0) & 1); }

// One tile given the per-column values h (surface row) and dirt (dirt rows below the grass).
void cell(int tx, int ty, uint32_t seed, int h, int dirt, uint8_t* fg, uint8_t* wall) {
  const int depth = ty - h;
  if (depth < 0) { *fg = 0; *wall = 0; return; }
  if (depth == 0) { *fg = kGenGrass; *wall = 0; return; }
  *wall = depth < 8 ? kGenWallDirt : kGenWallStone;
  if (depth <= dirt) { *fg = kGenDirt; return; }
  if (depth >= kGenCaveMinDepth) {
    const int n = (noise2(tx, ty, 5, seed, kSaltCave32) * 3 + noise2(tx, ty, 4, seed, kSaltCave16)) >> 2;
    if (n >= 128 - kGenCaveBand && n <= 128 + kGenCaveBand) { *fg = 0; return; }
  }
  *fg = kGenStone;
  const uint32_t r = hash(seed, kSaltOre, tx, ty);
  if (depth >= kGenDeepOreMinDepth && ((r >> 8) & 127) == 0) *fg = kGenDeepOre;
  else if (depth >= kGenOreMinDepth && (r & 63) == 0) *fg = kGenOre;
}

}  // namespace

int terrainHeight(int tx, uint32_t seed) {
  const int sum = noise1(tx, 6, seed, kSaltH64) * 14 + noise1(tx, 4, seed, kSaltH16) * 5 + noise1(tx, 2, seed, kSaltH4);
  return kGenSurfaceBase - 20 + (sum >> 7);  // sum max 255 * 20 = 5100 -> +39
}

void terrainTile(int tx, int ty, uint32_t seed, uint8_t* fg, uint8_t* wall) {
  cell(tx, ty, seed, terrainHeight(tx, seed), dirtDepth(tx, seed), fg, wall);
}

void terrainGen(uint8_t* fg, uint8_t* wall, int cx, int cy, uint32_t seed) {
  for (int lx = 0; lx < kChunkSize; lx++) {
    const int tx = cx * kChunkSize + lx;
    const int h = terrainHeight(tx, seed), dirt = dirtDepth(tx, seed);
    for (int ly = 0; ly < kChunkSize; ly++)
      cell(tx, cy * kChunkSize + ly, seed, h, dirt, &fg[ly * kChunkSize + lx], &wall[ly * kChunkSize + lx]);
  }
}

}  // namespace pge
