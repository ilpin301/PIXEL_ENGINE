# HAL API reference

> Summary for later sessions
> - The HAL is five C headers in `hal/include/pge/hal/`: display, input, time, audio, storage. A platform implements every function.
> - All headers are `extern "C"`; int-returning functions return `pge_error` (0 = ok, negative = error).
> - Display: flush RGB565 rects, optional async / transposed / vsync flags reported in `pge_display_info`.
> - Input: one polled `pge_raw_input` struct (buttons, 4 axes, touch). Time: wrapping 32-bit microsecond counter.
> - Audio: stereo int16 pull model via a `fill` callback. Storage: minimal binary file API.
> - Host extras: `host_audio.h` (null-backend pump), `host_bus.h` (slow-bus simulation), `pge_profile.h` (320x240 logical size).
> - STM32F429I-DISCO: `board.h` (init, LEDs, SDRAM test, `pge_dbg_log` ring buffer, `pge_f429_set_opts`) and its own `pge_profile.h`.

Sources: `hal/include/pge/hal/*.h`, `platform/host/*.h`, `platform/stm32f429i_disco/*.h`. Spec context: `docs/architecture/02-hal-render-game-api.md` section 2.

## 1 Common types

Defined in `display.h`:

```c
typedef int pge_error;                      /* 0 = ok, see 02 section 5 */
typedef struct { uint16_t x, y, w, h; } pge_rect;
```

Error values named in the headers: `-1` InvalidArg, `-3` NotSupported, `-4` Io (audio and storage headers).

## 2 Display (`display.h`)

### Flags and info

| Constant | Value | Meaning |
|---|---|---|
| `PGE_DISP_ASYNC` | `0x01` | flush may return before the transfer ends |
| `PGE_DISP_TRANSPOSED` | `0x02` | platform rotates internally (panel is portrait) |
| `PGE_DISP_VSYNC` | `0x04` | two scan-out buffers: `frame_end` swaps and blocks until latched |

```c
typedef struct { uint16_t panel_w, panel_h; uint32_t flags; } pge_display_info;
```

### Functions

| Signature | Meaning |
|---|---|
| `pge_error pge_hal_display_init(uint16_t want_w, uint16_t want_h, pge_display_info* out)` | Initialise the panel. `want_w/h` are the display coordinates the core will use; NotSupported if the panel cannot show them even with transposing (spec 2.2). Fills `*out`. |
| `pge_error pge_hal_display_flush(pge_rect r, uint16_t* px)` | Send a rect. `px`: `r.w*r.h` RGB565, row-major, stride `r.w`. |
| `pge_error pge_hal_display_wait(void)` | Returns when the last flush ended. |
| `void pge_hal_display_frame_begin(void)` | Hint, may be empty. |
| `void pge_hal_display_frame_end(void)` | With `PGE_DISP_VSYNC` set: buffer swap; else hint. |

Contract notes from the spec (2.2):

- One outstanding flush at a time. On a sync platform `flush` blocks until the transfer ends; on an async platform it blocks until the previous flush ended, then returns.
- The caller owns the pixel buffer until `wait()` returns (sync platform: until `flush` returns). Buffer content is undefined afterwards.
- The rect must lie inside display coordinates, else InvalidArg.
- `wait` is a no-op on sync platforms.
- With `PGE_DISP_VSYNC`, frame N+1 never draws into the buffer still being scanned out.
- None of the display calls is ISR-safe.

## 3 Input (`input.h`)

### Constants

| Constant | Value |
|---|---|
| `PGE_INPUT_QUIT` | `0x80000000u` (set in `buttons` when the platform asks to quit) |
| `PGE_BTN_UP` | `0x01` |
| `PGE_BTN_DOWN` | `0x02` |
| `PGE_BTN_LEFT` | `0x04` |
| `PGE_BTN_RIGHT` | `0x08` |
| `PGE_BTN_A` | `0x10` |
| `PGE_BTN_B` | `0x20` |
| `PGE_BTN_START` | `0x40` |
| `PGE_BTN_SELECT` | `0x80` |

### Raw input struct

```c
typedef struct {
  uint32_t buttons;  /* PGE_BTN_* bits, plus PGE_INPUT_QUIT */
  int16_t axis[4];
  uint8_t touch_down;
  int16_t touch_x, touch_y;
} pge_raw_input;
```

- `axis[]`: -32768..32767, 0 = centre, `axis[0]` +right, `axis[1]` +down.
- `touch_x` / `touch_y`: DISPLAY pixel coordinates of the landscape plane that flush rects use (origin top-left).
- `touch_down` is 1 while a finger / mouse button is down; x/y are 0 otherwise.
- Spec 2.2: the platform does debounce, stick centre and deadband.

### Functions

```c
int pge_hal_input_init(void);                 /* pge_error: 0 = ok */
int pge_hal_input_poll(pge_raw_input* out);   /* pge_error: 0 = ok */
```

## 4 Time (`time.h`)

```c
uint32_t pge_hal_time_us(void);              /* monotonic, wraps at 2^32 us: subtract as uint32 */
void     pge_hal_time_delay_us(uint32_t us);
```

- `pge_hal_time_us` is ISR-safe per the spec (2.2); the core uses unsigned subtraction for wrap.
- `pge_hal_time_delay_us` blocks, is not ISR-safe, is for frame pacing only and may be coarse (spec 2.2).

## 5 Audio (`audio.h`)

Stereo output, samples interleaved L,R `int16`. `fill` is called from the platform audio context (a thread or an interrupt): it must be bounded and allocation-free. `init` may open the device but must not start playback. Return 0 = ok, < 0 = `pge_error` values (-1 InvalidArg, -3 NotSupported, -4 Io).

```c
typedef void (*pge_audio_fill_fn)(int16_t* stereo, uint32_t frames);

int pge_hal_audio_init(uint32_t sample_rate, pge_audio_fill_fn fill);
int pge_hal_audio_start(void);
int pge_hal_audio_stop(void);
```

| Function | Meaning |
|---|---|
| `pge_hal_audio_init` | `sample_rate` in Hz; a second call replaces the previous registration. |
| `pge_hal_audio_start` | Playback begins, `fill` is called from now on. |
| `pge_hal_audio_stop` | Playback pauses; the device stays open. |

## 6 Storage (`storage.h`)

Minimal file storage. Paths are platform-relative (host: relative to the current directory). Files are binary. All int-returning functions use `pge_error`: 0 = ok, -1 = InvalidArg, -4 = Io.

```c
typedef int32_t pge_file;  /* < 0 = invalid */

int     pge_hal_storage_mount(void);
int     pge_hal_storage_open(const char* path, int mode, pge_file* out);
int32_t pge_hal_storage_read(pge_file f, void* buf, uint32_t n);
int32_t pge_hal_storage_write(pge_file f, const void* buf, uint32_t n);
int     pge_hal_storage_close(pge_file f);
int     pge_hal_storage_rename(const char* from, const char* to);
int     pge_hal_storage_remove(const char* path);
```

| Function | Meaning |
|---|---|
| `open` | `mode` 0 = read, 1 = write-truncate (creates the file). Writes the handle to `*out`. |
| `read` | Returns bytes read (0 = end of file), < 0 = error. |
| `write` | Returns bytes written, < 0 = error. |
| `rename` | `to` must NOT exist, else Io. Not an atomic replace (callers `remove` first, spec 2.2). |
| `remove` | Io if the file does not exist or cannot be removed. |

`mount`, `close`: signature only (no comment in the header).

## 7 Host platform extras

Host-only headers in `platform/host/`. They are not part of the HAL that a target must implement.

### `host_audio.h`

```c
int pge_host_audio_pump(int16_t* stereo, uint32_t frames);
```

Null audio backend only: runs the registered `fill` callback once on the caller's thread (tests, benches). Returns 0, or -1 if `pge_hal_audio_init` has not succeeded yet.

### `host_bus.h`

```c
void pge_host_bus_set(uint32_t clock_hz, uint32_t bits_per_clock);
void pge_host_bus_charge(uint32_t bytes);
```

- `pge_host_bus_set`: host-only slow-bus simulation. `clock_hz` 0 = off (default).
- `pge_host_bus_charge`: busy-waits for the time `bytes` would take on the simulated bus; no-op when off.

### `pge_profile.h` (host)

```c
#define PGE_PROFILE_LARGE 1
#define PGE_LOGICAL_W 320
#define PGE_LOGICAL_H 240
```

## 8 STM32F429I-DISCO board API

Headers in `platform/stm32f429i_disco/`.

### `pge_profile.h` (F429)

```c
#define PGE_PROFILE_LARGE 1
#define PGE_LOGICAL_W 320  // landscape; the panel is portrait, display_ltdc transposes (PGE_DISP_TRANSPOSED)
#define PGE_LOGICAL_H 240
```

Same profile values as the host; the panel is portrait and `display_ltdc` transposes, so this platform reports `PGE_DISP_TRANSPOSED`.

### `board.h` board functions

| Signature | Meaning |
|---|---|
| `void pge_board_init(void)` | HAL, 180 MHz clock, DWT, SysTick, LEDs. |
| `uint32_t pge_board_sysclk_hz(void)` | Signature only. |
| `void pge_board_delay_ms(uint32_t ms)` | `HAL_Delay`. |
| `void pge_board_led(int which, int on)` | `which`: 3 (red, error) or 4 (green, ok). |
| `int pge_sdram_selftest(void)` | 256 KB at `0xD0100000`; 0 = pass, logs result. |

### Debug ring buffer

There is no UART on this board. The log lives in RAM for OpenOCD: find the string `PGELOG1`.

```c
extern char              pge_dbg_log[32768];
extern volatile uint32_t pge_dbg_pos;           /* next write offset */
extern volatile uint32_t pge_dbg_now_us;        /* updated by main each frame */
void pge_dbg_puts(const char* s);               /* any length; pge_dbg_printf truncates at 127 B */
void pge_dbg_printf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
```

| Symbol | Meaning |
|---|---|
| `pge_dbg_log` | 32768-byte log buffer. |
| `pge_dbg_pos` | Next write offset. |
| `pge_dbg_now_us` | Updated by main each frame. |
| `pge_dbg_puts` | Accepts any length. |
| `pge_dbg_printf` | printf-style; truncates at 127 B. |

### Display options (`display_ltdc.c`)

```c
void pge_f429_set_opts(int dma2d, int backbuf);
void pge_f429_bench_fill(int use_dma2d, uint16_t color);
```

- `pge_f429_set_opts`: call before `pge_hal_display_init`; may also be changed between inits.
  - `dma2d`: portrait (240x320) flush through DMA2D M2M, ignored in landscape.
  - `backbuf`: second framebuffer + vsync swap.
- `pge_f429_bench_fill`: fill the whole 240x320 write framebuffer (micro benchmark). `use_dma2d` selects the DMA2D path, `color` is the fill value.
