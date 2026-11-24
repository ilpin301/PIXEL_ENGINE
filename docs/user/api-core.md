# Pixel Game Engine: core API reference

> Summary for later sessions
> - Covers 9 core headers: pge/engine.h, config.h, memory.h, error.h, timestep.h, version.h, debug.h, resource.h, asset.h.
> - Types and functions live in `namespace pge`, except version.h (`PGE_VERSION_*`, `pge_version()`), the `PGE_*` macros
>   (config.h, error.h) and `pge_hal_time_us()` / `pge_raw_input`, which are global names as written in the headers.
> - Names, signatures, constants and numbers are copied from the headers; meanings come only from header comments.
> - Where a header gives no comment, only the signature is listed.
> - Source: core/include/pge/. Not covered: gfx.h, ui.h, storage.h, hal/*.

## pge/engine.h

Engine object, input sampling, fixed-step game loop and run-time statistics.
Includes config.h, error.h, gfx.h, memory.h, timestep.h, hal/input.h, hal/time.h.

### struct Stats

| Field | Meaning |
|---|---|
| `uint32_t tick_us` | last onTick duration |
| `uint32_t tick_us_max` | longest onTick seen |
| `uint32_t frame_us` | last render pass (renderFrame, all bands) |
| `uint32_t band_us` | sum of onRender time over the bands of the last frame |
| `uint32_t band_us_max` | largest single-band onRender time seen |
| `uint32_t flush_us` | frame_us - band_us: pre-clear + flush + overhead (renderFrame is not instrumented) |
| `uint32_t ticks` | ticks run |
| `uint32_t ticks_dropped` | ticks skipped by the overrun policy |
| `uint32_t tick_overruns` | loop passes that hit PGE_MAX_CATCHUP |
| `uint16_t fps` | frames rendered in the last full second |
| `uint16_t draw_dropped` | invalid-argument draw calls in the last frame (saturating) |
| `uint16_t pool_high[PGE_POOL_KINDS]` | per pool kind (no comment in header) |
| `uint16_t pool_full[PGE_POOL_KINDS]` | per pool kind (no comment in header) |

### enum class Button : uint32_t

`Up = PGE_BTN_UP`, `Down = PGE_BTN_DOWN`, `Left = PGE_BTN_LEFT`, `Right = PGE_BTN_RIGHT`, `A = PGE_BTN_A`,
`B = PGE_BTN_B`, `Start = PGE_BTN_START`, `Select = PGE_BTN_SELECT`.

### class Input

Buttons, 4 analog axes and one touch point, sampled once per tick; pressed / released are edges against the previous
tick. How physical keys, pins, sticks and the mouse map onto buttons / axes / touch is decided by the platform HAL
backend (see hal/input.h), not here.

| Member | Meaning |
|---|---|
| `bool held(Button b) const`, `bool pressed(Button b) const`, `bool released(Button b) const` | pressed / released are edges against the previous tick |
| `int axis(unsigned i) const` | -32768..32767, 0 for i >= 4 |
| `bool touching() const`, `bool touchPressed() const`, `bool touchReleased() const` | |
| `int touchX() const`, `int touchY() const` | logical screen coordinates (raw display coordinates / scale); keep their last value after release, 0 before the first touch |
| `void update(const pge_raw_input& raw, int scale = 1)` | called by Engine each tick; public so tests can feed it; scale < 1 is treated as 1; the QUIT bit is the engine's business, never visible to games |

### class Engine

| Member | Meaning |
|---|---|
| `void requestQuit()`, `bool quitRequested() const`, `uint32_t tickCount() const` | |
| `Arena& initArena()`, `Arena& frameArena()` | |
| `const Stats& stats()` | refreshes the pool fields from the tracked pools |
| `void trackPool(unsigned kind, const PoolCounters* c)` | ignored when kind >= PGE_POOL_KINDS |
| `uint32_t seed() const` | per frame (not per band), a function of the tick count |

Internal (header comment: used by `init()` and `run<G>()`): `Error setup()`, `bool ready() const`,
`const Input& input() const`, `void beginRun()` (clears the quit flag and stats, locks the init arena (K5), starts the
clock), `uint32_t advanceClock()` (ticks due now), `void pace()` (sleeps until the next tick is due),
`void pollInput()`, `void recordTick(uint32_t us)`, `int render(RenderFn fn, void* ctx)`, `void endFrame()`.

### Free functions

| Signature | Meaning |
|---|---|
| `Engine& engine()` | |
| `Error init()` | HAL display + input, arenas, band buffer; Busy if already done |
| `template <class G> Error run(G& game)` | runs the game loop; returns NotInitialized if `init()` was not done |

`G` requirements (header comment): `Error onCreate(Engine&)`; `void onTick(const Input&)`; `void onRender(Canvas&)`;
`void onDestroy()` (optional). Loop as written in the header: `onCreate`, then while not quit: `advanceClock()`; if 0
ticks are due, `pace()`; else per tick `frameArena().reset()`, `pollInput()`, `onTick`, `recordTick`; then render at
most once per loop pass, decoupled from the tick count (K4); a non-zero render result ends the run and is returned
as `Error`; finally `onDestroy()` if present.

`namespace detail` holds helpers for `run<G>()` (`destroy`, `renderTrampoline`).

## pge/config.h

Compile-time configuration. Order: optional game-side `pge_user_config.h`, then the board's `pge_profile.h` (exactly one
`PGE_PROFILE_*` = 1, plus `PGE_LOGICAL_W/H`), then the defaults below. Every macro can be pre-set.

Helpers: `PGE_PROFILE_SMALL`, `PGE_PROFILE_MEDIUM`, `PGE_PROFILE_LARGE` (each defaults to 0; a `static_assert` requires
exactly one to be 1); `PGE_BY_PROFILE(s, m, l)` picks the SMALL / MEDIUM / LARGE value.

Defaults (SMALL / MEDIUM / LARGE where profile-dependent):

| Macro | Default | Header comment |
|---|---|---|
| `PGE_BAND_LINES` | 8 / 16 / 32 | 01-overview section 6 table. LARGE band is 32 lines (G3 accepted 32; 01 says 16) |
| `PGE_SCALE` | 1 | K15: display = logical * PGE_SCALE |
| `PGE_SCALE_STAGING_BYTES` | `PGE_SCALE > 1 ? PGE_BAND_LINES * PGE_SCALE * PGE_LOGICAL_W * PGE_SCALE * 2 : 0` | 02 section 3.4: (N*s) * (W*s) * 2 with W logical |
| `PGE_MAX_CHUNK_LOADS_PER_TICK` | 1 | 03 section 2.2: chunk generations per ensureLoaded call |
| `PGE_STORAGE_IO_BYTES` | 512 / 4096 / 16384 | 03 section 4.1: save/load streaming buffer |
| `PGE_PATH_MAX` | 64 | longest save path accepted is PGE_PATH_MAX - 5 characters (room for ".tmp"/".bak") |
| `PGE_CONFIG_MAX_KEYS` | 8 / 16 / 32 | 03 section 4.3: settings table entries |
| `PGE_CONFIG_KEY_BYTES` | 24 | includes the terminating NUL |
| `PGE_CONFIG_VALUE_BYTES` | 32 | includes the terminating NUL |
| `PGE_AUDIO_QUEUE` | 8 / 16 / 32 | game -> mixer command queue entries |
| `PGE_AUDIO_MIX_FRAMES` | 128 | accumulator block: frames mixed per pass |
| `PGE_LIGHTING_ENABLED` | 0 | LARGE: optional, only after the S5.6 measurement |
| `PGE_DEBUG_OVERLAY` | 1 | compiled out when 0 (drawOverlay becomes an empty function) |
| `PGE_ENTITY_CELL_PX` | 32 | overlap-grid cell edge in pixels |
| `PGE_ENTITY_GRID_CELLS` | 42 / 120 / 168 | u16 list heads; covers the active rect in cells |
| `PGE_MAX_CATCHUP` | 5 | ticks run per loop pass; the rest are dropped and counted |

Defaults without a header comment: `PGE_LOGICAL_W` = 160 / 320 / 320; `PGE_LOGICAL_H` = 128 / 240 / 240; `PGE_INTEGER_SCALE_MAX` = 1 / 2 / 3; `PGE_PARALLAX_LAYERS` = 1 / 2 / 4; `PGE_CHUNK_CACHE_COUNT` = 4 / 16 / 64; `PGE_AUDIO_CHANNELS` = 0 / 4 / 8; `PGE_PARTICLE_COUNT` = 32 / 128 / 512; `PGE_ENTITY_COUNT` = 64 / 256 / 1024; `PGE_EVENT_QUEUE` = 16 / 32 / 64; `PGE_TICK_HZ` = 60; `PGE_POOL_KINDS` = 8; `PGE_INIT_ARENA_BYTES` = `PGE_BY_PROFILE(8192, 32768, 65536) + PGE_SCALE_STAGING_BYTES`; `PGE_FRAME_ARENA_BYTES` = 512 / 2048 / 4096; `PGE_CLEAR_COLOR` = 0; `PGE_GAME_SEED` = 0.

Placement macros (default: empty): `PGE_RAM_FAST`, `PGE_RAM_DMA`, `PGE_RAM_EXT`, `PGE_ROM`. Header comment: the
platform profile maps these to sections/attributes; core only applies them.

Compile-time checks (`static_assert`): `PGE_TICK_HZ` 1..1000; `PGE_SCALE` 1..`PGE_INTEGER_SCALE_MAX`; `PGE_MAX_CATCHUP` >= 1;
`PGE_BAND_LINES` >= 1; `PGE_ENTITY_COUNT` 1..65533 (0xFFFF is the sentinel); `PGE_ENTITY_CELL_PX` >= 1; logical size > 0.

## pge/memory.h

Bump arena over a caller-owned buffer and a fixed pool with an index free-list. Includes `<type_traits>`.

### class Arena

Bump allocator over a caller-owned buffer. Never frees single blocks; `reset()` drops everything.

| Member | Meaning |
|---|---|
| `void init(void* buf, size_t bytes)` | |
| `void* alloc(size_t bytes, size_t align = 8)` | nullptr (and `failed()` + 1) when exhausted, locked, bytes == 0 or align is not a power of two |
| `template <class T> T* allocArray(size_t n)` | |
| `void reset()` | keeps high-water, failed count and lock |
| `void lock()` | K5: no allocation after init; later alloc fails and is counted |
| `size_t used() const`, `size_t capacity() const`, `size_t highWater() const`, `uint32_t failed() const` | |

### struct PoolCounters

| Field | Meaning |
|---|---|
| `uint16_t high = 0` | high-water of live slots |
| `uint16_t full = 0` | failed alloc() calls, saturates at 65535 |

### template <class T, uint16_t N> class Pool

Fixed pool with an index free-list. Exhaustion returns `kInvalid` and counts; it never allocates. Compile-time checks:
N must be 1..65533; T must be trivially copyable ("Pool<T> holds raw bytes"). Header note: no generation counter, a
stale handle whose slot was reallocated is not detected; upgrade if games hold handles long.

| Member | Meaning |
|---|---|
| `typedef uint16_t Handle` | |
| `static constexpr Handle kInvalid = 0xFFFF` | |
| `Handle alloc()` | returns `kInvalid` and increments `counters().full` when exhausted |
| `bool free(Handle h)` | |
| `T* get(Handle h)`, `const T* get(Handle h) const` | |
| `uint16_t size() const`, `uint16_t capacity() const`, `const PoolCounters& counters() const` | |

## pge/error.h

Error codes and the trust-boundary guard macro.

`enum Error : int`: `Ok` = 0, `InvalidArg` = -1, `OutOfMemory` = -2, `NotSupported` = -3, `Io` = -4, `Timeout` = -5,
`Busy` = -6, `Corrupt` = -7, `NotInitialized` = -8.

`PGE_CHECK(cond, err)`: always-on trust-boundary guard; returns `err` from the calling function if `cond` is false
(header comment: no fatal HAL exists yet).

## pge/timestep.h

Fixed-timestep accumulator. Overrun policy: at most `max_catchup` ticks per `advance()`, the rest are dropped and
counted.

### class Timestep

| Member | Meaning |
|---|---|
| `void init(uint32_t hz, uint32_t max_catchup)` | header note: period is truncated (`1000000u / hz`), 60 Hz runs 40 ppm fast; upgrade with a remainder carry if a port needs exact rate |
| `uint32_t advance(uint32_t dt_us)` | adds dt_us and returns the ticks due now |
| `uint32_t usUntilNext() const`, `uint32_t periodUs() const`, `uint32_t dropped() const`, `uint32_t overruns() const`, `void reset()` | |

## pge/version.h

Version macros and version string accessor. Global names (not in `namespace pge`).

| Name | Meaning |
|---|---|
| `PGE_VERSION_MAJOR` | 0 |
| `PGE_VERSION_MINOR` | 1 |
| `PGE_VERSION_PATCH` | 1 |
| `const char* pge_version()` | |

## pge/debug.h

Debug overlay. Includes engine.h and ui.h.

| Name | Meaning |
|---|---|
| `enum OverlayFlags : uint8_t { kOverlayTiming = 1, kOverlayPools = 2, kOverlayAll = 3 }` | |
| `void drawOverlay(Canvas& c, const ui::Style& s, const Stats& st, uint8_t flags)` | text panel at the top-left of the canvas (max 16 lines of 40 chars); call it last in onRender, every band redraws it; does nothing when flags == 0 or when PGE_DEBUG_OVERLAY is 0 |

## pge/resource.h

Resource manager (docs/architecture/03 section 4.4). An asset is either a blob in ROM (zero-copy) or a file in the
Storage. The asset blob header and its parsers are in pge/asset.h. Includes error.h, memory.h, storage.h.

| Name | Meaning |
|---|---|
| `typedef uint16_t AssetId` | index into the game's const AssetEntry table (generated by tools/ in S5.4) |
| `enum class AssetType : uint8_t` | `Sprites, Tileset, Font, Sound, Pattern, Blob, Tilemap` |
| `struct AssetInfo { AssetType type; uint32_t size; bool in_rom; }` | |
| `struct AssetEntry { AssetType type; const void* rom; uint32_t size; const char* path; }` | rom != nullptr: the blob lives in flash/ROM; otherwise it is a file at `path` in the Storage |

### class Resources

| Member | Meaning |
|---|---|
| `void init(const AssetEntry* table, uint16_t count, Storage* storage)` | storage may be nullptr (ROM only) |
| `Error info(AssetId id, AssetInfo& out) const` | InvalidArg for id >= count |
| `const void* rom(AssetId id) const` | zero-copy pointer, nullptr if storage-backed or bad id |
| `Error load(AssetId id, void* dst, uint32_t cap, uint32_t& len)` | ROM: copy. Storage: readFile. cap < size -> OutOfMemory; file length != size -> Corrupt; storage-backed without Storage -> NotSupported; bad id or null dst with size > 0 -> InvalidArg |
| `Error loadPersistent(Arena& a, AssetId id, const void** out)` | ROM: `*out` = the ROM pointer (no copy, no arena use). Storage: size bytes (align 8) from the init arena `a`, then load. An arena failure (e.g. locked after the first tick) gives OutOfMemory |

## pge/asset.h

Asset blob format (docs/architecture/03 section 4.4): 12-byte header + payload, little-endian, written by
tools/pge_assets.py. The view functions are zero-copy: the engine structs point into the blob, which must outlive them.
Blobs may come from Storage, so everything is validated (Corrupt on any violation). Includes error.h, gfx.h, resource.h.

| Name | Meaning |
|---|---|
| `constexpr uint32_t kAssetMagic = 0x41454750` | bytes "PGEA" |
| `constexpr uint8_t kAssetVersion = 1` | |
| `constexpr uint32_t kAssetHeaderSize = 12` | magic u32, type u8, version u8, reserved u16 (0), payload size u32 |
| `struct AssetView { AssetType type; const uint8_t* payload; uint32_t size; }` | |
| `struct TilemapView { uint16_t w, h; const uint8_t* tiles; }` | |

Functions:

| Signature | Meaning |
|---|---|
| `Error assetCheck(const void* blob, uint32_t len, AssetType expect, AssetView& out)` | nullptr blob -> InvalidArg. Bad magic / version / reserved / type != expect / len < 12 / 12 + size > len -> Corrupt. Trailing bytes after the payload are allowed. Alignment is not checked here |
| `Error assetSprites(const void* blob, uint32_t len, SpriteSheet& out)` | payload: u16 w, h, frames, cols, ncolors; u8 transparent (0/1), u8 pad (0); ncolors RGB565 colors; 8-bit pixels (cols == 0: w*h*frames bytes stacked; cols > 0: atlas of (cols*w) x (ceil(frames/cols)*h) bytes). The blob must be 2-byte aligned (the palette is used in place), else InvalidArg. Every pixel index must be < ncolors |
| `Error assetTileset(const void* blob, uint32_t len, Tileset& out)` | same payload with cols == 0 and w, h = tile size in 1..255; `frames` is the tile count (not used by Tileset) |
| `Error assetTilemap(const void* blob, uint32_t len, TilemapView& out)` | payload: u16 w, u16 h, then w*h u8 tile ids (w, h >= 1, size exact) |
| `Error assetFont(const void* blob, uint32_t len, Font& out)` | Font blob (type 2): payload u8 w (1..8), u8 h (>=1), u8 first, u8 count (may be 0), u8 spacing, u8 nranges; nranges x {u16 first, u16 count, u16 glyph} (FontRange, used in place); then N glyphs of h bytes, one byte per row. N = (payload - 6 - 6*nranges) / h must divide exactly. Zero-copy: the blob must outlive the Font and be 2-byte aligned (else InvalidArg); any malformed field gives Corrupt |
