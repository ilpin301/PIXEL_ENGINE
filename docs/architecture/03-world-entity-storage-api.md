# 03 World, entity, storage, resource, audio and UI API (session S2.3, 2026-11-15)
API spec - not implementation. Listings show contracts; signatures may be refined in S5 and any change is logged in `STATUS.md`. Tags: [verified] source named; [calculated] formula shown; [assumed] design judgement or budget.

## Summary for later sessions
- World (K8): 32x32-tile chunks, 2 layers (fg, wall), 1 B per tile = 2,048 B payload per chunk. Chunk = generator output + stored change list. Dropping a chunk never needs a write, because every `set` is already in the change log.
- Reserved tile ids: 0 = air, 255 = void (solid, indestructible). `get` outside the world or in an unloaded chunk returns 255, so nothing falls out of the world.
- Cache sizing rule [calculated]: chunks needed = (ceil(R_w / chunk_px) + 1) x (ceil(R_h / chunk_px) + 1), R = bounding box of (view + margin of m chunks on each side) and the active entity region (view + 32 px). MEDIUM (16 px tiles, m = 1) = 16; SMALL (8 px tiles, m = 0) = 4. Matches the 01 cache counts. Parallax layers never use the chunked World.
- Position range limit [calculated]: `Fix` 16.16 holds +-32,767 px, so a world is at most 2,047 tiles per axis at 16 px, 4,095 at 8 px.
- Change log: sorted array, 5 B per entry (u16 chunk, u16 location, u8 tile). Full log makes `set` return OutOfMemory; overflow policy D19 (section 2.3). Insert is O(n) (shortcut, upgrade if S5.1a measures > 1 ms).
- Tile properties: 4 B per tile id, 256 ids = 1,024 B in ROM. Drops and item meaning are game data.
- Entities (K9): 24 B body record, parallel arrays, handle = index + generation. Update order = ascending slot index. Spawn during a tick takes effect next tick; despawn is deferred to the end of the tick.
- Physics: per-axis sweep against solid tiles, max one tile per sub-step, no tunnelling. Gravity helper in the engine; the jump controller is game code.
- Health (G1): engine component `Health` + damage event queue (drop-oldest, `eventsDropped()`); hp is authoritative, not events. The game decides what dies, drops or respawns. Enemy AI and crafting are game code.
- Save (S5.3a): 32 B header with magic, version, CRC-32 of header and payload; sections are type-length-value, unknown types skipped. Streaming write through the storage I/O buffer (no contiguous payload). Two-slot write (`path.tmp`; remove `path.bak`; `path` -> `path.bak`; `path.tmp` -> `path`) needs only remove plus rename to a free name. Isolated edit costs about 3 B in the save [calculated].
- Resources: asset ids are compile-time numbers from `tools/`; ROM assets are zero-copy pointers; no runtime string lookup; no asset compression in v1.
- Audio: SPSC command queue from game to mixer; the HAL fill callback runs the mixer; integer mixing. 8 voices at 22,050 Hz = 176,400 voice-samples/s [calculated].
- UI: stateless draw helpers called inside `onRender`; menu navigation is a separate state object updated in `onTick`, because the render callback must not change state (02).
- RAM added on top of the 01 table [calculated, section 8]: SMALL +3,764 B, MEDIUM +24,048 B, LARGE +11,344 B internal and +163,840 B external. New totals: SMALL 20,276 B; MEDIUM 101,744 B; LARGE 101,840 B internal + 448,512 B external. Scale staging (01 section 5.3) is still extra.
- Decisions proposed: D13-D19 (section 9). Next: S2.4 GLM review of 01, 02, 03.

## 1 Conventions
- Same conventions as 01 section 7 and 02 section 1. `TileId` = `uint8_t`. Tile size `PGE_TILE_PX` is a compile-time power of two (8 or 16), set in the profile or user config. `Rect` is in pixels unless the name says tiles.
- Layers: `Layer::Fg = 0`, `Layer::Wall = 1`.
- Handles (`EntityId`, `Voice`) are `{u16 index, u16 gen}`; index 0xFFFF = invalid; a stale handle fails `alive()`.

## 2 World
### 2.1 API
```cpp
// API spec - not implementation. pge/world.h
namespace pge {
constexpr TileId kTileAir = 0, kTileVoid = 255;
using GenFn = void (*)(uint8_t* fg, uint8_t* wall, int16_t cx, int16_t cy, uint32_t seed);  // fills 1,024 + 1,024 B
using ActivateFn = void (*)(World&, int tx, int ty, TileId t, void* user);
struct WorldConfig { uint16_t w_chunks, h_chunks; uint32_t seed; GenFn gen; ActivateFn on_activate; void* user; };
class World {
 public:
  Error init(const WorldConfig&);               // allocates cache + change log from the init arena
  TileId get(int tx, int ty, Layer l) const;    // O(1); kTileVoid if out of bounds or chunk not loaded
  Error  set(int tx, int ty, Layer l, TileId t);// logs the change; OutOfMemory if the log is full
  bool   inBounds(int tx, int ty) const;
  uint32_t changeCount() const;  uint32_t changeCapacity() const;   // log use / cap (D19)
  bool   changeLogNearFull() const;             // ChangeLogNearFull flag: log >= 90% full
  TileSource source(Layer l) const;             // read-only chunk-row source for Canvas::tilemap (02 section 3.1)
  void   ensureLoaded(Rect view_px);            // loads/evicts around the view; at most PGE_MAX_CHUNK_LOADS_PER_TICK
  bool   activate(int tx, int ty);              // calls on_activate if the tile has the ACTIVATABLE flag
  const TileProps& props(TileId t) const;
  uint32_t seed() const;
  Error  writeChanges(Writer&) const;  Error readChanges(Reader&);   // save section type 1
};
}
```
### 2.2 Chunks, cache, generation
- Chunk = 1,024 fg + 1,024 wall bytes, row-major. Cache slot metadata (cx, cy, LRU stamp, flags) is 8 B per slot, kept in a separate array [calculated in section 8].
- Load: slot taken from the free list or the least recently used slot; `gen(fg, wall, cx, cy, seed)` runs; then the change-log entries for that chunk are applied. Generation must be a pure function of (cx, cy, seed): same inputs, same bytes (tested in S5.1b).
- Evict: the slot is simply reused. No write-back is needed, because `set` already logged the edit.
- `ensureLoaded` is called once per tick by the engine with the union of the camera view plus the prefetch margin and the active entity region (summary: cache is sized for that union); load work per tick is bounded by `PGE_MAX_CHUNK_LOADS_PER_TICK` (default 1). Measured generation time decides the default in S5.1b.
- Bodies standing in an unloaded chunk are marked sleeping and do not move.
Tradeoff: regenerating chunks costs CPU at load time in exchange for storing only the player's edits (K8).

### 2.3 Change log and bounds
- Entry: u16 chunk index, u16 location = (layer << 10) | (ty_local << 5) | tx_local, u8 new tile. Kept sorted by (chunk, location); a second `set` of the same cell overwrites its entry.
- Capacity from the profile: SMALL 512, MEDIUM 4,096, LARGE 32,768 entries. Full log: `set` returns OutOfMemory.
- Overflow policy (D19) [assumed]: the cap is a gameplay limit, not a bug. At >= 90% full (`count x 10 >= capacity x 9`) World raises `ChangeLogNearFull`. The engine cannot compact the log (no write-back, D13), so the game picks: (a) stop accepting edits and show a player-visible message; (b) later option, not v1: save plus a snapshot section (new save type) that becomes the new chunk base so the log can reset, which needs a load path that skips the generator (S5.1a open item).
- Out-of-bounds `get` returns void; out-of-bounds `set` returns InvalidArg.

### 2.4 Tile properties
```cpp
// API spec - not implementation. 4 B per tile id, array of 256 in ROM, supplied by the game
struct TileProps { uint16_t flags; uint8_t hardness; uint8_t light; };   // light: high nibble emit, low nibble absorb
// flags: SOLID 0x01, ONE_WAY 0x02, HAZARD 0x04, CLIMB 0x08, ACTIVATABLE 0x10, BREAKABLE 0x20, TRANSPARENT 0x40
```
`hardness` is dig ticks (0 = cannot be dug). Item drops, tool rules and tile names are game data. Animated tiles and liquid simulation are out of scope for v1.

### 2.5 Tile-activation hook
The game (or an entity interaction) calls `world.activate(tx, ty)`. If the tile has ACTIVATABLE, the engine calls `on_activate(world, tx, ty, tile, user)`. The game opens a door by `set(tx, ty, ...)`; chest contents are game data keyed by (tx, ty) and saved in the game blob. Tradeoff: one hook, no per-tile callbacks, no engine concept of "door" or "chest".

## 3 Entities, physics, health, particles
### 3.1 API
```cpp
// API spec - not implementation. pge/entity.h, pge/physics.h
struct Body { Fix x, y, vx, vy; uint8_t w, h; uint16_t flags; uint8_t kind; uint8_t anim; };   // 24 B
struct Health { int16_t hp, max_hp; uint8_t invuln_ticks; uint8_t flags; };                     // 6 B, optional module
struct EntityId { uint16_t index, gen; };
class Entities {
 public:
  EntityId spawn(uint8_t kind, Fix x, Fix y, uint8_t w, uint8_t h);   // invalid id if the pool is full (counter pool_full)
  void  despawn(EntityId);                                            // takes effect at the end of the tick
  bool  alive(EntityId) const;
  Body* body(EntityId);   Health* health(EntityId);                   // nullptr if not alive / no health
  template <class F> void forEach(F fn);                              // ascending slot order
  uint16_t queryOverlap(Rect r, EntityId* out, uint16_t cap) const;   // uniform grid, active region only
  void rebuildGrid(Rect active_px);                                   // once per tick
};
struct MoveResult { bool hit_x, hit_y, on_ground; uint16_t touched_flags; };
MoveResult moveAndCollide(const World&, Body&, Fix dx, Fix dy);       // X axis, then Y axis, <= 1 tile per sub-step
void       applyGravity(Body&, Fix g, Fix max_fall);
enum class EventType : uint8_t { Damage, Death };
struct Event { EventType type; EntityId a; int16_t amount; uint8_t aux; };           // 8 B
bool applyDamage(Entities&, EntityId target, int16_t amount, uint8_t kind);          // false if invulnerable or dead
bool popEvent(Event& out);                                                           // game drains in onTick
uint16_t eventsDropped();                                                            // overflow counter (also Stats.events_dropped)
```
### 3.2 Contracts
- Update order: ascending slot index. Entities spawned during a tick do not update until the next tick. Despawned entities are swept at the end of the tick, so `forEach` never sees a hole.
- `moveAndCollide` splits a move into sub-steps of at most one tile, so no tunnelling. Solid = tile flag SOLID; ONE_WAY blocks only when moving down from above. Bodies in unloaded chunks are not moved.
- `applyDamage` reduces `hp`, sets `invuln_ticks` (length is game data, passed through the config of the kind), pushes a Damage event, and pushes a Death event when `hp` reaches 0. The engine never despawns for the game unless the kind sets `Health.flags` bit AUTO_REAP (0x01).
- Event queue (capacity 16 / 32 / 64): when full the oldest event is dropped and `eventsDropped()` counts it, same style as the audio queue. Events are notifications only: `hp` is authoritative and the game must not rely on delivery for health state. A 0-hp entity is never stranded: with AUTO_REAP the engine despawns it at the end of the tick in which hp reached 0, regardless of events; otherwise the game finds it by scanning `hp <= 0` in `forEach`; `applyDamage` on it returns false.
- Particles are a separate pool with a compact 8 B record (x, y int16; vx, vy int8; life; color index), same spawn/despawn contract, no collision. This differs from the plan wording "pooled entity kind" only in record size, to keep particles cheap.
Tradeoff: parallel arrays and fixed pools give predictable time and no fragmentation (K5, K9) but need sizes chosen per profile.

## 4 Storage, save, config, resources
### 4.1 Storage and atomic save
```cpp
// API spec - not implementation. pge/storage.h
// Streaming writer/reader callbacks: the payload is never held contiguously in RAM.
using Producer = Error (*)(void* ctx, uint32_t offset, void* dst, uint32_t max, uint32_t* written);   // fills dst, *written = 0 ends
using Consumer = Error (*)(void* ctx, uint32_t offset, const void* src, uint32_t n);
struct SaveInfo { uint32_t world_seed, game_tick; uint16_t w_chunks, h_chunks; };
class Storage {
 public:
  Error mount();                                                                     // NotSupported: no medium, RAM-only world
  Error readFile(const char* path, void* buf, uint32_t cap, uint32_t& len);          // small files (config); Corrupt/Io on failure
  Error writeFile(const char* path, const void* data, uint32_t len);                // plain write, small files
  Error saveAtomic(const char* path, const SaveInfo&, Producer produce, void* ctx); // streaming, two-slot, below
  Error loadSave(const char* path, SaveInfo& info, Consumer consume, void* ctx);    // streaming; CRC checked before the first consume
  bool  exists(const char* path) const;
};
```
`saveAtomic`: (1) pass 1 calls `produce` to count payload_len and CRC; (2) write `path.tmp` (header, then pass 2 of `produce`) in chunks through the storage I/O buffer, offsets ascending from 0, same bytes in both passes (game state does not change during the call); (3) read back in chunks and verify the CRC; (4) `remove(path.bak)` (if it exists; NotFound is ignored); (5) rename `path` to `path.bak` (if it exists); (6) rename `path.tmp` to `path`. Load order: `path`, then `path.bak` when the first is missing or fails its CRC; a leftover `path.tmp` is ignored. A crash between (4) and (6) leaves `path` or `path.bak` valid. This needs only remove plus rename to a free name (02 rename contract), so it works on FatFs, LittleFS and the host. Needs a mounted medium, else NotSupported. Autosave is the game's decision; no periodic writes by the engine (R6 flash wear).

### 4.2 Save format v1 (all little-endian)
| Offset | Size | Field |
|---|---|---|
| 0 | 4 | magic "PGES" |
| 4 | 2 | format_version (1) |
| 6 | 2 | header_size (32) |
| 8 | 4 | world seed |
| 12 | 2 | world_w_chunks |
| 14 | 2 | world_h_chunks |
| 16 | 4 | payload_len |
| 20 | 4 | game_tick |
| 24 | 4 | payload_crc32 (CRC-32/IEEE over the payload) |
| 28 | 4 | header_crc32 (over bytes 0..27) |

Payload = sections, each `u8 type, u8 flags, u32 len, data`. Type 1 = world changes, type 2 = game blob (opaque to the engine; the game writes its own version number first). Unknown types are skipped by `len`. A newer `format_version` is rejected with Corrupt; an older one is migrated by code added at that time.
World-changes section: per changed chunk `u16 chunk_index, varint n_runs`, then runs `varint loc_delta, varint run_len, u8 tile` (loc_delta from the end of the previous run; run = consecutive locations with the same tile). Varint = LEB128.
Size [calculated]: isolated edit = 3 B (delta 1, run 1, tile 1) + 3 B per touched chunk (index 2, n_runs 1). Worst case with a full log: LARGE 32,768 x 3 = 98,304 B; SMALL 512 x 3 = 1,536 B (streamed through the I/O buffer, never held whole; plus the game blob).

### 4.3 Config file
Text `key = value`, one per line; `Config::getInt(key, default)`, `getStr(key, default)`. Fixed table of PGE_CONFIG_MAX_KEYS entries of 56 B each (24 B key, 32 B value) [assumed]; SMALL 8, MEDIUM 16, LARGE 32. Over-long keys or lines are skipped and counted.

### 4.4 Resource manager
```cpp
// API spec - not implementation. pge/resource.h
using AssetId = uint16_t;                       // enum generated by tools/ (S5.4)
enum class AssetType : uint8_t { Sprites, Tileset, Font, Sound, Pattern, Blob };
struct AssetInfo { AssetType type; uint32_t size; bool in_rom; };
class Resources {
 public:
  Error info(AssetId, AssetInfo&) const;
  const void* rom(AssetId) const;                                   // zero-copy pointer, nullptr if storage-backed
  Error load(AssetId, void* dst, uint32_t cap, uint32_t& len);      // ROM copy or Storage read
  Error loadPersistent(AssetId, const void** out);                  // init arena, allowed only in onCreate
};
```
Asset blobs start with a header (magic, type, version, size) checked by `PGE_CHECK`; field layouts come from the tools in S5.4. There is no runtime string lookup (02 difference 3). No asset compression in v1: indexed 4/8 bpp is already small; revisit after S6.2 measurements [assumed].

## 5 Audio
```cpp
// API spec - not implementation. pge/audio.h, present only if the profile has audio_channels > 0
struct Sound { const void* data; uint32_t frames; uint16_t rate; uint8_t format; };   // PCM8 or PCM16, mono
struct Voice { uint16_t index, gen; };
class Mixer {
 public:
  Error init(uint32_t sample_rate);             // registers fill() with the HAL
  Voice play(const Sound&, uint8_t volume, bool loop, uint8_t priority);
  void  stop(Voice);   void setVolume(Voice, uint8_t);   void setMaster(uint8_t);
  Error playMusic(const Pattern&, bool loop);   void stopMusic();
  void  fill(int16_t* stereo, uint32_t frames); // called by the platform audio context
};
```
- Threading: game calls enqueue commands into an SPSC queue (no lock, no allocation); `fill` drains it first. A full queue returns an invalid `Voice` and counts `audio_dropped`. No free voice: the lowest-priority voice is stolen if the new sound has higher priority, else the call fails.
- Mixing: integer, 16-bit accumulate with saturation, nearest-sample resampling with a 16.16 step. Pattern music: simple tracker-style events, format fixed in S5.5.
- State: channel states plus queue = 8 channels x 8 B + 8-entry queue x 8 B = 128 B [calculated]. The 01 audio rows (2,176 MEDIUM / 4,224 LARGE) hold this 128 B plus the mix buffer (2,048 / 4,096 B [calculated]); MEDIUM's 4 channels use less than the 128 B.
- CPU [calculated, cycles per voice-sample assumed 20]: 22,050 Hz x 8 voices = 176,400 voice-samples/s; x 20 = 3,528,000 cycles/s = 1.5% of 240 MHz, 2.0% of 180 MHz, 2.7% of 133 MHz. Measured in S5.5.
- F429I-DISCO has no audio codec; SAI1 block A or TIM9 PWM is possible (02 section 6); the backend exists only if S5.5 finds a usable output.

## 6 UI and debug overlay
```cpp
// API spec - not implementation. pge/ui.h, pge/debug.h
namespace ui {
struct Style { const Font* font; Color fg, bg, accent; uint8_t pad; };
void label(Canvas&, const Style&, int x, int y, const char* text);
void bar(Canvas&, const Style&, Rect r, uint16_t value, uint16_t max, Color fill);
void iconGrid(Canvas&, const Style&, Rect area, const SpriteSheet&, const uint16_t* icons, const uint8_t* counts, uint8_t cols, uint8_t rows, int8_t selected);
void listMenu(Canvas&, const Style&, Rect r, const char* const* items, uint8_t n, uint8_t selected);
int  formatU32(char* buf, uint8_t cap, uint32_t v);          // no printf in the core (01 P-4)
struct MenuNav { uint8_t selected; int update(const Input&, uint8_t n); };   // returns activated index or -1; call in onTick
}
struct Stats { uint32_t tick_us, tick_us_max, frame_us, band_us, band_us_max, flush_us; uint16_t fps, draw_dropped, world_miss, chunks_loaded, audio_dropped, events_dropped; uint16_t pool_high[8], pool_full[8]; };
const Stats& stats();   void drawOverlay(Canvas&, const ui::Style&, uint8_t flags);
```
- Widgets are stateless draw helpers called inside `onRender` (per band, culled by `canvas.visible`). Selection state lives in the game or in `MenuNav`, updated in `onTick`, because the render callback must not change state.
- `drawOverlay` shows fps, frame/band/flush time, pool use and drop counters; it is compiled out when the profile disables it.

## 7 Engine versus game split
| Topic | Engine provides | Game provides |
|---|---|---|
| Inventory | `ui::iconGrid`, `MenuNav`, save game-blob hooks, Storage | item ids, stack rules, slots, selected slot, serialisation of its blob |
| Crafting | `ui::listMenu`, `MenuNav` | recipe table (inputs, output, station tile), craft rules (S6.2) |
| Enemy AI | entity pools, `moveAndCollide`, `queryOverlap`, world queries | behaviours (walk, turn at ledge, chase), spawn rules (S6.1) |
| Health and damage | `Health`, `applyDamage`, invulnerability timer, Damage/Death events | damage values, what dies, drops, respawn, UI meaning |
| Items and drops | tile hardness field, tile properties | item definitions, drop tables, tool rules |
| Interactive tiles | ACTIVATABLE flag, `activate` hook | what a door, chest or switch does |
| Player controller | `applyGravity`, `moveAndCollide` | jump, speed, input mapping |
| Lighting | tile `light` field, optional module (S5.6) | which tiles emit light |

## 8 RAM and CPU estimates
All RAM entries are allocation budgets [assumed sizes, calculated sums]. Additions to the 01 table:

| Item | SMALL | MEDIUM | LARGE internal | LARGE external |
|---|---|---|---|---|
| Chunk metadata (8 B per slot) | 32 (4) | 128 (16) | 512 (64) | - |
| Change log (5 B per entry) | 2,560 (512) | 20,480 (4,096) | - | 163,840 (32,768) |
| Health array (6 B per entity) | 384 (64) | 1,536 (256) | 6,144 (1,024) | - |
| Event queue (8 B per event) | 128 (16) | 256 (32) | 512 (64) | - |
| Overlap grid: cells x 2 B + entities x 2 B | 212 | 752 | 2,384 | - |
| Config table (56 B per key) | 448 (8) | 896 (16) | 1,792 (32) | - |
| Save staging (streaming Producer/Consumer, section 4.1) | 0 | 0 | 0 | - |
| Added total | 3,764 | 24,048 | 11,344 | 163,840 |
| New engine total (01 + added) | 20,276 | 101,744 | 101,840 | 448,512 |

Grid arithmetic [calculated], cell 32 px: SMALL (160+64)/32 = 7 by (128+64)/32 = 6 = 42 cells x 2 = 84, + 64 x 2 = 128 -> 212. MEDIUM 12 by 10 = 120 cells x 2 = 240, + 256 x 2 = 512 -> 752. LARGE 14 by 12 = 168 cells x 2 = 336, + 1,024 x 2 = 2,048 -> 2,384.
Sums: SMALL 32+2,560+384+128+212+448 = 3,764. MEDIUM 128+20,480+1,536+256+752+896 = 24,048. LARGE internal 512+6,144+512+2,384+1,792 = 11,344. 01 totals: SMALL 16,512 + 3,764 = 20,276; MEDIUM 77,696 + 24,048 = 101,744; LARGE internal 90,496 + 11,344 = 101,840; external 284,672 + 163,840 = 448,512.
Save staging is 0 extra: `saveAtomic` and `loadSave` stream through the 01 storage I/O buffer (512 / 4,096 / 16,384 B), so the worst-case 98,304 B payload needs no buffer of its own. Scale staging (01 section 5.3) stays outside these totals.
Flash: no estimate before code (01 section 5.4 caps stand). Tile properties 1,024 B and fonts count as assets.
CPU [calculated counts; cycle costs assumed, measured in S5.2]: tile sweep for a 16x32 body at 16 px tiles = (1+2) x (2+2) = 12 tile lookups per axis, 24 per move; 256 bodies = 6,144 lookups per tick. At an assumed 10 cycles per lookup and 60 ticks/s = 3.7 million cycles/s, 1.5% of 240 MHz. Optimistic: tile lookups in SDRAM (DISCO) or PSRAM (ESP32-S3) cost more than 10 cycles, so the 1.5% holds only for internal-RAM chunks until measured in S5.2; a per-chunk solid-row cache is the fallback [assumed]. Chunk generation time is UNKNOWN until S5.1b.

## 9 Decisions and open items
| Id | Decision (proposal for G2) | Tradeoff |
|---|---|---|
| D13 | Change log + regenerate; evict without write-back | log capacity bounds how much a player can edit; sorted insert is O(n) |
| D14 | Reserved ids 0 air and 255 void; unloaded or out-of-bounds reads as void | tile id space is 254 usable ids |
| D15 | Engine owns Health + events; AI, crafting, inventory are game code (G1) | engine has no concept of items or enemies |
| D16 | Two-slot streaming save with CRC-32 and TLV sections | one extra file; needs only remove plus rename to a free name; payload produced twice (count, write) |
| D17 | Asset ids are compile-time numbers; no string lookup, no compression in v1 | assets cannot be added without a tools run |
| D18 | Audio via SPSC command queue and HAL fill callback; UI widgets stateless | audio commands apply at the next fill; menus need a separate nav object |
| D19 | Change-log cap is a gameplay limit; `ChangeLogNearFull` at 90%, `changeCount()`/`changeCapacity()`; game stops edits or saves (snapshot compaction deferred) | players cannot edit without bound; no engine-side recovery in v1 |
Open for S2.4: GLM review of 01, 02, 03 (rounds = 3, digest `docs/architecture/ARCH-REVIEW.md`). Open for S4.1: tick overrun policy. Open for S4.2a: wire scale staging and frame arena into the profile. Open for S5.1a/S5.3a: the section sink/source types used by `World::writeChanges`/`readChanges` (called from inside a `Producer`/`Consumer`), the snapshot option of D19. UNKNOWN carried: chunk generation cost, DMA reach on F429, TE pin on the user's module.
