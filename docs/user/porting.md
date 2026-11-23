# Porting guide

> Summary for later sessions
> - A port = one implementation of the 5 HAL headers (K7, link-time binding), a `pge_profile.h`, a CMake platform dir, a `PGE_PLATFORM` value.
> - `pge_profile.h` sets exactly one `PGE_PROFILE_*` = 1 plus `PGE_LOGICAL_W/H`; `PGE_RAM_FAST/DMA/EXT` are optional placement macros.
> - Implement in this order: time, display, input, storage, audio. Test first with `bench/b1` and the headless host tests.
> - Worked examples: STM32F429I-DISCO (display, input, time board-checked; audio compile-only; no storage medium) and host (reference port).
> - ESP32-S3: not available, deferred with session S4.5. Tier B skeletons: see `platform/tier_b/README.md` for status.
> - Future items (not implemented): USB HID host input, 8080 parallel display driver (Q3), band-skip / windowed flush.

Sources: `hal/include/pge/hal/*.h`, `core/include/pge/config.h`, `docs/architecture/02-hal-render-game-api.md` (section 2), `platform/stm32f429i_disco/`, `platform/host/`, root `CMakeLists.txt`, PLAN.md K7, K11, K14. Signatures: [api-hal.md](api-hal.md). Wiring and flashing: [hardware.md](hardware.md).

## 1 Overview: what a port consists of

| Part | What it is |
|---|---|
| HAL implementation | One implementation of the five headers `display.h`, `input.h`, `time.h`, `audio.h`, `storage.h` (`hal/include/pge/hal/`). K7: C-compatible free functions, one implementation linked per binary, no virtual calls in hot paths, no runtime backend switching. |
| `pge_profile.h` | Found through `PGE_PROFILE_DIR`. Sets exactly one of `PGE_PROFILE_SMALL`, `PGE_PROFILE_MEDIUM`, `PGE_PROFILE_LARGE` to 1 and the logical size `PGE_LOGICAL_W` / `PGE_LOGICAL_H`. |
| Placement macros (optional) | `PGE_RAM_FAST`, `PGE_RAM_DMA`, `PGE_RAM_EXT`, `PGE_ROM`. `config.h` defaults them to empty; the platform profile maps them to sections or attributes and core only applies them. The core uses `PGE_RAM_DMA` on the init arena and `PGE_RAM_FAST` on the frame arena (`core/src/base/engine.cpp`). |
| CMake platform dir | `platform/<name>/CMakeLists.txt`, added by the root `CMakeLists.txt`. |
| `PGE_PLATFORM` value | Cache variable of the root `CMakeLists.txt` (`host` is the default). `PGE_PROFILE_DIR` is set to `platform/${PGE_PLATFORM}` before `add_subdirectory(core)`, and `pge_core` gets it as a public include directory, so `config.h` finds your `pge_profile.h`. |

Rules from the sources:

- The core includes no platform header; hardware is reached only through the HAL (K1, K7).
- Int-returning HAL functions return `pge_error`: 0 = ok, -1 InvalidArg, -3 NotSupported, -4 Io.
- Link order matters on the ARM example: `pge_core` comes before the platform library because core needs the HAL symbols.
- Root CMake variable: `set(PGE_PLATFORM host CACHE STRING "host | stm32f429i_disco")` in the tree this guide was written against. Tier B values are documented in `platform/tier_b/README.md`.

## 2 HAL checklist

Order of implementation: time, display, input, storage, audio. Each step can be tested before the next exists (storage and audio may be stubs that return an error, as the F429 sandbox firmware does for storage).

### 2.1 Time (`time.h`)

| Function | Contract | Done when |
|---|---|---|
| `uint32_t pge_hal_time_us(void)` | Monotonic, wraps at 2^32 us, callers subtract as uint32. ISR-safe (spec 2.2). | Two reads differ by the elapsed time; a read across the 32-bit wrap still gives the right difference with unsigned subtraction. |
| `void pge_hal_time_delay_us(uint32_t us)` | Blocks, not ISR-safe, for frame pacing only, may be coarse. | A delay of `us` returns after at least roughly `us` (measured with `pge_hal_time_us`). |

### 2.2 Display (`display.h`)

| Function | Contract | Done when |
|---|---|---|
| `pge_error pge_hal_display_init(uint16_t want_w, uint16_t want_h, pge_display_info* out)` | `want_w/h` are the display coordinates the core will use; NotSupported if the panel cannot show them even with transposing. Fills `panel_w`, `panel_h`, `flags`. | Returns 0 for the profile's logical size and a negative value for a size the panel cannot show; `*out` is filled. |
| `pge_error pge_hal_display_flush(pge_rect r, uint16_t* px)` | `px` is `r.w*r.h` RGB565, row-major, stride `r.w`. One outstanding flush. Rect inside display coordinates, else InvalidArg. Caller owns `px` until `wait()` returns (sync platform: until `flush` returns); content undefined afterwards. | A full-screen test pattern and a small off-origin rect appear at the right place; an out-of-range rect returns -1. |
| `pge_error pge_hal_display_wait(void)` | Returns when the last flush ended; no-op on sync platforms. | Returns 0 after every flush; on an async platform the buffer may be reused only after it returns. |
| `void pge_hal_display_frame_begin(void)` | Hint, may be empty. | Compiles and links. |
| `void pge_hal_display_frame_end(void)` | `PGE_DISP_VSYNC` set: buffer swap, blocks until latched. Else a hint. | With VSYNC: frame N+1 never draws into the buffer being scanned out (no tearing in a scrolling test). |

None of the display calls is ISR-safe.

### 2.3 Input (`input.h`)

| Function | Contract | Done when |
|---|---|---|
| `int pge_hal_input_init(void)` | `pge_error`, 0 = ok. | Returns 0 (the F429 returns 0 even when the touch controller is missing and touch stays off). |
| `int pge_hal_input_poll(pge_raw_input* out)` | Short and bounded. Fills `buttons` (`PGE_BTN_*` bits, plus `PGE_INPUT_QUIT` when the platform asks to quit), `axis[4]` (-32768..32767, 0 = centre, `axis[0]` +right, `axis[1]` +down), `touch_down`, `touch_x/y` in display pixel coordinates of the landscape plane used by flush rects, origin top-left (x/y are 0 when no touch). The platform does debounce, stick centre and deadband. | Every wired button toggles its bit; the stick reads 0 at rest and the signs match; a touch at a corner reports that corner in display coordinates. |

### 2.4 Storage (`storage.h`)

| Function | Contract | Done when |
|---|---|---|
| `int pge_hal_storage_mount(void)` | Signature only in the header (no comment). | Returns 0 when a medium is usable, a negative value otherwise. |
| `int pge_hal_storage_open(const char* path, int mode, pge_file* out)` | `mode` 0 = read, 1 = write-truncate (creates the file). Writes the handle to `*out`; `pge_file` < 0 is invalid. Paths are platform-relative. Files are binary. | A written file can be opened for read; a missing file in read mode returns an error. |
| `int32_t pge_hal_storage_read(pge_file f, void* buf, uint32_t n)` | Bytes read, 0 = end of file, < 0 = error. | Round trip of a known buffer is byte-identical. |
| `int32_t pge_hal_storage_write(pge_file f, const void* buf, uint32_t n)` | Bytes written, < 0 = error. | Same round trip. |
| `int pge_hal_storage_close(pge_file f)` | Signature only in the header. | Returns 0; the data is readable afterwards. |
| `int pge_hal_storage_rename(const char* from, const char* to)` | `to` must NOT exist, else Io. Not an atomic replace (callers `remove` first). | Rename onto an existing name returns -4. |
| `int pge_hal_storage_remove(const char* path)` | Io if the file does not exist or cannot be removed. | Removing a missing file returns -4. |

No medium: return -4 from all of them, so `mount` fails. The sandbox example then shows `NO STORAGE` for SAVE / LOAD (this is what the F429 firmware does). The host storage test is `tests/test_hal_storage.cpp`.

### 2.5 Audio (`audio.h`)

| Function | Contract | Done when |
|---|---|---|
| `int pge_hal_audio_init(uint32_t sample_rate, pge_audio_fill_fn fill)` | `sample_rate` in Hz; a second call replaces the previous registration. May open the device, must not start playback. Return 0 or a negative `pge_error` (-1, -3, -4). | Returns 0 for a supported rate, -3 or -1 for an unsupported one; nothing is audible yet. |
| `int pge_hal_audio_start(void)` | Playback begins; `fill` is called from now on. | `fill(int16_t* stereo, uint32_t frames)` is called repeatedly with interleaved L,R int16 buffers. |
| `int pge_hal_audio_stop(void)` | Playback pauses; the device stays open. | `fill` stops being called; `start` resumes. |

`fill` runs in the platform audio context (a thread or an interrupt): the code in it must be bounded and allocation-free. With `PGE_AUDIO_CHANNELS` 0 (SMALL) audio is off.

### 2.6 What to test first

1. Headless: build the core against your platform, not a window. `bench/b1` (`b1_run`, `bench/b1/b1.h`) is portable: it needs `pge_core` plus the display and time HAL only, and prints through a callback you supply. The host build runs it as `pge_b1_host` against `pge_platform_host_null`; the F429 build runs it as `pge_f429_b1`. A port that runs `b1_run` has working time and display.
2. Host headless tests (`tests/`, `ctest`) show what the core expects from a HAL without a screen: for example `test_core`, `test_bench` link `pge_platform_host_null`.
3. Then input (a test firmware that draws the buttons, as `pge_f429_input` does), storage, audio.
4. A bench number is only valid for the board, clock and build type it was measured on; do not copy figures between ports.

## 3 Display notes

`pge_display_info.flags` is a bitmask the platform reports from `display_init`.

| Flag | Value | Use it when | How the F429 port uses it |
|---|---|---|---|
| `PGE_DISP_ASYNC` | `0x01` | Flush can return before the transfer ends (DMA or an interrupt-driven bus). Then `flush` blocks only until the previous flush ended, `wait` blocks until the last one ended, and the caller keeps the buffer until `wait` returns. | Not set. Flush is synchronous and `pge_hal_display_wait` returns 0. |
| `PGE_DISP_TRANSPOSED` | `0x02` | The panel is natively rotated (portrait) and the core renders in the landscape orientation it asked for: the platform rotates inside `flush`. The core always renders in the orientation `want_w/h` names. | Set when the logical size is 320x240 (panel 240x320). Not set for 240x320. `PGE_F429_ROT` picks the direction (0 = upright, confirmed on the board). |
| `PGE_DISP_VSYNC` | `0x04` | Two scan-out buffers exist. `flush` writes the back buffer, `frame_end` swaps and blocks until the swap is latched, so frame N+1 never draws into the buffer being scanned out. | Set when the back buffer option is on (`pge_f429_set_opts(dma2d, backbuf)`, `backbuf` = 1): `frame_end` reloads the LTDC at vertical blanking and swaps FB0 / FB1. Without it flush writes the displayed buffer and tearing is accepted. |

Other display rules:

- A 240x320 portrait request is accepted by the F429 and copied row by row (memcpy, or DMA2D M2M when `dma2d` = 1); any other size returns -1.
- A panel that cannot show the requested size even with transposing returns NotSupported from `display_init`; the spec fallback is a portrait game with `want_w/h` = panel size.
- `pge_hal_display_init` may be called again; the F429 initialises the hardware on the first call only and later calls switch orientation and options.
- The host null display reports `flags = 0`, validates the rect and draws nothing (`platform/host/display_null.cpp`).

## 4 Capability profile

`pge_profile.h` picks the profile and optionally the logical size; everything else comes from `config.h`. Order of inclusion: the game's optional `pge_user_config.h`, then the board's `pge_profile.h`, then the defaults. Every macro can be pre-set; `config.h` has a static assert that exactly one of `PGE_PROFILE_SMALL/MEDIUM/LARGE` is 1.

| Macro | SMALL | MEDIUM | LARGE |
|---|---|---|---|
| `PGE_LOGICAL_W` x `PGE_LOGICAL_H` | 160x128 | 320x240 | 320x240 |
| `PGE_BAND_LINES` | 8 | 16 | 32 |
| `PGE_INTEGER_SCALE_MAX` | 1 | 2 | 3 |
| `PGE_PARALLAX_LAYERS` | 1 | 2 | 4 |
| `PGE_CHUNK_CACHE_COUNT` | 4 | 16 | 64 |
| `PGE_STORAGE_IO_BYTES` | 512 | 4096 | 16384 |
| `PGE_AUDIO_CHANNELS` | 0 | 4 | 8 |
| `PGE_PARTICLE_COUNT` | 32 | 128 | 512 |
| `PGE_ENTITY_COUNT` | 64 | 256 | 1024 |
| `PGE_INIT_ARENA_BYTES` | 8192 | 32768 | 65536 (all plus `PGE_SCALE_STAGING_BYTES`) |
| `PGE_FRAME_ARENA_BYTES` | 512 | 2048 | 4096 |

How to choose and override:

- Pick the profile that matches the RAM and features of the board; see [modules.md](modules.md) section 3 for the full table and its caveats (band lines for LARGE: `config.h` says 32 where 01-overview says 16; `config.h` is the value in force).
- `PGE_LOGICAL_W/H` defaults to 160x128 (SMALL) or 320x240; set them in `pge_profile.h` to override (the host and F429 profiles set 320x240 explicitly).
- Any other macro in the table: `#define` it in `pge_profile.h` (board-wide) or `pge_user_config.h` (per game) before `config.h` reads it.
- `PGE_SCALE` (default 1) must be 1..`PGE_INTEGER_SCALE_MAX`; display size = logical size x `PGE_SCALE`. For `PGE_SCALE` > 1 the init arena grows by `PGE_SCALE_STAGING_BYTES`.
- `PGE_TICK_HZ` (default 60), `PGE_DEBUG_OVERLAY` (default 1, compiled out at 0) and `PGE_LIGHTING_ENABLED` (default 0) are not profile-dependent.
- Placement: define `PGE_RAM_FAST`, `PGE_RAM_DMA`, `PGE_RAM_EXT`, `PGE_ROM` in `pge_profile.h` as section attributes of your linker script; leave them undefined to get empty macros. The host profile defines none of them; the F429 profile defines only PGE_RAM_EXT as section .sdram (NOLOAD, upper 4 MB of the SDRAM at 0xD0400000, usable after pge::init(); S8.2).

## 5 Worked example 1: STM32F429I-DISCO

Directory `platform/stm32f429i_disco/`:

| File | What it implements |
|---|---|
| `pge_profile.h` | `PGE_PROFILE_LARGE` 1, `PGE_LOGICAL_W` 320, `PGE_LOGICAL_H` 240 (landscape; the panel is portrait). |
| `board.h`, `board.c` | `pge_board_init` (HAL, 180 MHz clock, DWT, SysTick, LEDs), `pge_board_led`, `pge_sdram_selftest`, the `pge_dbg_log` ring buffer (no UART), and the time HAL `pge_hal_time_us` / `pge_hal_time_delay_us` (DWT cycle counter accumulated into microseconds, interrupts masked while reading). |
| `display_ltdc.c` | The display HAL on the LTDC: 240x320 panel, framebuffers FB0 0xD0000000 and FB1 0xD0040000 in SDRAM, landscape transpose by CPU copy, portrait copy by memcpy or DMA2D, optional vsync back buffer. `pge_f429_set_opts`, `pge_f429_bench_fill`. |
| `input_gpio.c` | The input HAL: USER button, external buttons on GPIO, KY-023 analog stick on ADC1, STMPE811 touch through the ST BSP. Pin table in [hardware.md](hardware.md). |
| `audio_sai.c` | The audio HAL: SAI1 block A as I2S master transmitter, circular DMA, to a MAX98357A-class amplifier on PE4/PE5/PE6. |
| `STM32F429ZITX_FLASH.ld` | Linker script, passed with `-T`. |
| `stm32f4xx_hal_conf.h` | Cube HAL module configuration; the directory is on the include path. |
| `CMakeLists.txt` | Cube HAL + BSP static library and the firmware targets. |

There is no storage implementation in the platform directory. The firmwares that need the storage symbols (for example `examples/f429/sandbox_main.cpp`) define stubs that return -4.

How CMake wires it:

- Root `CMakeLists.txt`: with `-DPGE_PLATFORM=stm32f429i_disco` the languages are CXX, C and ASM, the build type defaults to Release, `bench/b1` is added `EXCLUDE_FROM_ALL`, then `platform/stm32f429i_disco`. The toolchain file is `cmake/arm-none-eabi.cmake`.
- STM32CubeF4 is not vendored: `PGE_CUBE_DIR` (cache path) must point at a checkout, and CMake stops with a fatal error if `stm32f4xx_hal.c` is not found there.
- Library `pge_f429_hal`: Cube HAL modules, the BSP (LCD, SDRAM, touch, STMPE811, ILI9341 component), CMSIS system file, the GCC startup file, and the five own files (`board.c display_ltdc.c input_gpio.c audio_sai.c`, compiled with `-Wall -Wextra`). It defines `STM32F429xx`, `USE_HAL_DRIVER`, `PGE_F429_ROT` and `PGE_F429_BACKBUF` publicly and exposes `hal/include` and the Cube headers.
- Function `pge_f429_firmware(tgt flash_tgt)`: adds `-T<linker script>` and `-Wl,--undefined=Reset_Handler` (pulls the vector table out of the archive), writes a `.map`, and post-build produces `.bin`, `.hex` and the size listing. It also creates a `flash_<name>` target that runs OpenOCD with the ST-LINK.
- Firmwares link `pge_core` before `pge_f429_hal`. Targets: `pge_f429_scene`, `pge_f429_b1`, `pge_f429_input`, `pge_f429_scroller`, `pge_f429_audio`, `pge_f429_platformer`, `pge_f429_platbench`, `pge_f429_platbench_nobb`, `pge_f429_sandbox`, `pge_f429_sandbench`; flash targets are listed in [hardware.md](hardware.md).
- Configure and build (PowerShell, from the repo root):

```powershell
cmake -S . -B build-f429 -G Ninja -DPGE_PLATFORM=stm32f429i_disco "-DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build-f429
```

Verified and not verified (from [hardware.md](hardware.md) and STATUS.md):

| Part | State |
|---|---|
| Display | Board-checked. |
| Buttons, stick, touch | Board check S4.3 (2026-11-15) passed. |
| Time | No separate board check is recorded in the sources read for this guide. |
| Audio (`audio_sai.c`) | Compile-only. Nothing was run on a board; the S5.5 board check is not done. |
| Storage | No storage medium: no diskio or block device, stubs return -4, SAVE / LOAD show `NO STORAGE`. USB mass storage is not implemented. |

## 6 Worked example 2: host (reference and simulation port)

`platform/host/` needs no hardware and is the quickest way to see a complete HAL. Its profile is `PGE_PROFILE_LARGE`, 320x240.

| Target (CMake) | Files | Role |
|---|---|---|
| `pge_host_common` | `host_common.cpp`, `host_bus.h` | Shared host code; public include of `hal/include`. |
| `pge_host_storage` | `storage_file.cpp` | Storage HAL on `<stdio.h>`; binary mode; 4 file slots; a budget hook simulates power loss (`host_storage.h`). |
| `pge_platform_host_null` | `display_null.cpp`, `input_null.cpp`, `audio_null.cpp` | Headless: the display validates rects, charges the simulated bus and draws nothing; used by tests and `pge_b1_host`. |
| `pge_platform_host` | `display_sdl.cpp`, `input_sdl.cpp`, `audio_sdl.cpp` | SDL2 window, keyboard and mouse, audio device. Built only when `SDL2::SDL2` exists. |

- Slow-bus simulation: `pge_host_bus_set(clock_hz, bits_per_clock)` and `pge_host_bus_charge(bytes)` (0 = off, default) make the host exercise the slow-display path.
- Null audio: `pge_host_audio_pump` runs the registered `fill` callback once on the caller's thread for tests.
- The host defines no time or input specifics beyond the HAL; the keyboard map is in [hardware.md](hardware.md) section 2.
- Root `CMakeLists.txt` (host branch): `find_package(SDL2 CONFIG QUIET)`, `add_subdirectory(platform/host)`, the examples when SDL2 is found, `bench/b1`, and `tests/` when `PGE_BUILD_TESTS` is ON (default).
- Use it as the reference when your port misbehaves: run the same example on the host and on the target and compare.

## 7 ESP32-S3

Not available: deferred with session S4.5 (user decision 2026-11-15). `platform/esp32s3` holds only a placeholder; no worked example is claimed.

## 8 Tier B skeletons

K11: Tier B ports are HAL skeletons, labelled untested; a Tier B port moves to Tier A only when a board is available. The skeleton, the CMake helper, the `PGE_PLATFORM` values and the per-platform status table are in [platform/tier_b/README.md](../../platform/tier_b/README.md). This guide does not repeat the status.

## 9 Future items

- USB HID host input: out of scope for v1 (STATUS.md, S7 USB HID devices: "future port item").
- 8080 parallel display driver: Q3 (answered 2026-11-15): the ESP32-S3 port uses the existing SPI ILI9341 module; the 8080 parallel screen needs a board that is not built yet and is added later as one more display driver, with no engine change.
- Band-skip (`setDirtyHint`, spec 02 section 3.4) and windowed flush: planned, not done (STATUS.md S5.6).
- Lighting (`PGE_LIGHTING_ENABLED` stays 0): planned, optional.
