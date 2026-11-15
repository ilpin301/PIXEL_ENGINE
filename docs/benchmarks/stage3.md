# Stage 3 benchmark report and decision review (session S3.4, 2026-11-15)

## Summary for later sessions

- Data: benchmark set B1 on host (null display, Release, tag "host, not representative") and on STM32F429I-DISCO (180 MHz, Release -O2, 30 timed frames per item, one run per variant). Raw files: `docs/benchmarks/raw/host.txt`, `docs/benchmarks/raw/f429.txt`.
- F429 worst B1 scene (tiles8 scroll, 2 layers, band 16 in SRAM) [measured]: 14.2 ms landscape + CPU transpose (A), 15.4 ms portrait + CPU memcpy (B), 9.9 ms portrait + DMA2D (C). Panel refresh [measured]: 15,306 us = 65.3 Hz.
- DMA2D band copy (SRAM to SDRAM framebuffer) is 6.6x faster than CPU memcpy [measured: 49 us vs 324 us for 240x16] and runs at 156 MB/s, at the SDRAM peak (160 MB/s, S1.2). DMA2D fill is only 8% faster than CPU fill (965 us vs 1049 us, 240x320).
- DMA2D cannot transpose, so the fast path needs a portrait-native logical screen. Landscape 320x240 stays on the CPU transpose (3.9 ms per frame at band 16).
- R1 band re-run overhead [measured, landscape]: tile scenes +1% (16 lines) and +3% (8 lines) over 32 lines; 100 sprites +11% (16) and +35% (8). Small.
- A full-height band buffer in SDRAM is 1.7x to 2.8x slower than SRAM bands. Use banded SRAM buffers on the F429 even in the LARGE profile.
- R2 tearing on LTDC: back buffer + vsync gives a constant 15,306 us frame (65.3 fps, 32.7 fps if a frame misses one refresh), user saw no tear line. Costs 153,600 B SDRAM.
- Verdicts: K2 confirmed, K3 confirmed (RGB565), K4 unchanged (not testable by B1), K15 unchanged (not measured, prototype has no scale; measure in S4.2a). No Key decision changed.
- Proposals for G3 (user decides): default band 8 / 16 / 32 lines for SMALL / MEDIUM / LARGE; F429 default landscape 320x240 + CPU transpose + vsync back buffer, portrait-native + DMA2D as a profile option; first fps targets in section 6.
- Nothing measured for ESP32-S3, SPI ILI9341, async flush, band-skip, game logic, audio. Those numbers stay "calculated" or absent (K12).
- Anomaly, not investigated: portrait CPU memcpy to SDRAM is slower per byte (23.7 MB/s) than the transposing copy (39.2 MB/s).

## 1 Method and limits

- B1 items: clear, flush_only, 100 opaque sprites, 100 transparent sprites, tiles8 and tiles16 static, tiles8 and tiles16 scroll. Logical screen 320x240 RGB565 (landscape) or 240x320 (portrait).
- `flush_only` = renderer pre-clear + flush. `clear` = pre-clear + flush of an empty scene.
- F429 build: arm-none-eabi-g++ 10.3.1, Release -O2, `-fno-exceptions -fno-rtti`. Timing: DWT cycle counter through `pge_hal_time_us`. Flush is synchronous (DMA2D polled), so there is no CPU/DMA overlap in any number here. LTDC scans out the same framebuffer concurrently except in variant D.
- Variants: A = landscape, CPU transposing copy; B = portrait-native, CPU memcpy; C = portrait-native, DMA2D copy; D = A plus back buffer + vsync (D numbers show the panel refresh period, not render cost).
- Limits: one run per variant, 30 frames per item, no variance figure. The prototype has no tile transparency (layer 2 overdraws), sprites are 8-bit indexed with palette, no scaling. The scene is render + flush only, no game logic, world, entities or audio.
- Host numbers (Release, desktop CPU) are 100x or more faster than the board and say nothing about MCU speed. They are used only for the slow-bus check in section 4.

## 2 STM32F429I-DISCO results

Per frame, band 16 lines, band buffer in SRAM, ms [measured 2026-11-15]:

| Item | A landscape CPU transpose | B portrait memcpy | C portrait DMA2D |
|---|---|---|---|
| clear | 5.90 | 8.48 | 2.98 |
| flush_only | 4.81 | 7.38 | 1.88 |
| sprites_opaque | 7.82 | 10.86 | 5.36 |
| sprites_transparent | 8.03 | 11.08 | 5.58 |
| tiles8_static | 13.60 | 15.33 | 9.83 |
| tiles16_static | 12.34 | 14.18 | 8.69 |
| tiles8_scroll | 14.20 | 15.43 | 9.93 |
| tiles16_scroll | 12.69 | 14.13 | 8.63 |

Flush of one band alone, us [measured, 100 reps]:

| Band | A 320 wide, transposing | B 240 wide, memcpy | C 240 wide, DMA2D |
|---|---|---|---|
| 8 lines | 138 | 162 | 25 |
| 16 lines | 261 | 324 | 49 |
| 32 lines | 507 | 647 | 97 |

Rates: A 10,240 B / 261 us = 39.2 MB/s; B 7,680 B / 324 us = 23.7 MB/s; C 7,680 B / 49.1 us = 156 MB/s. DMA2D vs memcpy = 324 / 49.1 = 6.6x.

Fill of the whole 240x320 panel (153,600 B): CPU 1049 us (146 MB/s), DMA2D 965 us (159 MB/s). DMA2D fill gains 8%, because SDRAM bandwidth is the limit.

Reading: worst scene at band 16 takes 14.2 ms (A) and 9.9 ms (C). B is slower than A although it copies fewer bytes (see anomaly in the summary). Flush plus pre-clear is 34% to 62% of the frame on A (4.8 ms of 7.8 to 14.2 ms); of that, the transposing flush is 3.9 ms (261 us x 15 bands) and the pre-clear about 0.9 ms.

## 3 Band size (R1) and full-height buffers

Variant A, ms per frame [measured]:

| Item | 8 lines (5,120 B) | 16 lines (10,240 B) | 32 lines (20,480 B) | 240 lines in SDRAM (153,600 B) |
|---|---|---|---|---|
| flush_only | 5.05 | 4.81 | 4.69 | 13.23 |
| sprites_opaque | 9.47 | 7.82 | 7.04 | 15.16 |
| tiles8_static | 13.87 | 13.60 | 13.48 | 23.46 |
| tiles8_scroll | 15.01 | 14.20 | 13.81 | 23.54 |

- Overhead against 32 lines: tiles8_static +2.9% (8) / +0.9% (16); tiles8_scroll +8.7% / +2.8%; sprites_opaque +34.5% / +11.0%. The sprite cost grows because every sprite is clipped against every band.
- Full height in SDRAM against 16 lines in SRAM: sprites 1.94x, tiles8 static 1.72x, tiles8 scroll 1.66x, flush_only 2.75x. Cause: the CPU draws and copies in slow SDRAM (assumed, not isolated).
- Portrait variants show the same shape (B: 7.7 / 7.7 ms flush_only at 8 / 32 lines, C: 1.90 / 1.86 ms).

## 4 Calculated bounds against measurement (K12)

| Quantity | Calculated | Measured | Verdict |
|---|---|---|---|
| Band buffer 320x16 RGB565 | 10,240 B | band_buf_bytes 10240 | equal |
| SDRAM peak (S1.2) vs DMA2D fill / copy | 160 MB/s | 159 / 156 MB/s | DMA2D is at the SDRAM limit |
| LTDC refresh (S1.3 assumed 60 Hz) | 60 Hz, 9.2 MB/s scan-out at 320x240 | 15,306 us = 65.3 Hz | panel is 65.3 Hz; scan-out about 10.0 MB/s (6% of SDRAM peak) |
| Slow bus 40 MHz 1-bit, 320x240, band 16 | 30.72 ms/frame | 30.73 ms (host bus simulation) | equal |
| Slow bus 10 MHz (ILI9341 limit) | 122.88 ms/frame (8.1 fps) | 122.9 ms | equal |
| SPI ILI9341 on real hardware | 8.1 fps at 10 MHz, 32.6 fps at 40 MHz (out of spec) | not measured | stays calculated until S4.5 |

The host bus simulation proves the formula and the pacing code, not any SPI driver. No SPI number from a board exists.

## 5 Verdicts on K2, K3, K4, K15 and the S1.3 proposals

- K2 band rendering: CONFIRMED. Band re-run cost is small (section 3). N = full height works but is slower on the F429 when the buffer lives in SDRAM.
  - Proposal (a) band-skip: not measured; stays planned (S5.6).
  - Proposal (c) async flush with double band buffer: low value on the DISCO. The DMA2D flush is about 1.0 ms of a 9.9 ms frame (49 us x 20), and the CPU transpose cannot overlap. Its value is on SPI, where bus time dominates; decide with S4.5 numbers.
  - Proposal (d) band in SRAM, DMA2D copy to SDRAM: CONFIRMED for the portrait-native path (6.6x).
- K3 RGB565 band format: CONFIRMED. Render plus flush fits one panel refresh in the worst scene (14.2 ms of 15.3 ms, landscape), so there is no measured pressure to change the format. An 8-bit indexed band (half the RAM, LTDC L8) stays deferred past v1; it would not shorten SPI bus time (S1.3), and nothing measured argues for it on the DISCO.
- K4 fixed timestep: UNCHANGED, B1 does not test it. New fact for S4.1: the panel refreshes at 65.3 Hz, so a 60 Hz tick drifts against vsync; the catch-up policy is still open.
- K15 integer scale: UNCHANGED, not measured (the prototype has no scale). Calculated: render work follows logical pixels, so 160x120 logical is a quarter of the render work of 320x240, but the flush writes the full 320x240 output, so the 3.9 ms transposing flush on the DISCO does not shrink. Measure in S4.2a.
- R10 portrait panel: landscape 320x240 + CPU transpose costs up to 43% more than portrait + DMA2D (14.2 vs 9.9 ms worst) but still fits one refresh with 1.1 ms (7%) headroom. Portrait + DMA2D leaves 5.4 ms headroom. The platform hook (`PGE_DISP_TRANSPOSED`) already supports both.
- R2 tearing: back buffer + vsync removes it at the cost of 153,600 B SDRAM and quantised frame time. Whether the no-vsync path tears visibly was not recorded.

## 6 Frame-rate targets and defaults (proposals for G3)

Defaults per profile (RAM table, section 3 of research 03; overhead measured on the F429 only, assumed to transfer):

| Profile | Band lines | Reason |
|---|---|---|
| SMALL | 8 | RAM bound; costs up to +35% in sprite-heavy scenes, about +3% in tile scenes |
| MEDIUM | 16 | within 3% (tiles) to 11% (sprites) of 32 lines |
| LARGE | 32 | SRAM band 20,480 B at 320 wide; measured fastest |

First realistic frame-rate targets (K12: every number states what it covers):

| Class | Target | Basis |
|---|---|---|
| F429 landscape (A) with vsync | 30 fps design target (shows as 32.7 fps = 2 refreshes); 65 fps only for light scenes | measured: render + flush of the B1 worst scene is 14.2 ms of a 15.3 ms refresh, leaving no room for logic at 65 fps |
| F429 portrait + DMA2D (C) | 60 fps possible | measured: 9.9 ms worst scene, 5.4 ms left for logic |
| ESP32-S3 + SPI ILI9341 | no target before S4.5 | calculated bus ceiling only: 8.1 fps at 10 MHz (in spec), 32.6 fps at 40 MHz (out of spec); 160x120 windowed 1:1 is 32.6 fps at 10 MHz |
| Host | none | not representative |
| Tier B boards | none | untested (K11) |

These cover render plus flush of the B1 scenes only. Game logic, world streaming, audio and USB/SD access will take part of the budget.

## 7 Not measured

- ESP32-S3 anything, SPI ILI9341 on a real board, TE pin.
- Async flush overlap, band-skip, windowed flush, DMA2D alpha blend for transparent sprites, integer scale.
- Game logic, chunk loading, audio and USB contention on SDRAM bandwidth.
- Variance across runs; no-vsync tearing record; cause of the memcpy anomaly.
- A full-height buffer in internal SRAM on the F429 (only SDRAM was tried).

## 8 Gate G3 questions for the user

1. Confirm K2, K3 (RGB565), K4, K15 as stated in section 5 (K15 measured in S4.2a).
2. F429 default orientation: landscape 320x240 + CPU transpose (recommended: same logical size and game code as the ESP32-S3 port) or portrait-native + DMA2D.
3. Make the vsync back buffer the F429 default (recommended yes, +153,600 B SDRAM)?
4. Accept the default band lines per profile and the first targets in section 6?
