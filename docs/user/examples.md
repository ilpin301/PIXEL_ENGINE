# Examples guide

> **Summary for later sessions**
> - Seven host examples in `examples/<name>/`, each a CMake target `pge_<name>` (empty, proto, gfxdemo, scroller, platformer, audiodemo, sandbox; proto's target is `pge_proto_demo`).
> - Windowed examples link `pge_platform_host` (SDL2). `pge_empty` links `pge_platform_host_null` (headless, no SDL).
> - F429 firmware exists for scroller, platformer, audiodemo and sandbox (sources in `examples/f429/`).
> - Sandbox: save/load verified on host only. On the F429 board SAVE and LOAD show NO STORAGE.
> - Game code in `examples/sandbox` may include only `"pge/<name>.h"`, `"game.h"` and a few std headers (`check_includes.cmake`).
> - Keys: arrows or WASD (Up/W, Down/S, Left/A, Right/D), Z/J = A, X/K = B, Enter = Start, Backspace or Right Shift = Select, Esc quits.

## Before you start

**Host build** (from `docs/dev/toolchain.md`, PowerShell form, CLion toolchain):

```powershell
$env:PATH = "C:\Program Files\JetBrains\CLion\bin\cmake\win\x64\bin;C:\Program Files\JetBrains\CLion\bin\ninja\win\x64;C:\Program Files\JetBrains\CLion\bin\mingw\bin;$env:PATH"
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER="C:/Program Files/JetBrains/CLion/bin/mingw/bin/g++.exe"
cmake --build build
```

- Executables land in `build/examples/<name>/`. Example: `build/examples/scroller/pge_scroller.exe`.
- The `pge_platform_host` examples need SDL2. The `SDL2::SDL2` library is copied next to each exe after build. Without SDL2 the host window demo is skipped (toolchain.md).
- `mingw/bin` must be on PATH when running the exes (runtime DLLs).

**F429 firmware** (cross build, arm-none-eabi 10.3.1; needs STM32CubeF4 via `PGE_CUBE_DIR`):

```powershell
cmake -S . -B build-f429 -G Ninja -DPGE_PLATFORM=stm32f429i_disco "-DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build-f429 --target <target>
cmake --build build-f429 --target flash_<target-without-pge_-prefix>
```

The flash targets use openocd and ST-LINK and need the board attached. Firmware files are written to `build-f429/platform/stm32f429i_disco/`. Example: `cmake --build build-f429 --target flash_f429_platformer` builds and flashes `pge_f429_platformer`. Logs go to the debug ring buffer `pge_dbg_log` (read through OpenOCD memory reads, no UART).

## Overview

| Example | Host target | Host flags | F429 firmware | Controls in short |
|---------|-------------|------------|---------------|-------------------|
| empty | `pge_empty` | `--seconds N` | none | none (headless) |
| proto | `pge_proto_demo` | `--frames`, `--shot`, `--band`, `--bus-hz` | none | move box, light buttons |
| gfxdemo | `pge_gfxdemo` | `--scale`, `--frames`, `--shot`, `--band` | none | none listed |
| scroller | `pge_scroller` | `--ticks`, `--shot` | `pge_f429_scroller` | scroll, Z faster, click/touch pulls camera |
| platformer | `pge_platformer` | `--auto`, `--overlay`, `--ticks`, `--shot` | `pge_f429_platformer`, `pge_f429_platbench`, `pge_f429_platbench_nobb` | move, Z or Up jumps, Select overlay |
| audiodemo | `pge_audiodemo` | `--seconds`, `--bench` | `pge_f429_audio` | none (plays on start) |
| sandbox | `pge_sandbox` | `--overlay`, `--seed`, `--ticks`, `--shot`, `--load`, `--save` | `pge_f429_sandbox`, `pge_f429_sandbench` | dig, place, craft menu |

The host exe path for `empty`, `proto`, `gfxdemo` and `sandbox` follows the same `build/examples/<name>/` layout as the examples toolchain.md names explicitly.

## empty

- **Shows:** the smallest engine loop with an empty game. Each tick it allocates and frees a few pool handles, and prints timing and pool stats (`tick_us`, `frame_us`, `fps`, `pool_high`, and so on). Headless: no window, no SDL.
- **Host target:** `pge_empty` (links `pge_core` and `pge_platform_host_null`).
- **Flags:** `--seconds N` (from the source header).
- **Keys:** none.
- **F429:** none.

## proto (`pge_proto_demo`)

- **Shows:** a box you move with the keyboard, drawn over a scene from `scene.h`. A row of eight indicators at the top shows UP, DOWN, LEFT, RIGHT, A, B, START and SELECT. The frame is rendered in horizontal bands, one call per band.
- **Host target:** `pge_proto_demo` (links `pge_core` and `pge_platform_host`).
- **Flags:** `--frames N` (0 = run until the window closes), `--shot out.bmp` (save the window to a BMP), `--band L` (band height), `--bus-hz H` (bus speed).
- **Keys:** arrows or WASD move the box. Z, X, Enter and Backspace light the A, B, START and SELECT indicators. Esc quits.
- **F429:** none. The board target `pge_f429_scene` is a separate tile and sprite scene (S3.2a), not this example.

## gfxdemo (`pge_gfxdemo`)

- **Shows:** drawing primitives and text on a logical screen of 320/S by 240/S pixels. It draws a row of 16 palette swatches, a rectangle that walks along them, pixel rows, and a 4-frame animated 8x8 sprite (a right-pointing arrow with a growing wing). It calls `renderFrame` directly, not the Engine.
- **Host target:** `pge_gfxdemo` (links `pge_core` and `pge_platform_host`).
- **Flags:** `--scale S` (1 to 3, default 2), `--frames N` (0 = until the window closes), `--shot out.bmp`, `--band L`.
- **Keys:** none in the source header.
- **F429:** none.

## scroller (`pge_scroller`)

- **Shows:** two parallax tile layers over a sky. Far clouds scroll at half the x speed and a quarter of the y speed; near ground and platforms scroll at 1:1. The camera is clamped to the world and a HUD shows the position. The game runs through `Engine` and `run<G>`.
- **Host target:** `pge_scroller` (`build/examples/scroller/pge_scroller.exe`).
- **Flags:** `--ticks N` (quit after N ticks), `--shot out.bmp` (save the window to a BMP after the loop).
- **Keys:**
  - Arrows or WASD scroll the camera.
  - Hold Z to scroll faster (step 4 instead of 2 per tick).
  - Left mouse button pulls the camera to the clicked point.
- **F429:** `pge_f429_scroller`, flash with `flash_f429_scroller` (board needed). On the board the buttons and the KY-023 stick scroll, and touch pulls the camera to the touched point.

## platformer (`pge_platformer`)

- **Shows:** tile collision with game-side 8.8 fixed-point physics, coyote time, a jump buffer, variable jump height, one-way platforms, spikes, two parallax map layers, a HUD and the debug overlay.
- **Host target:** `pge_platformer` (`build/examples/platformer/pge_platformer.exe`).
- **Flags:**
  - `--auto`: run the scripted bot instead of keyboard input.
  - `--overlay`: start with the debug overlay on.
  - `--ticks N`: quit after N ticks.
  - `--shot out.bmp`: save the window to a BMP after the loop.
- **Keys:**
  - Arrows or WASD move.
  - Z or Up jumps.
  - Select (Backspace or Right Shift) toggles the debug overlay.
- **Headless screenshot** (toolchain.md, PowerShell form):

  ```powershell
  $env:SDL_VIDEODRIVER='dummy'; $env:SDL_RENDER_DRIVER='software'
  build/examples/platformer/pge_platformer.exe --auto --overlay --ticks 120 --shot out.bmp
  ```

- **F429 firmware** (one source, `examples/f429/platformer_main.cpp`):
  - `pge_f429_platformer`: playable, vsync back buffer. Flash: `flash_f429_platformer`.
  - `pge_f429_platbench`: autoplay bench, back buffer. Flash: `flash_f429_platbench`.
  - `pge_f429_platbench_nobb`: autoplay bench, no vsync. Flash: `flash_f429_platbench_nobb`.
  - The bench runs 60 warm-up ticks plus 1800 samples (about 31 s). Wait about 50 s after flashing, then read `pge_dbg_log`. The log ends with `# PLAT DONE` and LED4 lights. Results: `docs/benchmarks/stage4.md`.

## audiodemo (`pge_audiodemo`)

- **Shows:** a 2-channel pattern (bass on channel 0, arpeggio lead on channel 1) plus one effect every 700 ms, at 22050 Hz. All sounds are synthesized at start-up; there are no asset files.
- **Host target:** `pge_audiodemo` (`build/examples/audiodemo/pge_audiodemo.exe`). Needs SDL.
- **Flags:**
  - `--seconds N`: play for N seconds (default 6).
  - `--bench`: no playback. Drives `Mixer::fill` directly and prints the mixing cost per voice-sample.
- **Keys:** none. It plays as soon as it starts.
- **Headless smoke test** (toolchain.md, PowerShell form):

  ```powershell
  $env:SDL_VIDEODRIVER='dummy'; $env:SDL_AUDIODRIVER='dummy'
  build/examples/audiodemo/pge_audiodemo.exe
  ```

- **F429:** `pge_f429_audio`, flash with `flash_f429_audio`. The board needs a MAX98357A on PE4 (LRC/FS), PE5 (BCLK) and PE6 (DIN). The source runs a mixer bench first, then 20 s of SAI1 playback. Bench lines `AUDIO ...` and `# AUDIO DONE` go to `pge_dbg_log`; LED4 lights at the end. Flashing replaces the firmware on the board.

## sandbox (`pge_sandbox`)

- **Shows:** a generated tile world (32 by 4 chunks, seeded; default seed 1234), a controllable character, digging and placing tiles within a 64 px reach limit, slimes, an 8-slot inventory with a hotbar HUD, a craft menu (BRICK, FURNACE, METAL) and save/load. Edits live in the world's change log, so they survive chunk eviction and reload.
- **Host target:** `pge_sandbox` (`build/examples/sandbox/pge_sandbox.exe`).
- **Flags:**
  - `--overlay`: start with the debug overlay on (also a menu item).
  - `--seed N`: world seed (default 1234).
  - `--ticks N`: quit after N ticks.
  - `--shot out.bmp`: save the window to a BMP after the loop.
  - `--load`: load the save at startup.
  - `--save PATH`: save file (default `sandbox.sav`).
- **Keys:**
  - Arrows move. Z or Up jumps.
  - WASD aims. With no aim key held, the aim is the tile in front.
  - X digs the aimed tile into the inventory. Down aims below, Up aims above the head. Up without X also jumps.
  - Enter places the selected hotbar tile at the aimed spot.
  - Left mouse button digs the tile under the pointer (reach 64 px), or selects a hotbar slot if you click a hotbar cell.
  - Select (Backspace or Right Shift) opens the menu. Up and Down choose an item. Z or Enter confirms. Left and Right pick the hotbar slot. X or Select closes the menu.
  - Esc quits.
- **Status:**
  - Save and load are verified on the host only.
  - On the F429 board the storage stubs fail, so SAVE and LOAD show **NO STORAGE**.
- **F429 firmware** (one source, `examples/f429/sandbox_main.cpp`):
  - `pge_f429_sandbox`: playable, vsync back buffer. Flash: `flash_f429_sandbox`.
  - `pge_f429_sandbench`: autoplay bench, back buffer. Flash: `flash_f429_sandbench`.
  - Chunk cache 12 slots and change log 2048 entries (`SANDBOX_CACHE`, `SANDBOX_CHANGE_CAP`).
  - The bench runs 60 warm-up ticks plus 1800 samples (about 31 s). Wait about 50 s after flashing, then read `pge_dbg_log`. The log ends with `# SANDBOX DONE`, LED4 lights, and it prints RAM figures and chunk generation times.

## Starting your own game

Start from `examples/empty`, the smallest complete example.

1. **Copy the folder.** Copy `examples/empty/` to `examples/<yourgame>/`, keeping `main.cpp` and `CMakeLists.txt`.
2. **Rename the target.** In the copied `CMakeLists.txt`, change `pge_empty` to your own target name (for example `pge_yourgame`). Register the new folder the same way the existing examples are registered.
3. **Write the game class.** `empty/main.cpp` shows the pattern:
   - `onCreate(pge::Engine& e)` stores the engine pointer, calls `e.trackPool(...)` if you need pool counters, and returns `pge::Ok`.
   - `onTick(const pge::Input&)` runs once per tick. Call `eng_->requestQuit()` to end the loop.
   - `onRender(pge::Canvas&)` runs once per band.
   - `eng_->stats()` gives the timing and pool counters.
4. **Pick the platform.**
   - `pge_platform_host_null` for a headless build with no SDL, as `pge_empty` does.
   - `pge_platform_host` for a window (SDL2), as the other host examples do.
5. **Keep includes public.** Game code includes only `"pge/<name>.h"` headers that exist in `core/include/pge/`, plus `"game.h"` for the game's own header, plus `<stdint.h>`, `<string.h>`, `<cstdio>`, `<cstdlib>` and `<cstring>`. `examples/sandbox/check_includes.cmake` enforces this for the files in `examples/sandbox/` only:

   ```powershell
   cmake -DPGE_ROOT=F:/____IL_CPP/PIXEL_ENGINE -P examples/sandbox/check_includes.cmake
   ```

   The script prints the number of includes checked and fails on any other include. To apply the same check to your own folder, change the glob path in the script.
6. **Build and run.**

   ```powershell
   cmake --build build --target pge_yourgame
   build/examples/yourgame/pge_yourgame.exe
   ```

For a game with a window and keyboard input, read `examples/scroller/game.h` and `examples/platformer/game.h`. Both use the same `onCreate`, `onTick` and `onRender` pattern. They also include `pge/*.h` headers only, so a firmware `main` can reuse them.
