#pragma once
// Default procedural terrain generator (S5.1b, docs/architecture/03 section 2.2, K8).
// terrainGen is a GenFn: a pure function of (cx, cy, seed), integer math only, all noise sampled in world tile
// coordinates, so chunk seams match and the same inputs always give the same bytes.
// World layout: surface row in [kGenSurfaceBase - 20, kGenSurfaceBase + 19] = rows 20..59, so the world needs
// h_chunks >= 4 (128 rows) to leave stone, caves and ore below the highest hills. No bedrock: the world bottom is
// simply where h_chunks ends.
#include <stdint.h>
#include "pge/collision.h"
#include "pge/world.h"

namespace pge {

// Foreground tile ids written by terrainGen. flatGen (placeholder) uses 1 grass, 2 dirt and wall id 3.
enum GenTile : uint8_t { kGenGrass = 1, kGenDirt = 2, kGenStone = 3, kGenOre = 4, kGenDeepOre = 5 };
// Wall-layer ids written by terrainGen (0 = none above the surface).
enum GenWall : uint8_t { kGenWallDirt = 1, kGenWallStone = 2 };

constexpr int kGenSurfaceBase = 40;     // mean surface row
constexpr int kGenCaveMinDepth = 8;     // caves start this many rows below the surface
constexpr int kGenCaveBand = 6;         // cave where |noise - 128| <= band (noise 0..255)
constexpr int kGenOreMinDepth = 10;
constexpr int kGenDeepOreMinDepth = 40;

// Surface row (first non-air foreground tile, the grass row) of world column tx. Same value terrainGen uses, so a
// game can place the player on the surface without loading a chunk.
int terrainHeight(int tx, uint32_t seed);

// Reference per-tile form of terrainGen: tile (tx, ty) in world coordinates, written to *fg and *wall.
void terrainTile(int tx, int ty, uint32_t seed, uint8_t* fg, uint8_t* wall);

// GenFn: surface from 3 octaves of 1D value noise (wavelengths 64, 16, 4), 1-2 dirt rows plus grass on top, stone
// below, caves as a thin band of 2D value noise, ore as a per-tile hash. Fills fg and wall (kChunkTiles bytes each).
void terrainGen(uint8_t* fg, uint8_t* wall, int cx, int cy, uint32_t seed);

// ---- Round world (S8.5): a disc (planet / asteroid) in a square grid of chunks ----
// Pure integer function of (tile coords, seed), same hash/noise helpers and tile ids as terrainGen; no game names.
// A tile outside the grid or outside the disc (so every chunk outside the grid) comes out as air (0) in both layers.
// shortcut: perfectly round surface (no hills); upgrade if the example looks dull.
// shortcut: no caves; upgrade if the example looks dull.
// shortcut: innermost layer is stone/ore (no separate core id); upgrade if the example looks dull.
// shortcut: dirt thickness is patchy per 64 pseudo-angle buckets, independent hashes (intended)

// Grid is chunks x chunks chunks (chunks * 32 tiles per side); radius is the disc radius in tiles.
struct RoundSpec {
  int chunks;
  int radius;
};

constexpr RoundSpec kPlanetSpec = {4, 60};      // 128 x 128 tiles
constexpr RoundSpec kAsteroid1Spec = {1, 14};   // 32 x 32 tiles
constexpr RoundSpec kAsteroid2Spec = {2, 29};   // 64 x 64 tiles

// Reference per-tile form of roundGen: tile (tx, ty) in world coordinates, written to *fg and *wall. A tile outside the
// grid or outside the disc is air in both layers, even if the radius is too large for the grid.
// Layers by depth below the rim: grass, 1-2 dirt (patchy by direction), stone with ore zones by direction and depth.
void roundTile(const RoundSpec& s, int tx, int ty, uint32_t seed, uint8_t* fg, uint8_t* wall);

// Fills chunk (cx, cy) of the round world s (kChunkTiles bytes each for fg and wall, row-major); equals roundTile.
void roundGen(const RoundSpec& s, uint8_t* fg, uint8_t* wall, int cx, int cy, uint32_t seed);

// GenFn wrappers: roundGen with kPlanetSpec, kAsteroid1Spec and kAsteroid2Spec.
void planetGen(uint8_t* fg, uint8_t* wall, int cx, int cy, uint32_t seed);
void asteroid1Gen(uint8_t* fg, uint8_t* wall, int cx, int cy, uint32_t seed);
void asteroid2Gen(uint8_t* fg, uint8_t* wall, int cx, int cy, uint32_t seed);

// Gravity toward the centre (cx, cy) for a point (x, y) in any one unit: the larger axis offset wins, a tie (exact
// diagonal, centre included) goes to the vertical axis, so the centre itself gives Down. Right of the centre pulls
// Left, below it pulls Up.
GravityDir roundGravity(int x, int y, int cx, int cy);

// roundGravity for tile (tx, ty) of the grid of s, using tile centres in doubled units (2 * t + 1) against the
// grid centre (chunks * 32, chunks * 32).
GravityDir roundTileGravity(const RoundSpec& s, int tx, int ty);

}  // namespace pge
