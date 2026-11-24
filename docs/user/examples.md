# Examples guide

> **Summary for later sessions**
> - Eight host examples in `examples/<name>/`, each a CMake target `pge_<name>` (empty, proto, gfxdemo, scroller, platformer, audiodemo, sandbox, planet; proto's target is `pge_proto_demo`), plus the Coin Catcher tutorial in `examples/tutorial/` (targets `pge_tutorial_step1` .. `pge_tutorial_step5`, walkthrough in `docs/help/`).
> - Windowed examples link `pge_platform_host` (SDL2). `pge_empty` links `pge_platform_host_null` (headless, no SDL).
> - F429 firmware exists for scroller, platformer, audiodemo and sandbox (sources in `examples/f429/`).
> - Sandbox: save/load verified on host only. On the F429 board SAVE and LOAD show NO STORAGE.
> - Planet (S8.6a): host only; the board firmware comes in S8.6b.
> - Game code in `examples/sandbox` may include only `"pge/<name>.h"`, `"game.h"`, the generated `"game_text.h"` and a few std headers (`check_includes.cmake`).
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
| planet | `pge_planet` | `--seed`, `--char`, `--core`, `--autoplay`, `--upright`, `--ticks`, `--shot`, `--load`, `--save` | none yet (S8.6b) | walk round a planet, jump, jetpack, dig |
| tutorial | `pge_tutorial_step1` .. `pge_tutorial_step5` | `--auto`, `--ticks`, `--shot` | none | move, collect coins, Enter restarts (step 5) |

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

- **Shows:** a generated tile world (32 by 4 chunks, seeded; default seed 1234), a controllable character, digging and placing tiles within a 64 px reach limit, slimes, an 8-slot inventory with a hotbar HUD, a craft menu (BRICK, FURNACE, METAL), save/load and a LANGUAGE item (English, Russian, German; the choice is kept in `settings.txt`). Menu and message texts come from `examples/sandbox/game_text.csv`. Edits live in the world's change log, so they survive chunk eviction and reload.
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
  - Select (Backspace or Right Shift) opens the menu. Up and Down choose an item. Z or Enter confirms. Left and Right pick the hotbar slot. X or Select closes the menu. The LANGUAGE item cycles English, Russian and German.
  - Esc quits.
- **Status:**
  - Save and load are verified on the host only. The language choice is kept in `settings.txt` on the host; on the F429 board the game starts in English.
  - On the F429 board the storage stubs fail, so SAVE and LOAD show **NO STORAGE**.
- **F429 firmware** (one source, `examples/f429/sandbox_main.cpp`):
  - `pge_f429_sandbox`: playable, vsync back buffer. Flash: `flash_f429_sandbox`.
  - `pge_f429_sandbench`: autoplay bench, back buffer. Flash: `flash_f429_sandbench`.
  - Chunk cache 12 slots and change log 2048 entries (`SANDBOX_CACHE`, `SANDBOX_CHANGE_CAP`).
  - The bench runs 60 warm-up ticks plus 1800 samples (about 31 s). Wait about 50 s after flashing, then read `pge_dbg_log`. The log ends with `# SANDBOX DONE`, LED4 lights, and it prints RAM figures and chunk generation times.

## planet (`pge_planet`)

- **Shows:** a round 4 by 4 chunk planet (`roundGen`, radius 52 tiles, 8 px tiles, seeded; default seed 12345) and one character that walks all the way around it. Gravity points to the planet centre in four directions and switches at the diagonals; the camera turns by 90 degrees with it behind a short black iris (10 ticks, input ignored). Walk and aim work in the screen frame, so holding right keeps going the same way around after every turn. Also jump, a jetpack without fuel, a dig mode (hold A), digging into the change log (32768 entries) and save/load. The character is a placeholder sheet, 16 x 32 px (default) or 16 x 24 px, shown a third of the way down the screen (its centre at row 80 of 240, `PlanetGame::kCharY`) instead of in the middle, so more ground shows below it. Aim and dig work on a 2 x 2 tile zone just outside the body. The centre of the planet is an unbreakable core (`kGenCore`, dark red with a glowing middle, 6 tiles = 48 px radius by default): dig and touch skip it, so a character that digs towards the centre has to dig around it and cannot reach the point where the direction to the centre swings. It is drawn along the radial axis of the planet, leaning up to 45 degrees in 9 steps of 11.25 degrees (the engine blit has only 8 orientations, so the game pre-builds rotated copies of the frames, 23 KB of RAM); collision and gravity stay 4-way, and `--upright` switches to the plain upright drawing for comparison.
- **Host target:** `pge_planet` (`build/examples/planet/pge_planet.exe`).
- **Compass:** in both views a small dial in the top-right corner has an arrow through its centre that points to the top of the planet (world up) as the picture shows it: bright tip with a "U", dim tail with a "D". In the 4-way view it turns in quarter turns with the camera, in `--rotate` it follows the view.
- **Flags:**
  - `--seed N`: world seed (default 12345).
  - `--char 24|32`: character height in px (default 32).
  - `--core N`: radius in tiles of the unbreakable core at the planet centre (default 6, 0 = none, at most 26). The F429 firmware takes the same number as the compile define `PLANET_CORE` (default the same): `cmake -DPLANET_CORE=N` for the `pge_f429_planet*` targets, to try sizes on the board.
  - `--autoplay`: run the scripted bot, which holds right and jumps when stuck.
  - `--upright`: draw the character upright (no lean along the radial axis).
  - `--rotate`: turn the whole world smoothly with the planet instead of the 90 degree snap: screen-up always points away from the planet centre, the character is upright and stays on the vertical centre line at that screen row, no iris and no input freeze (`Canvas::tilemapRotated`; `--upright` has no effect).
    The view turns at most 2 degrees per tick (walking on the surface turns about 0.2) and holds its angle within 48 px of the planet centre (`kHoldPx`), so flying about a dug-out centre does not make the picture jerk. Leaving that circle, the view swings to the new direction at 2 degrees per tick. Inside it gravity can still flip, which turns the 4-way control frame while the picture stays still, so screen Right may walk a different way there.
    Physics and controls stay 4-way, so walk direction, jump direction and gravity still kink at the diagonals; only the picture is smooth. After an 8 px step-up or a fall the character can sit a few px above or below its screen row for a few ticks (it stays on the vertical centre line).
  - `--ticks N`: quit after N ticks.
  - `--shot out.bmp`: save the window to a BMP after the loop.
  - `--load`: load the save at startup.
  - `--save PATH`: save file (default `planet.sav`).
- **Keys:**
  - Left and Right walk. Up jumps; held in the air it is the jetpack. On the F429 board the stick's up direction is Up, so it jumps and flies.
  - Hold Z (board: the first button, A) for dig mode: gravity is off and the character moves in any direction the stick points (analog, all directions on the board; the host arrows give 8 directions), at 0.75 px per tick, digging every breakable tile in its way; the unbreakable core still stops it. Release to walk again.
  - X digs the aimed 2 x 2 tiles (every breakable tile of the zone, which sits just outside the body). Up and Down or WASD aim (none = the zone in front); Up also jumps, WASD aims without jumping. The tiles of the aimed 2 x 2 zone that can be dug are drawn lighter (no outline). In `--rotate` the aim offset is turned with the view, so up and down are straight above and below the upright character on the screen.
  - A left click (host) or a touch (board) digs the 2 x 2 zone around the touched point, once per touch, at any distance (no reach limit yet). It is ignored during the 4-way iris turn.
  - Backspace (Select) switches the character between 24 and 32 px tall if there is room.
  - Enter saves. Esc quits.
- **Core size [measured, host]:** the numbers behind the default of 6 tiles. Scenario: all breakable rock dug out of a round cavity of 14 tiles around the centre (the core stays), the rotating view, 9 scripted runs of 600 ticks (right, left, jetpack and mixes from three start poses, 5,400 ticks per row, 32 px character). "Closest" is the body centre to the planet centre; "inside 48 px" counts the ticks inside the hold radius where the view stops following; "at the 2 degree limit" counts ticks over 1.9 degrees of view turn (no row ever turned more than 2.01 degrees or showed a jump over 4.1 px).

  | Core radius | Diameter | Closest | Inside 48 px | At the 2 degree limit | Gravity switches |
  |---|---|---|---|---|---|
  | 0 | none | 0.0 px | 3,965 | 768 | 442 |
  | 1 | 16 px | 17.9 px | 2,669 | 1,155 | 131 |
  | 2 | 32 px | 28.8 px | 2,283 | 1,169 | 84 |
  | 3 | 48 px | 40.0 px | 1,550 | 983 | 66 |
  | 4 | 64 px | 45.3 px | 346 | 900 | 46 |
  | 5 | 80 px | 51.2 px | 0 | 509 | 63 |
  | 6 | 96 px | 60.9 px | 0 | 169 | 27 |

  The guess of about 20 px across (radius 1) is not enough: the body centre still gets to 18 px of the centre and spends half the time inside the hold radius. The 24 px character needs the same or more (inside 48 px: 1,137 ticks at radius 4, none from radius 5). Radius 5 would be the first one that keeps the body out of the hold radius, but it leaves only 2 px of margin and traps the 32 px character in a gravity flip-flop at the core corner (the bot walks round a cavity in both directions from four start columns: at radius 1 and 5 all 8 runs flip gravity back and forth about 135 times in 1,500 ticks and never complete a lap; radii 2, 3, 4, 6, 7 and 8 go round normally, and so does the 24 px character at every radius). Radius 6 never enters the hold radius (closest 60.9 px for 32 px, 57.3 px for 24 px), has no flip-flop, and the view stays at the 2 degree limit for a few percent of the ticks (stair steps of the core, jetpack and falls; plain walking at that distance turns about 1.4 degrees per tick, computed as 1.5 px per tick over 61 px). `tests/test_planet.cpp` (`testCoreWalk`) keeps those three properties for both sizes in both views.
  A save stores only the edits, not the core size: a save made without a core (or with another core radius) digs its tiles through the new core when loaded (checked: a 20 x 20 tile cavity saved at core 0 removed all 112 core tiles of the centre block at core 6). Delete `planet.sav` before trying another size, and on the board before the first flash with a core.
- **Screenshots:** `docs/help/img/example-planet-char24.png` and `example-planet-char32.png`; `example-planet-tilt.png` shows the lean just before the first gravity switch.
- **Status:** host only. The board firmware (change log in external SDRAM, bench) comes in S8.6b.

## tutorial (`pge_tutorial_step1` .. `pge_tutorial_step5`)

- **Shows:** the Coin Catcher game built up in five steps, one complete `examples/tutorial/stepN/game.h` per step. All steps share `examples/tutorial/main.cpp`. The step-by-step walkthrough is in `docs/help/`.
- **Host targets:** `pge_tutorial_step1` .. `pge_tutorial_step5` (`build/examples/tutorial/`).
- **Flags:** `--auto` (scripted input), `--ticks N`, `--shot out.bmp`.
- **Keys:** arrows or WASD move, Enter (Start) restarts after game over in step 5, Esc quits.
- **F429:** none.

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
5. **Keep includes public.** Game code includes only `"pge/<name>.h"` headers that exist in `core/include/pge/`, plus `"game.h"` for the game's own header (and `"game_text.h"` for its generated texts), plus `<stdint.h>`, `<string.h>`, `<cstdio>`, `<cstdlib>` and `<cstring>`. `examples/sandbox/check_includes.cmake` enforces this for the files in `examples/sandbox/` only:

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
