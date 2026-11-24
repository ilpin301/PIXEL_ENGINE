# World, terrain, collision and entities API

> Summary for later sessions
> - Covers four headers: `pge/world.h`, `pge/worldgen.h`, `pge/collision.h`, `pge/entity.h`.
> - All identifiers live in namespace `pge`.
> - These modules are host-tested only and have not been run on a board
>   (per the STATUS.md tracker lines for world/entities).
> - `PGE_*` macros named below are not defined in these headers, so their values are not listed.

## pge/collision.h

Tile property flags and tile-sweep collision for axis-aligned boxes. Tile map, body and result types live here;
`World` and `Entities` build on them.

### Tile flags (`enum TileFlag : uint16_t`)

| Name | Value | Meaning |
|---|---|---|
| `kTileSolid` | 0x01 | blocks movement from every direction |
| `kTileOneWay` | 0x02 | blocks only a body falling onto its top edge |
| `kTileHazard` | 0x04 | never blocks; only reported in `MoveResult::touched_flags` |
| `kTileClimb` | 0x08 | game meaning (ladders); collision ignores it |
| `kTileActivatable` | 0x10 | `World::activate` calls `on_activate` |
| `kTileBreakable` | 0x20 | game meaning |
| `kTileTransparent` | 0x40 | game / lighting meaning |

### Types

| Type | Fields | Meaning |
|---|---|---|
| `TileProps` | `uint16_t flags; uint8_t hardness; uint8_t light;` | per-tile-id properties; `flags` are `TileFlag` bits; `hardness` and `light` are carried for game code, collision only reads `flags` |
| `TileGrid` | `const uint8_t* tiles; uint16_t w, h; uint8_t tile_w, tile_h; const TileProps* props;` | read-only tile map; `tiles` is row-major `w*h` tile ids; `w`, `h` in tiles; `tile_w`, `tile_h` in pixels, must be >= 1 (any value, not only powers of two); `props` is a table of 256 entries indexed by tile id |
| `Body` | `int x, y; int w, h;` | axis-aligned box: top-left pixel position and size in world pixels; plain ints |
| `GravityDir` | `enum class GravityDir : uint8_t` | `Down = 0, Left, Up, Right`; direction gravity pulls in; `Down` is the default |
| `MoveResult` | `bool hit_x; bool hit_y; bool on_ground; uint16_t touched_flags;` | outcome of `moveAndCollide` |

`MoveResult`: `hit_x` / `hit_y` = that axis move was stopped by a blocking tile; `on_ground` = a 1 px move along
gravity would be blocked (solid, or one-way met from its far side); `touched_flags` = OR of the flags of
all tiles overlapped by the final box.

### Functions

| Signature | Meaning |
|---|---|
| `uint16_t tileFlags(const TileGrid& g, int tx, int ty)` | flags of tile (tx, ty); any tile outside the grid reports `kTileSolid`, so the world boundary is solid in every direction |
| `MoveResult moveAndCollide(const TileGrid& g, Body& b, int dx, int dy, GravityDir gd = GravityDir::Down)` | moves `b` by (dx, dy), X axis first, then Y |

`moveAndCollide` behaviour as stated in the header:

- Each axis advances in sub-steps of at most one tile, so nothing tunnels, whatever the move or body size.
- The first blocked sub-step snaps the box flush against the blocking tile edge, sets `hit_x` / `hit_y` and ends
  movement on that axis.
- Moving horizontally or up, only `kTileSolid` blocks.
- Moving down, `kTileSolid` blocks, and a `kTileOneWay` tile blocks only if the box bottom before the sub-step was
  at or above the tile's top edge.
- `kTileHazard` never blocks.
- Precondition: `b` does not start overlapping a blocking tile. If it does, the call still terminates without
  out-of-bounds reads, but the snap may push the box back.
- `gd` != `Down`: the sweep runs in a frame where `gd` points down. The axis across gravity moves first, then the
  gravity axis; one-way tiles block only a body moving along `gd` onto their face that faces against `gd`;
  `on_ground` probes 1 px along `gd`; `hit_x` / `hit_y` always name world axes. `Down` is the behaviour above.

## pge/world.h

Chunked tile world: 32x32-tile chunks, two layers (fg, wall), 1 B per tile. A chunk is generator output plus the
logged edits for it; dropping a chunk from the cache never needs a write. Includes `pge/collision.h`,
`pge/config.h`, `pge/error.h`, `pge/gfx.h`, `pge/memory.h`.

### Constants and enums

| Name | Value / definition | Meaning |
|---|---|---|
| `kChunkSize` | 32 | chunk edge in tiles |
| `kChunkTiles` | `kChunkSize * kChunkSize` | tiles per chunk (bytes per layer) |
| `kTileAir` | 0 | |
| `kTileVoid` | 255 | outside the world or chunk not loaded; always treated as solid by collision |
| `kFlatSurfaceRow` | 16 | surface row of `flatGen` |
| `enum class Layer : uint8_t` | `Fg = 0, Wall = 1` | the two tile layers |

### Callback types

| Type | Signature | Meaning |
|---|---|---|
| `GenFn` | `void (*)(uint8_t* fg, uint8_t* wall, int cx, int cy, uint32_t seed)` | fills fg and wall (`kChunkTiles` bytes each, row-major) for chunk (cx, cy); must be a pure function of its arguments |
| `ActivateFn` | `void (*)(World& w, int tx, int ty, uint8_t tile, void* user)` | activation callback |

### `struct WorldConfig`

| Field | Meaning |
|---|---|
| `uint16_t w_chunks, h_chunks` | world size in chunks; `w_chunks * h_chunks <= 65535` |
| `uint32_t seed` | |
| `GenFn gen` | required |
| `ActivateFn on_activate` | optional |
| `void* user` | |
| `const TileProps* props` | required, 256 entries |
| `uint16_t cache_slots` | 0 = `PGE_CHUNK_CACHE_COUNT` |
| `uint32_t change_capacity` | 0 = 4096 entries (5 B each) |
| `int tile_px` | tile size in pixels, >= 1 (0 = 16) |
| `Arena* log_arena` | change log arena; `nullptr` = the arena passed to `init`. Lets a game put a big log in external RAM (the F429 planet check puts 163,840 B in SDRAM, S8.2) |

### `class World`

| Signature | Meaning |
|---|---|
| `Error init(const WorldConfig& cfg, Arena& arena)` | allocates chunk cache and slot metadata from the arena, the change log from `cfg.log_arena` (or the arena); `InvalidArg` on null gen/props, zero size, more than 65535 chunks or `tile_px < 0`; `OutOfMemory` if an arena runs out |
| `uint8_t get(int tx, int ty, Layer l) const` | tile at (tx, ty); `kTileVoid` if out of bounds or the chunk is not loaded |
| `Error set(int tx, int ty, Layer l, uint8_t t)` | `InvalidArg` if out of bounds or `t == kTileVoid`; edits in unloaded chunks are only logged and applied on the next load; a repeated set of one cell overwrites its entry; a new entry in a full log gives `OutOfMemory` |
| `bool inBounds(int tx, int ty) const` | |
| `uint32_t changeCount() const` | entries in the change log |
| `uint32_t changeCapacity() const` | |
| `bool changeLogNearFull() const` | `count * 10 >= capacity * 9` |
| `int ensureLoaded(Rect view_px, int max_loads = PGE_MAX_CHUNK_LOADS_PER_TICK)` | loads the chunks overlapping `view_px` (pixels, clamped to the world), at most `max_loads` generations per call; chunks the view needs are never evicted by the same call; returns how many needed chunks are still missing |
| `bool activate(int tx, int ty)` | calls `on_activate` if (tx, ty) is in bounds, its fg tile has `kTileActivatable` and a callback is set |
| `const TileProps& props(uint8_t t) const` | |
| `uint32_t seed() const` | |
| `uint16_t chunksLoaded() const` | |
| `bool isLoaded(int cx, int cy) const` | |
| `int tilePx() const` | |
| `uint32_t tileFlags(int tx, int ty) const` | flags of the fg tile; out of bounds, unloaded and void tiles report `kTileSolid` |
| `uint16_t chunksW() const` | |
| `uint16_t chunksH() const` | |
| `uint32_t changeKeyAt(uint32_t i) const` | sorted change log access, `i < changeCount()`; key = `(chunk << 16) \| (layer << 10) \| (ty_local << 5) \| tx_local` |
| `uint8_t changeTileAt(uint32_t i) const` | tile stored for entry `i` |
| `void clearChanges()` | empties the log and frees every cache slot so no chunk keeps stale edits; the caller must call `ensureLoaded` again |

### Free functions

| Signature | Meaning |
|---|---|
| `void flatGen(uint8_t* fg, uint8_t* wall, int cx, int cy, uint32_t seed)` | placeholder generator: world row `kFlatSurfaceRow` is grass (1), rows below dirt (2), above air; wall layer is 3 below the surface row, 0 above; ignores seed |
| `MoveResult moveAndCollide(const World& w, Body& b, int dx, int dy, GravityDir gd = GravityDir::Down)` | same semantics as `moveAndCollide(const TileGrid&, ...)` with tile size = `World::tilePx()` (square); unloaded chunks and the world boundary are solid |

## pge/worldgen.h

Default procedural terrain generator. `terrainGen` is a `GenFn`: a pure function of (cx, cy, seed), integer math
only, all noise sampled in world tile coordinates, so chunk seams match and the same inputs always give the same
bytes. Includes `pge/world.h`.

World layout stated in the header: surface row in `[kGenSurfaceBase - 20, kGenSurfaceBase + 19]` = rows 20..59, so
the world needs `h_chunks >= 4` (128 rows) to leave stone, caves and ore below the highest hills. No bedrock: the
world bottom is simply where `h_chunks` ends.

### Tile ids

| Name | Value | Meaning |
|---|---|---|
| `kGenGrass` | 1 | foreground tile written by `terrainGen` |
| `kGenDirt` | 2 | foreground |
| `kGenStone` | 3 | foreground |
| `kGenOre` | 4 | foreground |
| `kGenDeepOre` | 5 | foreground |
| `kGenCore` | 6 | foreground, written only by `roundTile` / `roundGen` (the core of a round world); a game should mark it solid and not breakable |
| `kGenWallDirt` | 1 | wall-layer id written by `terrainGen` (0 = none above the surface) |
| `kGenWallStone` | 2 | wall-layer id |

Enums: `GenTile : uint8_t` (foreground), `GenWall : uint8_t` (wall). `flatGen` uses 1 grass, 2 dirt, wall id 3.

### Constants

| Name | Value | Meaning |
|---|---|---|
| `kGenSurfaceBase` | 40 | mean surface row |
| `kGenCaveMinDepth` | 8 | caves start this many rows below the surface |
| `kGenCaveBand` | 6 | cave where `abs(noise - 128) <= band` (noise 0..255) |
| `kGenOreMinDepth` | 10 | |
| `kGenDeepOreMinDepth` | 40 | |

### Functions

| Signature | Meaning |
|---|---|
| `int terrainHeight(int tx, uint32_t seed)` | surface row (first non-air foreground tile, the grass row) of world column `tx`; same value `terrainGen` uses, so a game can place the player on the surface without loading a chunk |
| `void terrainTile(int tx, int ty, uint32_t seed, uint8_t* fg, uint8_t* wall)` | reference per-tile form of `terrainGen`: tile (tx, ty) in world coordinates, written to `*fg` and `*wall` |
| `void terrainGen(uint8_t* fg, uint8_t* wall, int cx, int cy, uint32_t seed)` | `GenFn`: surface from 3 octaves of 1D value noise (wavelengths 64, 16, 4), 1-2 dirt rows plus grass on top, stone below, caves as a thin band of 2D value noise, ore as a per-tile hash; fills fg and wall (`kChunkTiles` bytes each) |

### Round worlds (planets and asteroids)

A disc in a square grid of chunks, as a pure integer function of (tile coords, seed) with the same hash/noise helpers and tile ids as `terrainGen`, plus `kGenCore` for the core at the centre. Host tested only; generation cost on the F429 is not measured.

| Name | Value | Meaning |
|---|---|---|
| `RoundSpec` | `int chunks; int radius; int core = 0;` | grid is `chunks` x `chunks` chunks (`chunks * 32` tiles per side); `radius` = disc radius in tiles; `core` = radius in tiles of the `kGenCore` disc at the grid centre, 0 = none (so `{4, 60}` is the planet without a core) |
| `kPlanetSpec` | `{4, 60, 3}` | 128 x 128 tiles, core 6 tiles across |
| `kAsteroid1Spec` | `{1, 14, 2}` | 32 x 32 tiles, core 4 tiles across |
| `kAsteroid2Spec` | `{2, 29, 2}` | 64 x 64 tiles, core 4 tiles across |

| Signature | Meaning |
|---|---|
| `void roundTile(const RoundSpec& s, int tx, int ty, uint32_t seed, uint8_t* fg, uint8_t* wall)` | reference per-tile form: tile (tx, ty) in world coordinates, written to `*fg` and `*wall` |
| `void roundGen(const RoundSpec& s, uint8_t* fg, uint8_t* wall, int cx, int cy, uint32_t seed)` | fills chunk (cx, cy) of the round world `s` (`kChunkTiles` bytes each, row-major); equals `roundTile` |
| `void planetGen(uint8_t* fg, uint8_t* wall, int cx, int cy, uint32_t seed)`, same signature for `asteroid1Gen` and `asteroid2Gen` | `GenFn` wrappers: `roundGen` with `kPlanetSpec`, `kAsteroid1Spec`, `kAsteroid2Spec` |
| `GravityDir roundGravity(int x, int y, int cx, int cy)` | gravity toward the centre (cx, cy) for a point (x, y); the four numbers share any one unit |
| `GravityDir roundTileGravity(const RoundSpec& s, int tx, int ty)` | `roundGravity` for tile (tx, ty) of the grid of `s`, with tile centres in doubled units (`2 * t + 1`) against the grid centre (`chunks * 32`) |

Rules for round worlds:

- Disc: a tile is inside when its doubled-centre distance squared to the grid centre is <= `(2 * radius)^2`. A tile outside the disc or outside the grid is air (0) in both layers, even when `radius` is too large for the grid.
- Depth: the distance of a tile centre from the rim in whole tiles, `(2 * radius - isqrt(d2)) / 2` with integer division, where `d2` is the doubled-centre distance squared of the disc rule and `isqrt` the floor square root. A tile just inside the rim has depth 0 or 1. Ore, deep ore and the wall layer use this depth.
- Grass: every solid tile whose centre is within one tile of the rim (`2 * radius - isqrt(d2) <= 2`), so every solid tile with a 4-neighbour outside the disc is grass; where the rim runs diagonally a few tiles behind the edge are grass too. Grass has no wall.
- Foreground below the grass: 1-2 dirt tiles (patchy by direction), then stone, `kGenOre` from depth `radius / 6` and `kGenDeepOre` from depth `2 * radius / 3` in zones that depend on direction and depth. For `kPlanetSpec` these depths (10 and 40) equal `kGenOreMinDepth` and `kGenDeepOreMinDepth`.
- Ore is not guaranteed in asteroids: for some seeds an asteroid has no `kGenOre` or no `kGenDeepOre`; the planet preset (`kPlanetSpec`) always has both.
- Core: a tile whose doubled-centre distance squared to the grid centre is <= `(2 * core)^2` is `kGenCore` with `kGenWallStone` behind it, whatever its depth (clipped to the disc, so a core of `radius` or more fills the disc; `core <= 0` is no core). It is a tile staircase, not a smooth disc: the middle rows span `core` tiles on each side of the grid centre line, so `core` 1 is the 2 x 2 centre block, 2 is 12 tiles and 3 is 32 tiles. A game marks `kGenCore` solid and not breakable, so a character that digs towards the centre stops at the core instead of reaching the point where the direction to the centre swings with a few pixels of motion. The presets carry 3 (planet) and 2 (asteroids); they were not measured with a character on a board, and the planet example needed 6 tiles for a 16 x 32 px character on 8 px tiles (see `examples.md`, planet).
- Wall layer (not under grass): `kGenWallDirt` for depth < 8, then `kGenWallStone`.
- Gravity points toward the centre: right of it gives `Left`, below it gives `Up`. The larger axis offset wins; an exact diagonal tie goes to the vertical axis, and the centre itself gives `Down`.
- `GenFn` has no context argument, so each wrapper has its size baked in. A game with another size calls `roundGen` with its own `RoundSpec` from its own `GenFn`.
- Shortcuts (noted in the header): perfectly round surface (no hills), no caves, the core is solid (no cavity, no ore inside).

## pge/entity.h

Entity pool with tile-sweep physics and a uniform-grid overlap index. Per-slot data lives in parallel arrays
allocated from an `Arena` in `init`; nothing else allocates. Includes `pge/collision.h`, `pge/config.h`,
`pge/error.h`, `pge/gfx.h`, `pge/memory.h`, `pge/sprite.h`, `pge/world.h`.

### Handles

| Name | Definition | Meaning |
|---|---|---|
| `struct EntityId` | `uint16_t index, gen;` | a handle goes stale when its slot is freed (the slot's gen increments on free only) |
| `kNoEntity` | `EntityId{0xFFFF, 0}` | |
| `bool isValid(EntityId id)` | `id.index != 0xFFFF` | |

### Per-entity data

| Type | Fields | Meaning |
|---|---|---|
| `Motion` | `int16_t vx, vy; uint8_t rx, ry;` | `vx`, `vy` in 8.8 px/tick; `rx`, `ry` are the sub-pixel remainder 0..255 carried between ticks |
| `EntityMeta` | `uint8_t kind; uint8_t anim; uint16_t flags; uint16_t anim_t; GravityDir gravity_dir;` | `flags` are `EntityFlag` bits; `gravity_dir` is set by the game each tick, `Down` after spawn |
| `PhysicsParams` | `int gravity; int max_fall;` | `gravity` in 8.8 per tick^2, >= 0; `max_fall` in 8.8 per tick, >= 0; both act along `EntityMeta::gravity_dir` (`max_fall` caps the speed along it only) |
| `Health` | `int16_t hp, max_hp; uint8_t invuln_ticks; uint8_t flags;` | `invuln_ticks` is counted down by `update()`, also for bodies frozen in unloaded chunks; `flags` are `HealthFlag` |
| `Event` | `EventType type; EntityId a; int16_t amount; uint8_t aux;` | notification only: `Health::hp` is authoritative; `aux` is the `kind` passed to `applyDamage` |

### Enums

`enum EntityFlag : uint16_t`: low byte holds engine opt-ins (0x04..0x80 free for games); 0x0100..0x0400 are results
written by `update()`.

| Name | Value | Meaning |
|---|---|---|
| `kEntGravity` | 0x01 | |
| `kEntCollide` | 0x02 | |
| `kEntOnGround` | 0x0100 | result written by `update()` |
| `kEntHitX` | 0x0200 | result written by `update()` |
| `kEntHitY` | 0x0400 | result written by `update()` |
| `kEntHealth` | 0x1000 | internal: slot has a valid `Health` (`addHealth`) |
| `kEntIndexed` | 0x2000 | internal: in the grid built by the last `rebuildGrid` |
| `kEntFresh` | 0x4000 | internal: spawned this tick, `update()` skips it |
| `kEntDying` | 0x8000 | internal: `despawn()` called, slot freed in `endTick` |

| Enum | Values | Meaning |
|---|---|---|
| `enum HealthFlag : uint8_t` | `kHealthAutoReap = 0x01` | `endTick` despawns the entity once `hp <= 0` |
| `enum class EventType : uint8_t` | `Damage, Death` | event kinds pushed by `applyDamage` |

### Free function `entityFrame`

| Signature | Meaning |
|---|---|
| `uint16_t entityFrame(const EntityMeta& m, const Animation* table, uint8_t count)` | frame to draw for `m`: `animFrame(table[m.anim], m.anim_t)`, or 0 for a null table or an anim id >= `count` |

### `class Entities`

| Signature | Meaning |
|---|---|
| `Error init(Arena& a, uint16_t capacity = 0)` | capacity 0 = `PGE_ENTITY_COUNT`; `InvalidArg` if capacity > 65533; `OutOfMemory` if the arena runs out |
| `EntityId spawn(uint8_t kind, int x, int y, uint8_t w, uint8_t h, uint16_t flags = 0)` | `kNoEntity` (and `counters().full` + 1) when the pool is full; game flags only: internal bits are masked off |
| `void despawn(EntityId id)` | deferred to `endTick`; `alive()` is false at once; stale or repeated ids are ignored |
| `bool alive(EntityId id) const` | |
| `Body* body(EntityId id)` | |
| `Motion* motion(EntityId id)` | |
| `EntityMeta* meta(EntityId id)` | |
| `void setAnim(EntityId id, uint8_t anim)` | restarts `anim_t` only when the animation id changes |
| `bool addHealth(EntityId id, int16_t max_hp, uint8_t flags = 0)` | false if not alive or `max_hp <= 0`; sets `hp = max_hp`, invuln 0 |
| `Health* health(EntityId id)` | `nullptr` if not alive or no health |
| `bool applyDamage(EntityId target, int16_t amount, uint8_t kind, uint8_t invuln_ticks = 0)` | false (no change, no event) if the target is not alive, has no `Health`, `hp <= 0`, is invulnerable or `amount <= 0`; else `hp = max(0, hp - amount)`, `invuln_ticks` = the argument, pushes `Damage` (+ `Death` when hp reaches 0), `kind` goes to `aux` |
| `bool popEvent(Event& out)` | FIFO; false when empty |
| `uint16_t eventsDropped() const` | oldest events lost to a full queue (saturating) |
| `uint16_t eventCount() const` | |
| `template <class F> void forEach(F fn)` | `fn(EntityId, Body&)` for every live, non-dying entity (fresh ones included), ascending slot order |
| `void update(const World& w, const PhysicsParams& p)` | gravity, sub-pixel motion and tile collision for live, non-dying, non-fresh slots in ascending order; a body overlapping an in-bounds chunk that is not loaded is frozen (velocity kept); gravity, on-ground and the sweep follow each body's `gravity_dir`; sub-pixel remainders floor in world axes, so with a sub-pixel gravity a body under Up or Left gravity moves its first pixel up to one tick earlier than under Down or Right (rest positions are the same) |
| `void endTick()` | despawns `hp <= 0` entities with `kHealthAutoReap`, frees dying slots (gen + 1), clears `kEntFresh` |
| `void rebuildGrid(Rect active_px)` | indexes every live entity by the cell of its top-left corner, once per tick; entities whose cell lies outside `active_px` (or outside the `PGE_ENTITY_GRID_CELLS` budget) are not indexed |
| `uint16_t queryOverlap(Rect r, EntityId* out, uint16_t cap) const` | live entities indexed by the last `rebuildGrid` whose `Body` strictly overlaps `r` (touching edges do not count); writes at most `cap` ids; order is deterministic (cell scan order) |
| `uint16_t size() const` | slots in use, dying ones included until `endTick` |
| `uint16_t capacity() const` | |
| `const PoolCounters& counters() const` | |
| `uint32_t gridClamped() const` | `rebuildGrid` calls that had to shrink the grid |
