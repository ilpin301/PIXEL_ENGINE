#include "pge/worldgen.h"

namespace pge {

namespace {

enum : uint32_t { kSaltH64 = 1, kSaltH16 = 2, kSaltH4 = 3, kSaltDirt = 4, kSaltCave32 = 5, kSaltCave16 = 6, kSaltOre = 7,
             kSaltRoundDirt = 8, kSaltRoundOre = 9, kSaltRoundZone = 10 };

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

// Floor square root, bit by bit.
int isqrt(int n) {
  uint32_t x = (uint32_t)n, r = 0, bit = 1u << 30;
  while (bit > x) bit >>= 2;
  while (bit) {
    if (x >= r + bit) { x -= r + bit; r = (r >> 1) + bit; }
    else r >>= 1;
    bit >>= 2;
  }
  return (int)r;
}

// Diamond pseudo-angle of (dx, dy): 0..255, 64 per quadrant, rises with the true angle, continuous, wraps 255 -> 0.
int pseudoAngle(int dx, int dy) {
  const int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy, s = ax + ay;
  if (s == 0) return 0;
  if (dy >= 0) return dx >= 0 ? ay * 64 / s : 64 + ax * 64 / s;
  return dx < 0 ? 128 + ay * 64 / s : 192 + ax * 64 / s;
}

// Like noise2 over (pseudo-angle 0..255, depth >= 0): lattice 32 along the angle, wrapping mod 8 (no seam), 8 along depth.
int noiseRing(int ang, int depth, uint32_t seed, uint32_t salt) {
  const int fa = ang & 31, fd = depth & 7;
  const int ia = ang >> 5, ib = (ia + 1) & 7, id = depth >> 3;
  const int sa = smooth((fa << 8) >> 5), sd = smooth((fd << 8) >> 3);
  const int a = (int)(hash(seed, salt, ia, id) & 255), b = (int)(hash(seed, salt, ib, id) & 255);
  const int c = (int)(hash(seed, salt, ia, id + 1) & 255), d = (int)(hash(seed, salt, ib, id + 1) & 255);
  const int top = a * (256 - sa) + b * sa, bot = c * (256 - sa) + d * sa;
  return (top * (256 - sd) + bot * sd) >> 16;
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

void roundTile(const RoundSpec& s, int tx, int ty, uint32_t seed, uint8_t* fg, uint8_t* wall) {
  const int g = s.chunks * kChunkSize, r2 = 2 * s.radius;
  *fg = 0;
  *wall = 0;
  if (tx < 0 || ty < 0 || tx >= g || ty >= g) return;  // outside the grid is air even if the radius is too large
  const int dx = 2 * tx + 1 - g, dy = 2 * ty + 1 - g;
  if (dx > r2 || dx < -r2 || dy > r2 || dy < -r2) return;  // also keeps d2 far from overflow
  const int d2 = dx * dx + dy * dy;
  if (d2 > r2 * r2) return;
  const long long c2 = 2LL * s.core;  // the core disc in doubled units, 64-bit: a huge core must not overflow
  if (s.core > 0 && d2 <= c2 * c2) { *fg = kGenCore; *wall = kGenWallStone; return; }
  const int q = r2 - isqrt(d2);  // doubled units from the tile centre to the rim
  if (q <= 2) { *fg = kGenGrass; return; }  // within one tile of the rim: every tile touching air is grass
  const int depth = q / 2;  // tiles below the rim, rounded down (ore, deep ore and wall layers use this)
  *wall = depth < 8 ? kGenWallDirt : kGenWallStone;
  const int ang = pseudoAngle(dx, dy);
  if (q <= 4 + 2 * (int)(hash(seed, kSaltRoundDirt, ang >> 2, 0) & 1)) { *fg = kGenDirt; return; }  // 1-2 tiles under the grass
  *fg = kGenStone;
  const uint32_t r = hash(seed, kSaltRoundOre, tx, ty);
  const bool deep = depth >= 2 * s.radius / 3 && ((r >> 8) & 15) == 0;
  const bool ore = depth >= s.radius / 6 && (r & 7) == 0;
  if ((deep || ore) && noiseRing(ang, depth, seed, kSaltRoundZone) >= 150) *fg = deep ? kGenDeepOre : kGenOre;
}

void roundGen(const RoundSpec& s, uint8_t* fg, uint8_t* wall, int cx, int cy, uint32_t seed) {
  for (int ly = 0; ly < kChunkSize; ly++)
    for (int lx = 0; lx < kChunkSize; lx++)
      roundTile(s, cx * kChunkSize + lx, cy * kChunkSize + ly, seed, &fg[ly * kChunkSize + lx], &wall[ly * kChunkSize + lx]);
}

void planetGen(uint8_t* fg, uint8_t* wall, int cx, int cy, uint32_t seed) { roundGen(kPlanetSpec, fg, wall, cx, cy, seed); }
void asteroid1Gen(uint8_t* fg, uint8_t* wall, int cx, int cy, uint32_t seed) { roundGen(kAsteroid1Spec, fg, wall, cx, cy, seed); }
void asteroid2Gen(uint8_t* fg, uint8_t* wall, int cx, int cy, uint32_t seed) { roundGen(kAsteroid2Spec, fg, wall, cx, cy, seed); }

GravityDir roundGravity(int x, int y, int cx, int cy) {
  const long long dx = (long long)x - cx, dy = (long long)y - cy;  // 64-bit: INT_MIN / INT_MAX inputs cannot overflow
  if ((dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy)) return dx > 0 ? GravityDir::Left : GravityDir::Right;
  return dy > 0 ? GravityDir::Up : GravityDir::Down;
}

GravityDir roundTileGravity(const RoundSpec& s, int tx, int ty) {
  const int g = s.chunks * kChunkSize;
  return roundGravity(2 * tx + 1, 2 * ty + 1, g, g);
}

}  // namespace pge
