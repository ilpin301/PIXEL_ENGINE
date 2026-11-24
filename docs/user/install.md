# Pixel Game Engine: Install and Build Guide

> **Summary for later sessions**
> - Host build: CMake >= 3.20, Ninja, C++17 compiler. SDL2 2.x is optional; without it the window demos are skipped and the tests still build.
> - Windows dev machine: CMake, Ninja and g++ come from the CLion install and are not on PATH. The commands in section 1.5 are the verified ones.
> - ARM: arm-none-eabi-gcc 10.3.1 and OpenOCD 0.12.0. Set `PGE_PLATFORM=stm32f429i_disco`. STM32CubeF4 is not vendored; point `PGE_CUBE_DIR` at it.
> - ESP-IDF: not implemented (deferred, session S4.5).
> - Asset tools need Python 3 and Pillow.
> - Tests: `ctest --test-dir build --output-on-failure`. Names are listed in section 5.

---

## 1. Host build

### 1.1 Requirements

- CMake 3.20 or newer (the root `CMakeLists.txt` declares `cmake_minimum_required(VERSION 3.20)`).
- Ninja (the examples use `-G Ninja`).
- A C++17 compiler. The project sets `CMAKE_CXX_STANDARD 17` with extensions off.
- Optional: SDL2 2.x development package (CMake config package).

### 1.2 Configure, build, test (generic)

```bash
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

Use `-DCMAKE_CXX_COMPILER=<path to your C++17 compiler>` if CMake does not pick the right one.

### 1.3 How SDL2 is found

The root `CMakeLists.txt` calls `find_package(SDL2 CONFIG QUIET)`. To point CMake at an SDL2 install, pass its prefix:

```bash
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=<SDL2 prefix>
```

### 1.4 What is skipped without SDL2

If SDL2 is not found, CMake prints `SDL2 not found: host window demo skipped` and does not add these directories:

- `examples/proto`, `examples/gfxdemo`, `examples/scroller`, `examples/platformer`, `examples/sandbox`, `examples/audiodemo`

Still built without SDL2: `platform/host`, `examples/empty`, `bench/b1`, and the tests.

### 1.5 Windows dev machine example (PowerShell)

Paths below are the ones recorded in `docs/dev/toolchain.md` (checked 2026-11-15). The CLion-bundled cmake, ninja and g++ are not on the global PATH, so add them for the session:

```powershell
$env:PATH = "C:/Program Files/JetBrains/CLion/bin/cmake/win/x64/bin;C:/Program Files/JetBrains/CLion/bin/ninja/win/x64;C:/Program Files/JetBrains/CLion/bin/mingw/bin;$env:PATH"
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER="C:/Program Files/JetBrains/CLion/bin/mingw/bin/g++.exe"
cmake --build build
ctest --test-dir build --output-on-failure
```

`mingw/bin` must stay on PATH when the tests run, because the executables need its runtime DLLs.

With the SDL2 development package (version 2.32.10 on the dev machine):

```powershell
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER="C:/Program Files/JetBrains/CLion/bin/mingw/bin/g++.exe" -DCMAKE_PREFIX_PATH=C:/Users/<user>/sdk/SDL2-2.32.10/x86_64-w64-mingw32
```

The post-build step copies `SDL2.dll` next to the demo executable.

Run the window demo (no `--frames`; runs until the window is closed):

```powershell
build/examples/proto/pge_proto_demo.exe
```

### 1.6 Release benchmark build (separate directory)

The B1 host benchmark is built in its own Release directory with tests off, so the asserts stay on only in the test build:

```bash
cmake -S . -B build-rel -G Ninja -DCMAKE_BUILD_TYPE=Release -DPGE_BUILD_TESTS=OFF
cmake --build build-rel
build-rel/bench/b1/pge_b1_host.exe --frames 200
```

Optional flags: `--bands 8,16,32,240`, and for a slow-bus simulation `--bus-hz 40000000 --bus-bits 1`.

Output goes to `docs/benchmarks/raw/host.txt` and is tagged `host`, so it is not representative of the target hardware.

### 1.7 Host options

| Option | Default | Meaning |
|---|---|---|
| `PGE_PLATFORM` | `host` | `host` or `stm32f429i_disco` (see section 2) |
| `PGE_BUILD_TESTS` | `ON` (OFF under add_subdirectory) | Build the host unit tests (`tests/`) |
| `PGE_BUILD_EXAMPLES` | `ON` (OFF under add_subdirectory) | Build the examples, benches and, on the F429, the example firmwares |
| `CMAKE_BUILD_TYPE` | (none) | Use `Release` for benchmarks and firmware |

---

## 2. ARM / STM32F429I-DISCO

### 2.1 Requirements

- `arm-none-eabi-gcc` and `arm-none-eabi-g++`: version 10.3.1 (GNU Arm Embedded Toolchain 10.3-2021.10) was verified on the dev machine. Newer code should be re-checked against this version.
- `arm-none-eabi-objcopy` and `arm-none-eabi-size` (used by the CMake toolchain file).
- OpenOCD 0.12.0 with an ST-LINK probe (`interface/stlink.cfg`, `transport select hla_swd`, `target/stm32f4x.cfg`). Only needed for the flash targets.
- STM32CubeF4 (see 2.2). It is not included in this repository.

The toolchain file `cmake/arm-none-eabi.cmake` sets the Cortex-M4F flags (`-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard`), `--specs=nano.specs --specs=nosys.specs`, and C++ without exceptions and RTTI. For the firmware, the root `CMakeLists.txt` defaults `CMAKE_BUILD_TYPE` to `Release`.

### 2.2 STM32CubeF4 (`PGE_CUBE_DIR`)

STM32CubeF4 is not vendored. The cache variable `PGE_CUBE_DIR` points at a checkout. Its default is `F:/____IL_CPP/STM32F429I-DISCO/STM32CubeF4`, and it can be overridden on the configure line.

Configure fails with `PGE_CUBE_DIR does not point at STM32CubeF4` if `Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal.c` is missing under that path.

### 2.3 Configure, build, flash (generic)

```bash
cmake -S . -B build-f429 -G Ninja -DPGE_PLATFORM=stm32f429i_disco -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake -DCMAKE_BUILD_TYPE=Release -DPGE_CUBE_DIR=<path to STM32CubeF4>
cmake --build build-f429
cmake --build build-f429 --target flash_f429
```

The build writes `pge_f429_scene.elf`, `.bin`, `.hex` and `.map` into `build-f429/platform/stm32f429i_disco/`. The `flash_<name>` targets run OpenOCD with ST-LINK and need the board connected. Flashing replaces the firmware currently on the board.

### 2.4 Windows dev machine example (PowerShell)

```powershell
$env:PATH = "C:/Program Files/JetBrains/CLion/bin/cmake/win/x64/bin;C:/Program Files/JetBrains/CLion/bin/ninja/win/x64;$env:PATH"
cmake -S . -B build-f429 -G Ninja -DPGE_PLATFORM=stm32f429i_disco "-DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake" -DCMAKE_BUILD_TYPE=Release -DPGE_CUBE_DIR="F:/____IL_CPP/STM32F429I-DISCO/STM32CubeF4"
cmake --build build-f429
cmake --build build-f429 --target flash_f429
```

Quote `"-DCMAKE_TOOLCHAIN_FILE=..."` in PowerShell: unquoted, it is split at the dot and the configure fails. `arm-none-eabi-gcc` and `arm-none-eabi-g++` must be on PATH too (on the dev machine they are in `C:/ProgramData/chocolatey/bin`, see `docs/dev/toolchain.md`). `cmake --build build-f429 --target pge_f429_platformer` builds one firmware instead of all.

### 2.5 Firmware targets

Each target has a matching `flash_<name>` target (except where noted). Targets are built in `build-f429/platform/stm32f429i_disco/`.

| Target | Flash target | Purpose |
|---|---|---|
| `pge_f429_scene` | `flash_f429` | Scene firmware (`examples/f429/main.cpp`) |
| `pge_f429_b1` | `flash_f429_b1` | B1 benchmark: 4 display variants plus micro benchmarks |
| `pge_f429_input` | `flash_f429_input` | Input HAL test screen (buttons, KY-023 stick) |
| `pge_f429_scroller` | `flash_f429_scroller` | Scroller game (same game as the host example) |
| `pge_f429_audio` | `flash_f429_audio` | Audio bench (MAX98357A on PE4/PE5/PE6) |
| `pge_f429_platformer` | `flash_f429_platformer` | Playable platformer, vsync back buffer |
| `pge_f429_platbench` | `flash_f429_platbench` | Platformer autoplay bench |
| `pge_f429_platbench_nobb` | `flash_f429_platbench_nobb` | Platformer autoplay bench, no vsync |
| `pge_f429_sandbox` | `flash_f429_sandbox` | Playable sandbox, vsync back buffer |
| `pge_f429_sandbench` | `flash_f429_sandbench` | Sandbox autoplay bench |

### 2.6 Options

| Option | Default | Meaning |
|---|---|---|
| `PGE_CUBE_DIR` | `F:/____IL_CPP/STM32F429I-DISCO/STM32CubeF4` | STM32CubeF4 checkout (not vendored) |
| `PGE_F429_ROT` | `0` | Landscape rotation. `0` = upright (confirmed on the board). `1` = 180-degree rotated |
| `PGE_F429_BACKBUF` | `0` | `1` makes `pge_f429_scene` use the vsync back buffer (FB1 at `0xD0040000`) |

Example: `cmake -S . -B build-f429 ... -DPGE_F429_ROT=1`.

### 2.7 Reading the debug log (no serial port)

The board has no UART. Firmware writes text to a ring buffer `pge_dbg_log` (32768 bytes). The buffer starts with the string `PGELOG1`, and the write offset is the `uint32` `pge_dbg_pos`. `pge_dbg_now_us` is updated every frame. Read both through OpenOCD.

Symbol addresses change with every build, so look them up first:

```bash
arm-none-eabi-nm build-f429/platform/stm32f429i_disco/pge_f429_b1.elf | grep -E "pge_dbg_(log|pos)$"
```

Dump the buffer (use the addresses from `nm`):

```bash
openocd -f interface/stlink.cfg -c "transport select hla_swd" -f target/stm32f4x.cfg -c "init" -c "mdw <pge_dbg_pos addr>" -c "dump_image build-f429/dump/log.bin <pge_dbg_log addr> 32768" -c "shutdown"
```

To decode `log.bin` in Python, the text is bytes `[8:pos]`. If the buffer has wrapped, the text is bytes `[pos+1:]` followed by `[8:pos]`.

Benchmark firmware ends with a marker line and LED4 on:

| Firmware | End marker |
|---|---|
| `pge_f429_b1` | `# B1 DONE` (LED3 with `FAILED rc=` on error) |
| platformer bench | `# PLAT DONE` |
| sandbox bench | `# SANDBOX DONE` |
| audio | `# AUDIO DONE` |

The platformer and sandbox benches are autoplay: 60 warm-up ticks plus 1800 samples (about 31 s). Wait about 50 s after flashing before reading the log.

To restore the original board firmware, use the same interface and target arguments as above with `program <backup .bin> 0x08000000 verify reset exit`.
---

## 3. ESP-IDF

Not implemented: deferred with session S4.5 (user decision 2026-11-15). ESP-IDF is not installed on the dev machine; platform/esp32s3 is a placeholder. No build steps exist yet.

---

## 4. Asset tools

The asset tools need Python 3 and Pillow (no other dependency). Install Pillow into the Python you run.

Convert sprite sheets, palettes and tilemaps into PGEA blobs and C headers:

```bash
python tools/pge_assets.py sprites sheet.png --frame 16x16 [--atlas] [--type tileset] --out-bin sheet.pgea --out-c sheet.h --name kSheet
python tools/pge_assets.py palette sheet.png --out-c pal.h --name kPal
python tools/pge_assets.py tilemap level.csv|level.json [--layer NAME] --out-bin level.pgea --out-c level.h --name kLevel
python tools/pge_assets.py font glyphs.png --frame 8x8 (--chars 'ABC abc 0123' | --chars-file F) [--spacing 1] --out-bin font.pgea --out-c font.h --name kFont
python tools/pge_assets.py strings texts.csv [--prefix TXT_] --out-bin texts.pgea --out-c texts.h --name kTexts
python tools/pge_assets.py selftest [--emit-dir DIR]
```

Notes:

- Pixels are 8-bit indexed only (no 4 bpp packing). Palette is RGB565. Transparent is palette index 0.
- Tiled JSON: one tileset, plain `data` list, no flip bits, no base64 or compression, no infinite maps. id = gid - firstgid + 1 (0 = air, 255 reserved). CSV ids are used as authored; negative values become 0.
- `strings` reads a UTF-8 CSV: header `id,en,ru,...` (the first language is the fallback), one row per string, an empty cell in another language falls back to the first one. `--out-c` also writes an enum with `<PREFIX><ID>` and `<PREFIX>COUNT`. Example: `examples/sandbox/game_text.csv`.
- `selftest` is also run by ctest (`tools_assets`).

Memory report for a firmware image:

```bash
python tools/memreport.py build-f429/platform/stm32f429i_disco/pge_f429_platformer.map --elf <same>.elf --core-lib build-f429/core/libpge_core.a
python tools/memreport.py --selftest
```

`memreport.py --selftest` is run by ctest (`tools_memreport`).

---

## 5. Running tests

Tests exist only in the host build and are skipped when `PGE_BUILD_TESTS=OFF`.

```bash
ctest --test-dir build --output-on-failure
ctest --test-dir build -R ui
```

Test names (from `tests/CMakeLists.txt`):

| Group | Test names |
|---|---|
| Core and graphics | `version`, `gfx`, `core`, `prims`, `sprites`, `camera`, `input`, `collision` |
| World and game | `platformer`, `world`, `worldgen`, `entities`, `health`, `particles`, `bench`, `sandbox`, `sandbox_includes` |
| Storage | `storage`, `settings`, `storage_fatfs`, `storage_littlefs`, `settings_fatfs`, `settings_littlefs`, `hal_storage_host`, `hal_storage_fatfs`, `hal_storage_littlefs` |
| Assets and tools | `asset`, `tools_assets`, `tools_memreport` |
| Audio and UI | `audio`, `ui` |

`tools_assets` and `tools_memreport` are added only if Python 3 with Pillow is found at configure time. Without them, CMake prints `Python3 with Pillow not found: asset tool tests and the cross-language check are skipped`.

---

## 6. Wrappers

PlatformIO (native env only, verified), STM32CubeIDE import notes (untested), VS Code tasks (untested) and the release checklist are in `wrappers.md`. CMake stays the supported build.

---

## 7. Use PGE from your own CMake project

A game keeps its own `CMakeLists.txt` and adds the engine with `add_subdirectory`. The second argument is needed,
because the engine folder is outside the game folder:

```cmake
set(PGE_PLATFORM host)   # or stm32f429i_disco
add_subdirectory(<PGE root> pge)
```

Set `PGE_PLATFORM` before `add_subdirectory`. You can set it as a normal variable or on the command line.
For the F429, `PGE_CUBE_DIR` works the same way.

A normal variable wins over the cache entry (the engine sets policy `CMP0126` to NEW), so `CMakeCache.txt` can still show `PGE_PLATFORM=host` while the F429 is built.

Under `add_subdirectory`, `PGE_BUILD_EXAMPLES` and `PGE_BUILD_TESTS` default to `OFF`. So only the engine libraries
are built. The game picks the build type. The engine sets `Release` only when it is the top-level project.

Link these libraries:

| Library | What it is |
|---|---|
| `pge_core` | The engine. C++17 is passed on to the game. |
| `pge_platform_host` | SDL2 window, input, audio, file storage. Exists only when SDL2 is found. |
| `pge_platform_host_null` | No window. For tests and headless tools. |
| `pge_host_storage` | File storage only. |
| `pge_f429_hal` | STM32F429I-DISCO board HAL, Cube HAL and BSP. |
| `pge_f429_usb` | USB stick and FatFs storage. |

Link `pge_core` first, then the platform library. Link order matters on the F429.

**Host.** The engine finds SDL2 itself. Pass `-DSDL2_DIR` as in section 1.3. On Windows, copy `SDL2.dll` next to
the game exe. The example does this with the generator expression `TARGET_RUNTIME_DLLS`, which needs CMake 3.21.

**F429.** The game project enables C and ASM as well as C++. Use `project(... LANGUAGES C CXX)` plus
`enable_language(ASM)`. Configure with the engine toolchain file, given as an absolute path. After
`add_subdirectory`, call `pge_f429_firmware(<exe> <flash target>)`. The function comes from
`cmake/pge_f429_firmware.cmake`. It gives the `.elf`, `.map`, `.bin` and `.hex` files and the flash target.

The complete example is `examples/subproject/CMakeLists.txt`. It builds the scroller example as the game.
Configure and build commands for the dev machine (PowerShell; PATH set as in sections 1.5 and 2.4):

```powershell
$env:PATH = "C:/Program Files/JetBrains/CLion/bin/cmake/win/x64/bin;C:/Program Files/JetBrains/CLion/bin/ninja/win/x64;C:/Program Files/JetBrains/CLion/bin/mingw/bin;$env:PATH"
cmake -S examples/subproject -B build-sub -G Ninja -DSDL2_DIR="C:/Users/il720506/sdk/SDL2-2.32.10/x86_64-w64-mingw32/lib/cmake/SDL2"
cmake --build build-sub --target demo

$env:PATH = "C:/Program Files/JetBrains/CLion/bin/cmake/win/x64/bin;C:/Program Files/JetBrains/CLion/bin/ninja/win/x64;$env:PATH"
cmake -S examples/subproject -B build-sub-f429 -G Ninja "-DCMAKE_TOOLCHAIN_FILE=F:/____IL_CPP/PIXEL_ENGINE/cmake/arm-none-eabi.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build-sub-f429 --target demo_f429
cmake --build build-sub-f429 --target flash_demo_f429
```

The F429 lines need `arm-none-eabi-gcc` and `arm-none-eabi-g++` on PATH too, as in section 2.4.
