# Changelog

All notable changes to Pixel Game Engine are listed here. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/). Status words come from the STATUS.md feature tracker: "host only" = built and tested on the development PC, nothing run on a board; "board-checked" = the user confirmed it on the STM32F429I-DISCO; "compile-only" = it builds, nothing ran; "incomplete" and "out of scope" as in the tracker. This file states no performance numbers; measured numbers with their conditions are in `docs/benchmarks/`.

## [0.1.1] - 2026-11-17

Documentation release; no engine behaviour change. `pge_version()` returns "0.1.1".

### Added
- User help in English, German and Russian: `docs/help/en.md` (main), `docs/help/de.md`, `docs/help/ru.md`, with 4 SVG diagrams and 9 host screenshots in `docs/help/img/`.
- Step-by-step tutorial "Coin Catcher": five host targets `pge_tutorial_step1` .. `pge_tutorial_step5` in `examples/tutorial/` (host only; checked with scripted `--auto` runs, not played by keyboard).

### Fixed
- `docs/user/api-core.md` version table showed 0.0.1.

## [0.1.0] - 2026-11-17

First feature-complete preview. Version constants in `core/include/pge/version.h`, `pge_version()` returns "0.1.0".

### Core engine
- Lifecycle, fixed-timestep loop with catch-up and overrun counters, compile-time profiles SMALL / MEDIUM / LARGE (`config.h`), init and frame arenas, fixed pools, error codes, `PGE_CHECK`. Host and F429 (bench programs and examples ran on the board).
- Statistics (tick, band, flush time, pool high-water marks). Host and F429 bench runs.

### Rendering
- Band renderer with RGB565 band buffers, clipping, pixels, lines, rectangles, circles, 5x7 bitmap text, palettes. Host and F429, board-checked.
- Sprites and sprite sheets, flip, 90-degree rotation, transparency, frame animation helper. Host and F429.
- Integer output scale 1x / 2x / 3x. Implemented; the engine path with scale above 1 was only compile-checked, and its cost on the F429 is not measured.
- Tilemaps, multiple layers, camera with world clamp, parallax. Host and F429, board-checked.
- STM32F429I-DISCO display: landscape 320x240 with CPU transpose, vsync back buffer, DMA2D flush option. F429, board-checked.
- Band-skip, windowed flush and tile lighting: planned, not done. Free sprite rotation and free camera zoom: out of scope.

### World
- Chunk-based world store with LRU chunk cache, sorted change log, lazy loading, world bounds. Host only.
- Procedural terrain generator (caves, ores, walls) and interactive-tile activation hook. Host only; board timing was not measured for generation.
- Tile properties are flags only (solid, one-way, hazard, climb, activatable, breakable, transparent).

### Entities and physics
- Tile-sweep collision (TileGrid on host and F429 bench; World overload host only), entity pool with gravity and sweep, overlap queries, health and damage events, particles. Entities, health and particles: host only.
- Enemies and basic AI: planned as example-game code only; the sandbox slimes are example code.

### Storage
- Storage HAL and host file backend, two-slot atomic save with CRC-32 and a `.bak` fallback, run-length change records, settings file, resource manager and asset blob checks. Host only.
- FatFs and LittleFS HAL backends (`drivers/`), tested against own fakes on the host; the LittleFS backend was also run against real littlefs in a scratch directory (not part of the repository). No SD, USB MSC or flash block-device driver exists: incomplete.

### Audio
- Mixer with PCM8 / PCM16 effects, pattern music, SPSC command queue, host null and SDL backends. Host tested; real SDL playback was heard by the user.
- F429 SAI1 (I2S) backend: compile-only, no board run. PWM and DAC audio: incomplete, not scheduled.

### UI and debug
- Stateless UI widgets (label, bar, icon grid, list menu, menu navigation) and a debug overlay. Host only.

### Tools
- `tools/pge_assets.py` (PNG to indexed sprite sheet with RGB565 palette, CSV and Tiled JSON tilemaps, self-test) and `tools/memreport.py` (linker map memory report). Host tools; the map report was checked on an F429 build. Tested on generated samples only, not on a real Tiled export.
- The AssetId table generator is not written.

### Platforms
- Host (SDL2 window or headless null backends, keyboard and mouse input): tested.
- STM32F429I-DISCO (180 MHz, LTDC, SDRAM, buttons, KY-023 stick, STMPE811 touch): display, input and touch board-checked; benchmarks measured on the board (see `docs/benchmarks/`).
- Input: GPIO buttons, joystick, touch, unified input API: host and F429, board-checked. USB HID: out of scope.

### Tier B skeletons (untested)
- Shared stub HAL plus `cmake/tier_b.cmake` for rp2040, rp2350, stm32h7 and stm32f4_generic: core and stub compile with arm-none-eabi-g++, not linked, never run, profiles assumed. esp32: not built (no Xtensa toolchain).

### Examples
- Host: `proto`, `gfxdemo`, `scroller`, `platformer`, `audiodemo`, `sandbox`, `empty`. F429 firmware targets for scene, benchmarks, input, scroller, platformer, audio (compile-only backend) and sandbox. Platformer and scroller were played on the board; sandbox ran as a board bench, with no save medium (SAVE and LOAD show NO STORAGE).
- `examples/sandbox` uses only public `pge/*.h` headers (checked by the CTest target `sandbox_includes`).

### Documentation and build
- README, user docs in `docs/user/` (install, hardware, modules, examples, benchmarks, porting, API reference), architecture and research notes, `STATUS.md`.
- Host unit tests with CTest, no test-framework dependency.
- `THIRD_PARTY_NOTICES.md` added. MIT licence.
- Packaging: PlatformIO native env (verified), VS Code tasks (untested), STM32CubeIDE import notes (untested), third-party notices; see `docs/user/wrappers.md`.

### Known limits
- No save medium on the F429: no diskio or block device, so saving works on the host only.
- F429 SAI audio is compile-only; nothing was wired or heard on a board.
- ESP32-S3 is deferred (session S4.5, user decision 2026-11-15); `platform/esp32s3` is a placeholder. The claim that the core needs no change for a second board is not proven.
- ESP-IDF is not installed on the development machine.
- Tier B skeletons are untested and unlinked; no vendor SDK for them is installed.
- USB HID input is out of scope for v1.
- Most features are host only (see the tracker in `STATUS.md`); world size is limited by 16.16 positions to about 2,047 tiles per axis at 16 px.
- The F429 build needs your own STM32CubeF4 checkout (`PGE_CUBE_DIR`); see `THIRD_PARTY_NOTICES.md` before redistributing firmware.

## [0.0.1]

Prior prototype state (internal, before the 0.1.0 preparation); not a supported release.
