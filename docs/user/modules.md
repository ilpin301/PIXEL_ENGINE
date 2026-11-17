# Pixel Engine module guide

> **Summary for later sessions**
> - 18 spec modules in 6 layers (L0 hardware to L4 game systems). Dependencies point to the same or a lower layer only.
> - Engine code reaches hardware only through the C HAL headers in `hal/include/pge/hal/`. One platform per binary, bound at link time.
> - Three compile-time capability profiles, SMALL / MEDIUM / LARGE, set in `config.h`.
> - Most features are host-only; board checks are noted per row in section 4.
> - RAM totals are calculated from assumed budgets (see 01-overview section 5.3). No flash figure exists yet.
> - Status table in section 4 is copied from the STATUS.md Feature tracker, caveats included.

Module map source: [01-overview section 2](../architecture/01-overview.md#2-module-map-all-18-spec-modules). Headers below are the files in `core/include/pge/` unless marked otherwise.

## 1. Modules

Layer tags follow 01-overview: L0 hardware side, L1 base, L2 graphics, L3 services, L4 game systems. "Optional" means the profile can switch the module off.

### HAL (L0)
- Does: C-compatible headers for display, input, time, audio, storage and debug. The only path from engine code to hardware.
- Headers: `hal/include/pge/hal/` (display.h, input.h, time.h, audio.h, storage.h, debug.h). Not in `core/include/pge/`.
- Doc: [api-hal.md](api-hal.md)

### Platform and Display (L0)
- Does: board implementation of the HAL, startup, band buffer placement (Platform). Controller drivers such as ILI9341 and LTDC glue (Display). Code lives in `platform/` and `drivers/`.
- Headers: none in `core/include/pge/`. Board-side code.
- Doc: [api-hal.md](api-hal.md)

### Core (L1)
- Does: lifecycle, fixed-timestep loop, configuration, memory arenas and fixed pools. Integer and fixed-point maths only; no `float` in the core API.
- Headers: `engine.h`, `timestep.h`, `config.h`, `memory.h`, `error.h`, `version.h`.
- Doc: [api-core.md](api-core.md)

### Debug (L1)
- Does: counters (frame, band and flush time, pool high-water mark), log output, overlay data.
- Headers: `debug.h`.
- Doc: [api-core.md](api-core.md)

### Graphics (L2)
- Does: colour, clipping, primitives, bitmap text.
- Headers: `gfx.h`, `color.h`.
- Doc: [api-gfx.md](api-gfx.md)

### Renderer (L2)
- Does: band lifecycle, layers, flush, integer scale. One band renderer: the game render callback runs once per band under a clip rectangle. Band buffer is RGB565.
- Headers: no dedicated header in the list above.
- Doc: [api-gfx.md](api-gfx.md)

### Sprite (L2)
- Does: sprite sheets, flip, transparency, frame animation.
- Headers: `sprite.h`.
- Doc: [api-gfx.md](api-gfx.md)

### Tilemap (L2)
- Does: multi-layer tile drawing from a tile source.
- Headers: no dedicated header in the list above.
- Doc: [api-world.md](api-world.md)

### Camera (L2)
- Does: position, world clamp, parallax factor, integer scale.
- Headers: `camera.h`.
- Doc: [api-gfx.md](api-gfx.md)

### Input (L3)
- Does: unified buttons, axes, touch point, mapping table.
- Headers: none in `core/include/pge/`. The HAL side is `hal/include/pge/hal/input.h`.
- Doc: [api-hal.md](api-hal.md)

### Audio (L3, optional)
- Does: mixer, PCM effects, pattern music. Channel count is set by the profile (section 3).
- Headers: `audio.h`.
- Doc: [api-audio-storage.md](api-audio-storage.md)

### Storage (L3, optional)
- Does: file-like interface, save format, configuration file.
- Headers: `storage.h`, `save.h`, `settings.h`.
- Doc: [api-audio-storage.md](api-audio-storage.md)

### Resource Manager (L3)
- Does: assets from flash or storage through one interface.
- Headers: `resource.h`, `asset.h`.
- Doc: [api-audio-storage.md](api-audio-storage.md)

### World (L4, optional)
- Does: chunk store, LRU chunk cache, generation hook, change lists.
- Headers: `world.h`, `worldgen.h`.
- Doc: [api-world.md](api-world.md)

### Entity (L4, optional)
- Does: fixed pools, parallel arrays, spawn and despawn.
- Headers: `entity.h`.
- Doc: [api-world.md](api-world.md)

### Physics (L4, optional)
- Does: tile sweep collision, gravity step, overlap queries.
- Headers: `collision.h`.
- Doc: [api-world.md](api-world.md)

### UI (L4, optional)
- Does: label, bar, icon grid, list menu. Widgets are stateless.
- Headers: `ui.h`.
- Doc: [api-gfx.md](api-gfx.md)

### Particles (listed for completeness)
- Does: particle effects. 01-overview lists no separate module for it.
- Headers: `particles.h`.
- Doc: [api-world.md](api-world.md)

## 2. Dependency rules

- **Hardware isolation.** `core/` and `hal/` include no vendor or platform header. Hardware code lives only in `platform/` and `drivers/`.
- **Layers.** A module may use modules in its own layer or lower ones. No cycles. L4 may use L2 and L3, never the reverse.
- **One platform per binary.** The platform is chosen at link time. There is no runtime backend switch and no virtual call in hot paths.
- **Optional modules.** An optional module can be compiled out. Core modules never call an optional one; they expose a hook (function pointer set at init) instead.
- **No heap after init.** Nothing is allocated after `pge::init()` returns. No exceptions, no RTTI, no iostream, no allocating STL.
- **Game code.** Game code sits above L4 and uses public headers only. `examples/sandbox` is checked by a CTest target (`sandbox_includes`).
- **Known exception.** Sprite and Tilemap read asset data through a pointer plus size, so they depend only on `asset.h`, not on the Resource Manager implementation. Tagged [assumed, checked in S2.2] in 01-overview.

Memory model in short: an init arena (never freed), a frame arena (reset each tick), and fixed pools with an error value on exhaustion. Sizes come from the profile.

## 3. Capability profiles

Exactly one of `PGE_PROFILE_SMALL`, `PGE_PROFILE_MEDIUM`, `PGE_PROFILE_LARGE` is set to 1, in `pge_profile.h` or the optional `pge_user_config.h`. `config.h` enforces this with a static assert.

Values below come from `config.h`:

| Switch | SMALL | MEDIUM | LARGE |
|---|---|---|---|
| Logical screen | 160x128 | 320x240 | 320x240 |
| Band lines | 8 | 16 | 32 |
| Integer scale max | 1 | 2 | 3 |
| Parallax layers | 1 | 2 | 4 |
| Chunk cache count | 4 | 16 | 64 |
| Audio channels | 0 (audio off) | 4 | 8 |
| Audio command queue | 8 | 16 | 32 |
| Particles | 32 | 128 | 512 |
| Entities | 64 | 256 | 1,024 |
| Storage I/O buffer (bytes) | 512 | 4,096 | 16,384 |
| Config keys | 8 | 16 | 32 |
| Lighting | off | off | off (optional, disabled in config.h) |

Notes:
- Band lines for LARGE differ between sources: 01-overview section 6 says 16, while `config.h` sets 32 (its comment says G3 accepted 32). Check `config.h` for the value in force.
- Lighting is planned only (section 4). LARGE can enable it only after a measurement in S5.6, which has not been done.
- A switched-off feature removes its code and memory from the link.

Profile mapping per board [assumed, from 01-overview section 6]:
- Host: LARGE. It can be built as MEDIUM or SMALL to test the low-RAM path.
- STM32F429I-DISCO: LARGE.
- ESP32-S3 N16R8: LARGE, band buffers in internal RAM.
- ESP32 classic and RP2350 (Tier B, untested): MEDIUM.
- RP2040 and small STM32F4 parts: SMALL or MEDIUM.

RAM totals [calculated] from per-module budgets that are [assumed], per [01-overview section 5.3](../architecture/01-overview.md#53-per-module-ram-budget-by-profile-bytes). Scale staging at s > 1 is not included.
- SMALL: 16,512 B
- MEDIUM: 77,696 B
- LARGE: 90,496 B internal, plus 284,672 B external

## 4. Status per module

Status values come from the STATUS.md Feature tracker: implemented, incomplete, planned, out of scope. The "Where it runs" column carries the tracker's host and board notes. Research rows are omitted.

| Module / feature | Status | Where it runs and caveats |
|---|---|---|
| Graphics: primitives, text, sprites, sprite sheets, animated and transparent sprites, palette, clipping, flip, 90-degree rotation | implemented | Tracker gives no board note. |
| Free sprite rotation | out of scope | - |
| Free camera zoom | out of scope | - |
| Camera integer scale 1x/2x/3x (K15) | implemented | Tracker gives no board note. |
| Band renderer and DMA2D flush | implemented | Prototype, measured in S3.2b and S3.4. |
| Band-skip and windowed flush | planned | Not done (S5.6, optional). |
| Tilemaps, multiple layers, camera movement, parallax, world boundaries, camera-based rendering | implemented | Host and F429, board-checked. |
| Lighting and environment effects | planned | Optional; not done in S5.6, no cost measured. |
| Menus, HUD, UI (widgets, MenuNav, debug overlay) | implemented | Host only. |
| Input: GPIO buttons, joysticks, gamepads and keypads, touchscreens, unified input API | implemented | Host and F429, board-checked. |
| USB HID devices | out of scope | Out of scope for v1 (future port item). |
| Sound effects and music (S5.5, mixer, PCM8/PCM16, pattern music, 8 channels) | implemented | Host tested; real SDL playback heard by the user 2026-11-16. F429 SAI backend is compile-only. |
| I2S audio | incomplete | F429 SAI1 backend written, compile-only, no board run. ESP32-S3 I2S deferred with S4.5. |
| PWM and DAC audio | incomplete | Not scheduled. |
| Collision: tile collision, collision maps (TileGrid) | implemented | Tile collision: host and F429 bench. |
| Entity sweep and overlap query | implemented | Host only. |
| Physics and gravity | implemented | Example controller (S4.4). Engine gravity and sweep: host only. |
| Character movement and animation | implemented | Example (S4.4). Engine entities and animation state: host only. |
| Entity management, object pooling | implemented | Host only. |
| Interactive objects | implemented | Host only (tile hook and entities). |
| Interactive tiles (activation hook) | implemented | Host only. |
| Health and damage | implemented | Host only. |
| Particle effects | implemented | Host only. |
| Enemies and basic AI | planned | Example-game code only; pools come from S5.2. |
| Inventory | implemented | Host and F429 bench run (S6.2b). No board save. G6 passed. |
| Items and crafting | implemented | Host and F429 bench run (S6.2b). No board save. Crafting is an example recipe table. |
| Tile properties and metadata | implemented (flags only) | Flags implemented (S4.4). Hardness and light used from S5.x. |
| Chunk-based world storage, chunk loading and cache | implemented | Host only. |
| Large scrolling worlds (store, cache, generator) | implemented | Host only. |
| Procedural world generation and terrain | implemented | Host only. Board timing is S6.2. |
| Compressed world storage (run-length) | implemented | Host only. Embedded FatFs and LittleFS backends run on fakes. On-board: S6.2. |
| External storage for worlds | incomplete | FatFs and LittleFS HAL backends written and host-tested on fakes. No diskio or block-device driver. No board check. |
| Internal flash, external SPI flash, SD card, LittleFS | incomplete | No SD, USB MSC or flash block-device drivers. LittleFS backend also run on real lfs.c over a RAM device in scratch. |
| Save games | implemented | Host only. Board save media not done. |
| Configuration files | implemented | Host only. |
| Asset loading, unified resource interface | implemented | Host only. Asset converters and blob checks (S5.4) are host-side. |
| Asset tools (PNG to indexed sprite sheet, RGB565 palette, CSV and Tiled tilemap, linker map report) | implemented | Host tools. Map report checked on the F429 platformer build. |
| Capability-based feature switching (K14) | implemented | `config.h` profile constants. |
| Game code separated from engine | implemented | `examples/sandbox` includes only `pge/*.h`, checked by CTest `sandbox_includes`. |

Open notes carried from STATUS.md:
- Audio board check (S5.5) and board save media are not done.
- Tiled CSV empty-cell convention (0 or -1) is unconfirmed against a real export.
- AssetId enum and AssetEntry table generator are not written; decision deferred to S6.1.
- Lighting, band-skip and windowed flush are not done (optional, S5.6).
