#pragma once
// Default procedural terrain generator (S5.1b, docs/architecture/03 section 2.2, K8).
// terrainGen is a GenFn: a pure function of (cx, cy, seed), integer math only, all noise sampled in world tile
// coordinates, so chunk seams match and the same inputs always give the same bytes.
// World layout: surface row in [kGenSurfaceBase - 20, kGenSurfaceBase + 19] = rows 20..59, so the world needs
// h_chunks >= 4 (128 rows) to leave stone, caves and ore below the highest hills. No bedrock: the world bottom is
// simply where h_chunks ends.
#include <stdint.h>
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

}  // namespace pge
