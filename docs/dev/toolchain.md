# Toolchain (S3.0)
Facts checked 2026-11-15 on the dev machine (Windows 11, Git Bash).

| Tool | Status | Version | Path | Install if missing |
|---|---|---|---|---|
| CMake + CTest | ok (CLion bundled) | 4.2.2 | C:/Program Files/JetBrains/CLion/bin/cmake/win/x64/bin | - |
| Ninja | ok (CLion bundled) | 1.13.2 | C:/Program Files/JetBrains/CLion/bin/ninja/win/x64 | - |
| Host C++17 compiler | ok (CLion bundled MinGW) | GCC 13.1.0 | C:/Program Files/JetBrains/CLion/bin/mingw/bin/g++.exe | - |
| arm-none-eabi-gcc/g++ | ok | 10.3.1 (GNU Arm Embedded 10.3-2021.10) | C:/ProgramData/chocolatey/bin | - |
| OpenOCD | ok | 0.12.0 (2023-01-14) | C:/ProgramData/chocolatey/bin | - |
| git | ok | 2.45.1.windows.1 | /mingw64/bin/git | - |
| Python | ok | 3.14.4 | C:/Python314 | - |
| SDL2 (dev package) | ok (mingw dev package) | 2.32.10 | C:/Users/<user>/sdk/SDL2-2.32.10 | see SDL2 section |
| ESP-IDF | MISSING | - | - | needed from S4.5 only; Espressif ESP-IDF Tools Installer |
| STM32CubeF4 | not needed now | - | - | submodule of reference repo il721/STM32F429I-DISCO; S3.2 decides how to obtain it |

cmake, ninja and g++ are not on the global PATH; use the CLion ones.

## Host build (verified 2026-11-15: 2 of 2 tests passed: version, gfx)
```
export PATH="/c/Program Files/JetBrains/CLion/bin/cmake/win/x64/bin:/c/Program Files/JetBrains/CLion/bin/ninja/win/x64:/c/Program Files/JetBrains/CLion/bin/mingw/bin:$PATH"
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER="C:/Program Files/JetBrains/CLion/bin/mingw/bin/g++.exe"
cmake --build build
ctest --test-dir build --output-on-failure
```
mingw/bin must be on PATH when tests run (runtime DLLs).

## ARM
- `arm-none-eabi-g++ --version`: arm-none-eabi-g++ (GNU Arm Embedded Toolchain 10.3-2021.10) 10.3.1 20210824 (release)
- `openocd --version`: Open On-Chip Debugger 0.12.0 (2023-01-14-23:37)
- GCC 10.3.1 is old. Check on 2026-11-15: `arm-none-eabi-g++ -std=c++17 -fno-exceptions -fno-rtti -c` compiled a test with a generic lambda and `if constexpr` without errors. Re-check with real code when S3.2 starts.

## SDL2 (ok, 2.32.10)
Installed at C:/Users/<user>/sdk/SDL2-2.32.10 (SDL2-devel mingw package); SDL2.dll is in `x86_64-w64-mingw32/bin`. Configure with the prefix (the demo exe gets SDL2.dll copied next to it post-build):
```
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER="C:/Program Files/JetBrains/CLion/bin/mingw/bin/g++.exe" -DCMAKE_PREFIX_PATH=C:/Users/<user>/sdk/SDL2-2.32.10/x86_64-w64-mingw32
```
Without the prefix, `find_package(SDL2 CONFIG QUIET)` fails, the log says "SDL2 not found: host window demo skipped", and the unit tests still build and pass.

Headless screenshot (no window; 60 frames, 16-line bands, BMP after the last frame; `--band 240` gives the full-height reference, the BMP must be byte-identical):
```
SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software build/examples/proto/pge_proto_demo.exe --frames 60 --shot out.bmp
```
Window demo: `build/examples/proto/pge_proto_demo.exe` (no `--frames`; runs until the window is closed; `--band L` sets the band height).

## ESP-IDF
Missing. Needed from S4.5 (ESP32-S3). Install with the Espressif ESP-IDF Tools Installer.

## Host benchmark (S3.1b)
Configure, build and run the B1 host benchmark in a separate Release dir (tests off, so asserts stay on in the test build only):
```bash
cmake -S . -B build-rel -G Ninja -DCMAKE_BUILD_TYPE=Release -DPGE_BUILD_TESTS=OFF -DCMAKE_CXX_COMPILER="C:/Program Files/JetBrains/CLion/bin/mingw/bin/g++.exe"
cmake --build build-rel
build-rel/bench/b1/pge_b1_host.exe --frames 200
# optional: --bands 8,16,32,240
# optional slow-bus simulation: --bus-hz 40000000 --bus-bits 1
```
Output goes to docs/benchmarks/raw/host.txt and is tagged host, not representative.

## STM32F429I-DISCO build (S3.2a)
Cross build only (arm-none-eabi 10.3.1, Release -O2 -g); flashing is a separate step. STM32CubeF4 is not vendored: `PGE_CUBE_DIR` (cache var, default `F:/____IL_CPP/STM32F429I-DISCO/STM32CubeF4`) points at it. The host build is unchanged (`PGE_PLATFORM` defaults to `host`).
```bash
export PATH="/c/Program Files/JetBrains/CLion/bin/cmake/win/x64/bin:/c/Program Files/JetBrains/CLion/bin/ninja/win/x64:$PATH"
cmake -S . -B build-f429 -G Ninja -DPGE_PLATFORM=stm32f429i_disco -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-f429                 # -> build-f429/platform/stm32f429i_disco/pge_f429_scene.{elf,bin,hex,map}
cmake --build build-f429 --target flash_f429   # openocd + ST-LINK (needs the board)
```
Clock 180 MHz (HSE 8 MHz, PLL 8/360/2, overdrive). Orientation: default `PGE_F429_ROT=0` (upright, confirmed on the board); `-DPGE_F429_ROT=1` selects the 180-degree-rotated variant. There is no UART: read the debug ring buffer `pge_dbg_log` (32768 B, starts with the string `PGELOG1`, write offset in the uint32 `pge_dbg_pos`) through OpenOCD memory reads; `pge_dbg_now_us` is updated every frame.

Firmware targets (all in `build-f429/platform/stm32f429i_disco/`): `pge_f429_scene` (+ `flash_f429`), `pge_f429_b1` (+ `flash_f429_b1`; B1 in 4 display variants plus micro benchmarks, ends with `# B1 DONE`, LED4 on; LED3 + `FAILED rc=` on error), `pge_f429_input` (+ `flash_f429_input`; input HAL test screen, buttons PA0/PC11/PC12/PD2/PD5, KY-023 stick on PC3/PA5).
`-DPGE_F429_BACKBUF=1` makes `pge_f429_scene` use the vsync back buffer (FB1 at 0xD0040000); default 0. The B1 firmware picks its options per variant at run time.

Read the log without a serial port (symbol addresses move with every build, so look them up first):
```bash
arm-none-eabi-nm build-f429/platform/stm32f429i_disco/pge_f429_b1.elf | grep -E "pge_dbg_(log|pos)$"   # e.g. 20000000 B pge_dbg_log
openocd -f interface/stlink.cfg -c "transport select hla_swd" -f target/stm32f4x.cfg -c "init" -c "mdw <pge_dbg_pos addr>" -c "dump_image build-f429/dump/log.bin <pge_dbg_log addr> 32768" -c "shutdown"
```
In Python the ring buffer is bytes [8:pos] of log.bin (pos from `mdw`; if the buffer has wrapped, the text is bytes [pos+1:] followed by [8:pos]).

Restore the original board firmware (same interface/transport/target args):
```bash
openocd -f interface/stlink.cfg -c "transport select hla_swd" -f target/stm32f4x.cfg -c "program F:/____IL_CPP/STM32F429I-DISCO/backup/flash_backup_0x08000000_2MB.bin 0x08000000 verify reset exit"
```

## S4.3 scroller and touch
- Host scroller: `build/examples/scroller/pge_scroller.exe [--ticks N] [--shot out.bmp]` (arrows/WASD scroll, Z faster, left mouse button recentres the camera; `--ticks` quits after N ticks, `--shot` saves the window to a BMP).
- F429 input test (buttons, stick, touch crosshair): `cmake --build build-f429 --target flash_f429_input` (board needed).
- F429 scroller (`pge_f429_scroller`, same game as the host): `cmake --build build-f429 --target flash_f429_scroller` (board needed).

## S4.4 platformer
- Host: `build/examples/platformer/pge_platformer.exe [--auto] [--ticks N] [--shot f.bmp]` (`--auto` = scripted bot, `--ticks` quits after N ticks, `--shot` saves a BMP).
- F429 firmware (one source, `examples/f429/platformer_main.cpp`, three targets in `build-f429/platform/stm32f429i_disco/`; all need the board): `pge_f429_platformer` (playable, vsync back buffer) with `flash_f429_platformer`; `pge_f429_platbench` (autoplay bench, back buffer) with `flash_f429_platbench`; `pge_f429_platbench_nobb` (autoplay bench, no vsync) with `flash_f429_platbench_nobb`.
- The bench is autoplay: 60 warm-up ticks + 1800 samples (about 31 s). After flashing wait about 50 s, then read `pge_dbg_log` as in the F429 section above (look up addresses with `arm-none-eabi-nm` on the matching elf); the log ends with `# PLAT DONE`, LED4 on. Results: `docs/benchmarks/stage4.md`.

## S6.2b sandbox on the F429
- F429 firmware (one source, `examples/f429/sandbox_main.cpp`, two targets in `build-f429/platform/stm32f429i_disco/`; both need the board): `pge_f429_sandbox` (playable, vsync back buffer) with `flash_f429_sandbox`; `pge_f429_sandbench` (autoplay bench, back buffer) with `flash_f429_sandbench`. Chunk cache 12 slots and change log 2048 entries (`SANDBOX_CACHE`, `SANDBOX_CHANGE_CAP`).
- The bench is autoplay (scripted bot: walks, jumps, digs and places): 60 warm-up ticks + 1800 samples (about 31 s). After flashing wait about 50 s, then read `pge_dbg_log` as in the F429 section above; the log ends with `# SANDBOX DONE`, LED4 on. Besides the timing lines it prints RAM figures (data, bss, stack high-water by stack paint, init arena high-water, chunk cache, change log, entity pool) and the chunk generation time (`chunkgen` lines, after the run). The board has no storage backend yet: SAVE / LOAD show NO STORAGE on the F429.

## S5.4 asset tools and memory report
Python 3 + Pillow (no other dependency). Blob format (little-endian): 12-byte header `PGEA`, type u8 (AssetType value, 6 = Tilemap), version u8 = 1, u16 0, u32 payload size; payload layouts in `core/include/pge/asset.h` and the docstring of `tools/pge_assets.py`. 8-bit indexed pixels only (no 4 bpp packing), palette RGB565, transparent = palette index 0.
```bash
python tools/pge_assets.py sprites sheet.png --frame 16x16 [--atlas] [--type tileset] --out-bin sheet.pgea --out-c sheet.h --name kSheet
python tools/pge_assets.py palette sheet.png --out-c pal.h --name kPal
python tools/pge_assets.py tilemap level.csv|level.json [--layer NAME] --out-bin level.pgea --out-c level.h --name kLevel
python tools/pge_assets.py selftest [--emit-dir DIR]     # also run by ctest (tools_assets, and test_asset reads the emitted headers)
python tools/memreport.py build-f429/platform/stm32f429i_disco/pge_f429_platformer.map --elf <same>.elf --core-lib build-f429/core/libpge_core.a
python tools/memreport.py --selftest                     # ctest: tools_memreport
```
Tiled JSON limits: one tileset, plain `data` list, no flip bits, no base64 / compression, no infinite maps; id = gid - firstgid + 1 (0 = air, 255 reserved). CSV ids are used as authored, negative = 0.

## S5.5 audio

- Host demo: `build/examples/audiodemo/pge_audiodemo.exe [--seconds N] [--bench]` (needs SDL; headless smoke test with SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy).
- F429 firmware: `pge_f429_audio`, flash with `cmake --build build-f429 --target flash_f429_audio`. Board + MAX98357A on PE4 (LRC/FS), PE5 (BCLK), PE6 (DIN). The bench lines `AUDIO ...` and `# AUDIO DONE` go to pge_dbg_log, read as in the F429 section; LED4 on at the end. Flashing replaces the firmware now on the board. memreport counts linked objects only, `.data` in flash and RAM, and cross-checks flash and RAM against `arm-none-eabi-size` (the 8 B `.init_array`/`.fini_array` difference is explained in the OK line).

## S5.6 UI and overlay

- Host demo: `build/examples/platformer/pge_platformer.exe --overlay` (SELECT toggles the overlay).
- Headless shot: `SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software build/examples/platformer/pge_platformer.exe --auto --overlay --ticks 120 --shot out.bmp`
- Test: `ctest -R ui`
## S8.4b rotation on the F429

- B1 now has 14 items (rotated tile and sprite items) and prints about 22.7 KB per full run into the 32,768 B ring (check the log ends with "# B1 DONE" and pos < 32768).
- Read the ring with openocd as in the F429 section, but give dump_image an absolute path and no quote characters around it (a relative or quoted path makes openocd fail with "couldn't open").
- Orientation viewer firmware `pge_f429_orient` with `flash_f429_orient` (needs the board): shows the test picture in orient 0..3, 3 s each, log lines `orient=n`.
- Baseline for S8.x board numbers is docs/benchmarks/raw/f429-head-s8.3.txt (f429.txt of S3.2b is outdated).
- `flash_f429_sandbox` restores the sandbox firmware.

