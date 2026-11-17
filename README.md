# Pixel Game Engine (PGE)

PGE is a small 2D pixel-game engine for microcontrollers and the desktop. The portable C++17 core (`pge_core`) contains no platform code. Each board is reached through a thin per-platform hardware abstraction layer (HAL). Rendering uses band rendering: the scene is drawn in small horizontal bands and flushed to the display, so the frame never has to fit in RAM. After initialization the engine makes no heap allocations; arenas and fixed pools are sized at compile time.

## Status

- Measured Tier A targets: the host (SDL2) and the STM32F429I-DISCO board.
- ESP32-S3 is deferred (session S4.5). Its I2S audio is deferred with it.
- Other boards (RP2040/RP2350, STM32H7, ESP32 classic, generic STM32F4) are untested HAL skeletons, planned in S7.2a. They are not verified.

Known limits (from the feature tracker in `STATUS.md`):

- World, entity, audio and storage code is host-only in most parts. The F429 audio backend (SAI) is compile-only, with no board run.
- No save media on the F429: there is no SD, USB MSC or flash block-device driver yet, so SAVE and LOAD report no storage. FatFs and LittleFS backends are tested on fakes only (LittleFS also on the real `lfs.c` in scratch).
- Enemies and basic AI, lighting, band-skip and windowed flush are planned, not implemented.
- PWM and DAC audio are not scheduled.
- Out of scope: USB HID host input, free camera zoom (integer 1x, 2x, 3x scale only), free sprite rotation (90-degree steps only), 3D, networking.

## Quick start (host build)

You need CMake 3.20 or newer, Ninja and a C++17 compiler. Toolchain details are in `docs/dev/toolchain.md`.

```
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

For installing the tools and building for the board, see `docs/user/install.md`.

## Documentation

User guide:

- `docs/help/en.md` - main user help with a step-by-step tutorial
- `docs/user/install.md` - installing and building
- `docs/user/hardware.md` - supported boards and wiring
- `docs/user/modules.md` - engine modules
- `docs/user/examples.md` - example programs
- `docs/user/benchmarks.md` - benchmark results
- `docs/user/api-core.md` - core API
- `docs/user/api-gfx.md` - graphics API
- `docs/user/api-world.md` - world, entity and storage API
- `docs/user/api-audio-storage.md` - audio and storage API
- `docs/user/api-hal.md` - HAL API
- `docs/user/porting.md` - porting guide
- `platform/tier_b/README.md` - Tier B skeletons: compile-checked, untested
- `docs/user/wrappers.md` - PlatformIO, STM32CubeIDE, VS Code and the release checklist
- `CHANGELOG.md` - release notes
- `THIRD_PARTY_NOTICES.md` - third-party licences and what is bundled

Architecture:

- `docs/architecture/01-overview.md` - overview
- `docs/architecture/02-hal-render-game-api.md` - HAL, rendering and game API
- `docs/architecture/03-world-entity-storage-api.md` - world, entity and storage API

Development:

- `docs/dev/toolchain.md` - toolchain setup

Project:

- `STATUS.md` - session table, resume point, decision log and feature tracker

## License

MIT. See `LICENSE`.
