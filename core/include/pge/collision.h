#pragma once
#include <stdint.h>

namespace pge {

// Tile property flags (docs/architecture/03 section 2.4).
enum TileFlag : uint16_t {
  kTileSolid = 0x01,   // blocks movement from every direction
  kTileOneWay = 0x02,  // blocks only a body falling onto its top edge
  kTileHazard = 0x04,  // never blocks; only reported in MoveResult::touched_flags
  kTileClimb = 0x08,        // game meaning (ladders); collision ignores it
  kTileActivatable = 0x10,  // World::activate calls on_activate
  kTileBreakable = 0x20,    // game meaning
  kTileTransparent = 0x40,  // game / lighting meaning
};

// Per-tile-id properties. hardness and light are carried for game code; collision only reads flags.
struct TileProps {
  uint16_t flags;    // TileFlag bits
  uint8_t hardness;  // not used by collision
  uint8_t light;     // not used by collision
};

// Read-only tile map for collision. tiles is row-major w*h tile ids; props is a table of 256 entries indexed by
// tile id (so ids need no range check). tile_w and tile_h must be >= 1 (any value, not only powers of two).
struct TileGrid {
  const uint8_t* tiles;
  uint16_t w, h;  // size in tiles
  uint8_t tile_w, tile_h;  // tile size in pixels
  const TileProps* props;
};

// Axis-aligned box: top-left pixel position and size in world pixels. Plain ints (like Camera); the 03 spec uses
// fixed point, which this module does not.
struct Body {
  int x, y;
  int w, h;
};

// Direction gravity pulls in (y down). Down = 0 is the default everywhere; each next value turns 90 degrees
// clockwise on screen.
enum class GravityDir : uint8_t { Down = 0, Left = 1, Up = 2, Right = 3 };

// Outcome of moveAndCollide.
struct MoveResult {
  bool hit_x;               // the X move was stopped by a blocking tile
  bool hit_y;               // the Y move was stopped by a blocking tile
  bool on_ground;           // a 1 px move along gravity would be blocked (solid, or one-way met from far side, below)
  uint16_t touched_flags;   // OR of the flags of all tiles overlapped by the final box
};

// Flags of tile (tx, ty). Any tile outside the grid reports kTileSolid: the world boundary is solid in every
// direction.
uint16_t tileFlags(const TileGrid& g, int tx, int ty);

// Moves b by (dx, dy): X axis first, then Y. Each axis advances in sub-steps of at most one tile so nothing
// tunnels, whatever the move or body size. The first blocked sub-step snaps the box flush against the blocking
// tile edge, sets hit_x / hit_y and ends movement on that axis. Moving horizontally or up, only kTileSolid blocks.
// Moving down, kTileSolid blocks, and a kTileOneWay tile blocks only if the box bottom before the sub-step was at
// or above the tile's top edge. kTileHazard never blocks.
// Precondition: b does not start overlapping a blocking tile. If it does, the call still terminates without
// out-of-bounds reads, but the snap may push the box back.
// With g != Down the sweep runs in a frame where g points down: the axis across gravity moves first, then the gravity
// axis; one-way tiles block only a body moving along g onto their face that faces against g; on_ground probes 1 px
// along g. hit_x / hit_y always name world axes. g == Down is exactly the behaviour described above.
MoveResult moveAndCollide(const TileGrid& g, Body& b, int dx, int dy, GravityDir gd = GravityDir::Down);

}  // namespace pge
