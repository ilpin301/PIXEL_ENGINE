# Pixel Game Engine: IDE and Tool Wrappers

> **Summary for later sessions**
> - CMake plus arm-none-eabi-gcc is the supported build. Everything here is a thin wrapper around it.
> - PlatformIO: native (host) env only, verified 2026-11-17. STM32F429I-DISCO and ESP32-S3 are not provided through PlatformIO.
> - STM32CubeIDE: manual import facts only, untested (not installed on the dev machine).
> - VS Code: `.vscode/tasks.json` with 8 tasks, untested (VS Code not installed).
> - Release: version 0.1.1; tags `v0.1.0` and `v0.1.1` exist and are published on GitHub.

Commands and paths that are not repeated here are in `install.md` and `hardware.md`.

---

## 1. PlatformIO (native env only)

Status: verified 2026-11-17: pio 6.2.0, build ok, program ran, exit 0.

The manifest is `../../platformio.ini` (repository root). It has one environment, `native`. It builds the same program as the CMake target `pge_empty`: `pge_core`, the null host platform (`host_common`, `storage_file`, `display_null`, `input_null`, `audio_null`) and `examples/empty/main.cpp`. Flags: `-std=gnu++17 -fno-exceptions -fno-rtti -Wall -Wextra`, include paths `core/include`, `hal/include`, `platform/host`. A whitelist (`build_src_filter`) keeps everything else under the repository (tests, other examples, bench, tools, other platforms) out of the build.

Build and run (PowerShell; the executable is `program.exe` on Windows, `program` elsewhere):

```powershell
pio run -e native
.pio/build/native/program.exe --seconds 1
```

Notes:

- CMake stays the canonical build. The source list in `platformio.ini` mirrors `core/CMakeLists.txt` and `platform/host/CMakeLists.txt` and has to be kept in sync by hand when files are added there.
- The window demos (SDL2) are not part of this env.
- STM32F429I-DISCO and ESP32-S3 are NOT provided through PlatformIO. Use CMake for the board (see `install.md` section 2). ESP32-S3 is deferred together with session S4.5 (see `install.md` section 3).

---

## 2. STM32CubeIDE import notes

Status: untested: STM32CubeIDE is not installed on the dev machine.

The supported build is CMake + arm-none-eabi (`install.md` section 2). The CubeIDE route is a manual import: create an empty STM32 project (target STM32F429ZITx) or import the existing sources, then add the facts below. The menu steps in CubeIDE are unverified and not described here. The flash and debug step in CubeIDE (ST-LINK) was not tried; the verified flash path is the `flash_<name>` CMake target with OpenOCD (`hardware.md`, "Flashing").

The facts below come only from `platform/stm32f429i_disco/CMakeLists.txt` and `cmake/arm-none-eabi.cmake`.

### 2.1 MCU and compiler flags

- MCU flags, for C, C++ and assembler: `-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard`.
- C and C++: `-ffunction-sections -fdata-sections`. C++ also: `-std=c++17 -fno-exceptions -fno-rtti -fno-threadsafe-statics`. (`-std=c++17` is set by the root `CMakeLists.txt`, extensions off, not by the toolchain file.)
- Assembler: `-x assembler-with-cpp`.
- Release configuration: `-O2 -g`. Own files (not Cube) also use `-Wall -Wextra`.
- Link flags: `--specs=nano.specs --specs=nosys.specs -Wl,--gc-sections`.

### 2.2 Preprocessor defines

- `STM32F429xx`
- `USE_HAL_DRIVER`
- `PGE_F429_ROT=0` (landscape rotation, 0 = upright, confirmed on the board)
- `PGE_F429_BACKBUF=0` (1 = vsync back buffer in `pge_f429_scene`)
- Per example, some firmware targets add their own defines: `PGE_PLATFORMER_BENCH`, `PGE_PLAT_BACKBUF`, `SANDBOX_BENCH`, `SANDBOX_BACKBUF` (see the functions at the end of the platform `CMakeLists.txt`).

### 2.3 Include directories

Relative to the STM32CubeF4 checkout (`PGE_CUBE_DIR`, not vendored):

- `Drivers/CMSIS/Include`
- `Drivers/CMSIS/Device/ST/STM32F4xx/Include`
- `Drivers/STM32F4xx_HAL_Driver/Inc`
- `Drivers/BSP/STM32F429I-Discovery`
- `Drivers/BSP/Components/Common`, `Drivers/BSP/Components/ili9341`, `Drivers/BSP/Components/stmpe811`
- `Utilities/Fonts`

Relative to this repository:

- `platform/stm32f429i_disco` (`board.h`, `stm32f4xx_hal_conf.h`)
- `hal/include`
- `core/include` (from the `pge_core` target; this path is listed in `platformio.ini`)

### 2.4 Sources

- Own files, in `platform/stm32f429i_disco/`: `board.c`, `display_ltdc.c`, `input_gpio.c`, `audio_sai.c`.
- Engine core: the `pge_core` library, sources listed in `core/CMakeLists.txt` (not re-read for this page; the list in `platformio.ini` mirrors it).
- Application: one of `examples/f429/*.cpp` (for example `main.cpp` for `pge_f429_scene`, `platformer_main.cpp`, `sandbox_main.cpp`), with extra include dirs per example (`examples/proto`, `examples/scroller`, `examples/platformer`, `examples/sandbox`).
- Cube HAL modules, `Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_<m>.c` for `m` in: `hal`, `hal_adc`, `hal_adc_ex`, `hal_cortex`, `hal_dma`, `hal_dma2d`, `hal_flash`, `hal_flash_ex`, `hal_gpio`, `hal_i2c`, `hal_ltdc`, `hal_ltdc_ex`, `hal_pwr`, `hal_pwr_ex`, `hal_rcc`, `hal_rcc_ex`, `hal_sai`, `hal_sai_ex`, `hal_sdram`, `hal_spi`, `ll_fmc`.
- BSP files, in `Drivers/BSP/STM32F429I-Discovery/`: `stm32f429i_discovery.c`, `stm32f429i_discovery_lcd.c` (it includes `Utilities/Fonts/font*.c` itself), `stm32f429i_discovery_sdram.c`, `stm32f429i_discovery_ts.c` (needs only the `IOE_*` functions from `stm32f429i_discovery.c`, not `_io.c`).
- BSP components: `Drivers/BSP/Components/stmpe811/stmpe811.c`, `Drivers/BSP/Components/ili9341/ili9341.c`.
- System file: `Drivers/CMSIS/Device/ST/STM32F4xx/Source/Templates/system_stm32f4xx.c`.
- Startup: `Drivers/CMSIS/Device/ST/STM32F4xx/Source/Templates/gcc/startup_stm32f429xx.s`.

### 2.5 Linker script and link order

- Linker script: `platform/stm32f429i_disco/STM32F429ZITX_FLASH.ld` (generated by STM32CubeIDE for the STM32F429ZITx; see `../../THIRD_PARTY_NOTICES.md` section 1.1).
- The CMake build adds `-T<script> -Wl,--undefined=Reset_Handler -Wl,-Map=<name>.map`. `--undefined` pulls the startup object (vector table) out of the static archive. If your project compiles the startup file as a normal object, this is not needed.
- Link order: `pge_core` first, then the HAL library (`pge_f429_hal`). The core needs the display and time HAL symbols, so order matters with static archives. The B1 benchmark links `pge_b1 pge_core pge_f429_hal`, with the core repeated after `pge_b1`.
- Output: ELF, plus `.bin` and `.hex` via `objcopy`, and `arm-none-eabi-size`.

### 2.6 Debug output: ring buffer, not UART

The board has no UART. Firmware writes text to the RAM ring buffer `pge_dbg_log` (32768 bytes, starts with `PGELOG1`, write offset `pge_dbg_pos`). Read it over ST-LINK; the OpenOCD commands are in `install.md` section 2.7. Whether the same read works from the CubeIDE debugger was not tried.

---

## 3. VS Code

Status: untested in VS Code (not installed).

`../../.vscode/tasks.json` holds 8 tasks. The command lines equal the ones in `install.md`; they were only cross-checked against that file and the CMake target names. The file assumes cmake, ninja, g++ / arm-none-eabi-gcc and openocd on PATH; on the Windows dev machine apply the PATH lines from `install.md` sections 1.5 and 2.4 first.

| Task label | What it runs |
|---|---|
| `PGE: configure host` | `cmake -S . -B build -G Ninja` |
| `PGE: build host` | `cmake --build build` (default build task) |
| `PGE: test host` | `ctest --test-dir build --output-on-failure` |
| `PGE: configure F429` | configure `build-f429` with `PGE_PLATFORM=stm32f429i_disco`, the arm toolchain file, Release, and a prompt for `PGE_CUBE_DIR` |
| `PGE: build F429 platformer` | `cmake --build build-f429 --target pge_f429_platformer` |
| `PGE: flash F429 platformer` | `cmake --build build-f429 --target flash_f429_platformer` (needs the board; replaces the firmware on it) |
| `PGE: configure Tier B skeleton` | configure `build-tb-<platform>` for rp2040, rp2350, stm32h7 or stm32f4_generic (picked from a list) |
| `PGE: build Tier B skeleton` | build that directory (runs the configure task first) |

---

## 4. Release checklist

Status: the current version 0.1.1 is tagged (`v0.1.1`). The checklist below is for the next release.

- [ ] Version constants updated and equal: `core/include/pge/version.h`, `core/src/base/version.cpp` (`pge_version()`), `tests/test_version.cpp`.
- [ ] `../../CHANGELOG.md` has the entry for the new version.
- [ ] `../../THIRD_PARTY_NOTICES.md` is present and read before redistributing firmware (the F429 build needs the user's own STM32CubeF4 checkout).
- [ ] Host tests pass: `ctest --test-dir build --output-on-failure`.
- [ ] Commit the release state.
- [ ] Tag after that commit: `git tag -a vX.Y.Z -m "<message>"`.
- [ ] Push the commit and the tag.
- [ ] GitHub release only on the user's explicit go.
