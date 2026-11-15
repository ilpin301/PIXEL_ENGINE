# Pixel Game Engine (PGE)

A small 2D pixel-game engine in C++17 for microcontrollers and the desktop.
The portable core (`pge_core`) has no platform code; hardware sits behind a thin HAL.
Targets: host (SDL2), STM32F429I-DISCO, ESP32-S3 (see `PLAN.md`).

Status: S3.0 skeleton. Only `pge_core` (version query) and one host test exist.

## Build on the host
Needs CMake >= 3.20, Ninja and a C++17 compiler (details: `docs/dev/toolchain.md`).

    cmake -S . -B build -G Ninja
    cmake --build build
    ctest --test-dir build --output-on-failure

## Where to look
- `PLAN.md` - plan, key decisions K1-K15
- `STATUS.md` - session table, resume point, decision log
- `docs/` - research, architecture, dev notes

License: MIT (see `LICENSE`).
