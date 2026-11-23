#pragma once
// Chunked tile world (docs/architecture/03 section 2, K8): 32x32-tile chunks, two layers (fg, wall), 1 B per tile.
// A chunk is generator output plus the logged edits for it; dropping a chunk from the cache never needs a write.
#include <stdint.h>
#include "pge/collision.h"
#include "pge/config.h"
#include "pge/error.h"
#include "pge/gfx.h"
#include "pge/memory.h"

namespace pge {

constexpr int kChunkSize = 32;
constexpr int kChunkTiles = kChunkSize * kChunkSize;
constexpr uint8_t kTileAir = 0;
constexpr uint8_t kTileVoid = 255;  // outside the world or chunk not loaded; always treated as solid by collision

enum class Layer : uint8_t { Fg = 0, Wall = 1 };

class World;
// Fills fg and wall (kChunkTiles bytes each, row-major) for chunk (cx, cy). Must be a pure function of its arguments.
typedef void (*GenFn)(uint8_t* fg, uint8_t* wall, int cx, int cy, uint32_t seed);
typedef void (*ActivateFn)(World& w, int tx, int ty, uint8_t tile, void* user);

struct WorldConfig {
  uint16_t w_chunks, h_chunks;  // world size in chunks; w_chunks * h_chunks <= 65535
  uint32_t seed;
  GenFn gen;                    // required
  ActivateFn on_activate;       // optional
  void* user;
  const TileProps* props;       // required, 256 entries
  uint16_t cache_slots;         // 0 = PGE_CHUNK_CACHE_COUNT
  uint32_t change_capacity;     // 0 = 4096 entries (5 B each)
  int tile_px;                  // tile size in pixels, >= 1 (0 = 16)
  Arena* log_arena;             // change log arena; nullptr = the arena passed to World::init (e.g. external RAM, S8.2)
};

class World {
 public:
  // Allocates chunk cache and slot metadata from the arena, the change log from cfg.log_arena (or the arena). InvalidArg
  // on null gen/props, zero size, more than 65535 chunks or tile_px < 0; OutOfMemory if an arena runs out.
  Error init(const WorldConfig& cfg, Arena& arena);

  // kTileVoid if out of bounds or the chunk is not loaded.
  uint8_t get(int tx, int ty, Layer l) const;
  // InvalidArg if out of bounds or t == kTileVoid. Edits in unloaded chunks are only logged and applied on the next
  // load. A repeated set of one cell overwrites its entry; a new entry in a full log gives OutOfMemory.
  Error set(int tx, int ty, Layer l, uint8_t t);
  bool inBounds(int tx, int ty) const;

  uint32_t changeCount() const { return count_; }
  uint32_t changeCapacity() const { return cap_; }
  bool changeLogNearFull() const { return count_ * 10 >= cap_ * 9; }

  // Loads the chunks overlapping view_px (pixels, clamped to the world), at most max_loads generations per call.
  // Chunks the view needs are never evicted by the same call. Returns how many needed chunks are still missing.
  int ensureLoaded(Rect view_px, int max_loads = PGE_MAX_CHUNK_LOADS_PER_TICK);

  // Calls on_activate if (tx, ty) is in bounds, its fg tile has kTileActivatable and a callback is set.
  bool activate(int tx, int ty);

  const TileProps& props(uint8_t t) const { return props_[t]; }
  uint32_t seed() const { return seed_; }
  uint16_t chunksLoaded() const { return loaded_; }
  bool isLoaded(int cx, int cy) const;
  int tilePx() const { return tile_px_; }
  // Flags of the fg tile; out of bounds, unloaded and void tiles report kTileSolid.
  uint32_t tileFlags(int tx, int ty) const;

  uint16_t chunksW() const { return w_chunks_; }
  uint16_t chunksH() const { return h_chunks_; }
  // Sorted change log access, i < changeCount(). Key = (chunk << 16) | (layer << 10) | (ty_local << 5) | tx_local.
  uint32_t changeKeyAt(uint32_t i) const { return key_[i]; }
  uint8_t changeTileAt(uint32_t i) const { return tile_[i]; }
  // Empties the log and frees every cache slot so no chunk keeps stale edits. The caller must call ensureLoaded again.
  void clearChanges();

 private:
  int findSlot(int chunk) const;
  int logFind(uint32_t key) const;  // index of first entry >= key
  void load(int slot, int cx, int cy, int chunk);

  uint16_t w_chunks_ = 0, h_chunks_ = 0, slots_ = 0, loaded_ = 0;
  mutable uint16_t last_hit_ = 0;
  int tile_px_ = 16;
  uint32_t seed_ = 0, tick_ = 0;
  GenFn gen_ = nullptr;
  ActivateFn on_activate_ = nullptr;
  void* user_ = nullptr;
  const TileProps* props_ = nullptr;
  uint8_t* data_ = nullptr;   // slots_ * 2 * kChunkTiles bytes: fg then wall per slot
  int32_t* slot_chunk_ = nullptr;  // chunk index per slot, -1 = free
  uint32_t* slot_stamp_ = nullptr;  // LRU stamp (tick of the last ensureLoaded that needed the slot)
  uint32_t* key_ = nullptr;   // sorted (chunk << 16) | (layer << 10) | (ty_local << 5) | tx_local
  uint8_t* tile_ = nullptr;   // new tile per key
  uint32_t count_ = 0, cap_ = 0;
};

// Placeholder generator: world row kFlatSurfaceRow is grass (1), rows below dirt (2), above air; wall layer is 3
// below the surface row, 0 above. Ignores seed.
constexpr int kFlatSurfaceRow = 16;
void flatGen(uint8_t* fg, uint8_t* wall, int cx, int cy, uint32_t seed);

// Same semantics as moveAndCollide(const TileGrid&, ...) with tile size = World::tilePx() (square). Unloaded chunks
// and the world boundary are solid.
MoveResult moveAndCollide(const World& w, Body& b, int dx, int dy);

}  // namespace pge
