# Benchmarks

> **Summary for later sessions**
> - Three board benchmarks exist, all on the STM32F429I-DISCO (180 MHz, Release): B1 scenes (stage 3), platformer autoplay (stage 4), sandbox autoplay (stage 6). A host B1 run exists but is "host, not representative".
> - Worst B1 scene on the board: 14.2 ms landscape + CPU transpose, 9.9 ms portrait + DMA2D; panel refresh 15,306 us = 65.3 Hz [measured] ([stage3.md section 2](../benchmarks/stage3.md)).
> - Platformer frame: 28,666 us with vsync back buffer, 17,419 us without [measured] ([stage4.md section 2](../benchmarks/stage4.md)). Sandbox frame: 27,906 us with vsync [measured] ([stage6.md section 2](../benchmarks/stage6.md)).
> - Every board number is one run, no variance figure. No audio benchmark, no ESP32-S3 number, no on-board save exists.
> - Tag rule (K12 in PLAN.md): [measured] = board, build flags, date; [calculated] = formula shown.

Sources: [stage3.md](../benchmarks/stage3.md), [stage4.md](../benchmarks/stage4.md), [stage6.md](../benchmarks/stage6.md), and the raw files [host.txt](../benchmarks/raw/host.txt), [f429.txt](../benchmarks/raw/f429.txt), [f429-platformer.txt](../benchmarks/raw/f429-platformer.txt), [f429-sandbox.txt](../benchmarks/raw/f429-sandbox.txt).

## 1 What was measured

| Benchmark | Where | What it covers | Source |
|---|---|---|---|
| B1 on host | desktop CPU, null display, Release | 8 render items, band 8 / 16 / 32 lines; slow-bus simulation | [host.txt](../benchmarks/raw/host.txt), [stage3.md section 4](../benchmarks/stage3.md) |
| B1 on the F429, display variants A / B / C / D | STM32F429I-DISCO, 180 MHz, Release -O2 | render plus flush of 8 items, band size, full-height buffer, DMA2D, vsync | [stage3.md](../benchmarks/stage3.md), [f429.txt](../benchmarks/raw/f429.txt) |
| Platformer autoplay | STM32F429I-DISCO, LARGE profile, band 32 | tick, band, flush, frame over 1800 samples, with and without vsync | [stage4.md](../benchmarks/stage4.md), [f429-platformer.txt](../benchmarks/raw/f429-platformer.txt) |
| Sandbox autoplay | STM32F429I-DISCO, LARGE profile, band 32, vsync | tick, band, flush, frame, chunk generation, memory counters | [stage6.md](../benchmarks/stage6.md), [f429-sandbox.txt](../benchmarks/raw/f429-sandbox.txt) |
| Audio benchmark | none | no benchmark file exists; the S5.5 audio board check was not done in S6.2b | [stage6.md section 5](../benchmarks/stage6.md) |

B1 items: clear, flush_only, 100 opaque sprites, 100 transparent sprites, tiles8 and tiles16 static, tiles8 and tiles16 scroll. Logical screen 320x240 RGB565 (landscape) or 240x320 (portrait) ([stage3.md section 1](../benchmarks/stage3.md)).

Display variants on the F429 ([stage3.md section 1](../benchmarks/stage3.md)):

- A = landscape, CPU transposing copy.
- B = portrait-native, CPU memcpy.
- C = portrait-native, DMA2D copy.
- D = A plus back buffer and vsync (D numbers show the panel refresh period, not render cost).

## 2 Host B1 bench

Tag: host, not representative (desktop CPU, not an MCU). Build Release, logical 320x240 RGB565, 200 frames per item, no window ([host.txt](../benchmarks/raw/host.txt) header).

| Item (band 16) | us per frame | Tag |
|---|---|---|
| clear | 8 | [measured] host, Release, null display, [host.txt](../benchmarks/raw/host.txt) |
| flush_only | 4 | [measured] host, Release, null display, [host.txt](../benchmarks/raw/host.txt) |
| sprites_opaque | 68 | [measured] host, Release, null display, [host.txt](../benchmarks/raw/host.txt) |
| tiles8_scroll | 118 | [measured] host, Release, null display, [host.txt](../benchmarks/raw/host.txt) |

- Host numbers are 100x or more faster than the board and say nothing about MCU speed. They are used only for the slow-bus check ([stage3.md section 1](../benchmarks/stage3.md)).
- Slow-bus simulation, 40 MHz 1-bit, 320x240, band 16: 30.72 ms/frame [calculated], 30.73 ms [measured] on the host bus simulation ([stage3.md section 4](../benchmarks/stage3.md)).
- Slow bus 10 MHz (ILI9341 limit): 122.88 ms/frame (8.1 fps) [calculated], 122.9 ms [measured] on the host bus simulation ([stage3.md section 4](../benchmarks/stage3.md)).
- The host bus simulation proves the formula and the pacing code, not any SPI driver. No SPI number from a board exists ([stage3.md section 4](../benchmarks/stage3.md)).

## 3 F429 B1 bench, display variants

Setup: STM32F429I-DISCO, 180 MHz, arm-none-eabi-g++ 10.3.1, Release -O2, `-fno-exceptions -fno-rtti`, DWT cycle counter, 30 timed frames per item, one run per variant, [measured 2026-11-15] ([stage3.md section 1](../benchmarks/stage3.md), [f429.txt](../benchmarks/raw/f429.txt) header). Flush is synchronous (DMA2D polled), so no CPU/DMA overlap is in any number.

### 3.1 Per-frame time, band 16 lines, band buffer in SRAM

Milliseconds [measured 2026-11-15, STM32F429I-DISCO, Release -O2] ([stage3.md section 2](../benchmarks/stage3.md)):

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

Worst B1 scene (tiles8 scroll, 2 layers, band 16 in SRAM) [measured]: 14.2 ms (A), 15.4 ms (B), 9.9 ms (C) ([stage3.md summary](../benchmarks/stage3.md)).

### 3.2 Panel refresh and tearing

- Panel refresh: 15,306 us = 65.3 Hz [measured] ([stage3.md summary](../benchmarks/stage3.md), [f429.txt](../benchmarks/raw/f429.txt) header). The earlier assumption of 60 Hz was calculated; the panel is 65.3 Hz ([stage3.md section 4](../benchmarks/stage3.md)).
- Variant D (back buffer + vsync): constant 15,306 us frame (65.3 fps, 32.7 fps if a frame misses one refresh), user saw no tear line [measured]. Costs 153,600 B SDRAM ([stage3.md summary](../benchmarks/stage3.md)).
- Whether the no-vsync path tears visibly was not recorded ([stage3.md section 5](../benchmarks/stage3.md)).

### 3.3 DMA2D versus CPU copy

Flush of one band alone, us [measured, 100 reps] ([stage3.md section 2](../benchmarks/stage3.md)):

| Band | A 320 wide, transposing | B 240 wide, memcpy | C 240 wide, DMA2D |
|---|---|---|---|
| 8 lines | 138 | 162 | 25 |
| 16 lines | 261 | 324 | 49 |
| 32 lines | 507 | 647 | 97 |

- DMA2D band copy is 6.6x faster than CPU memcpy: 324 / 49.1 = 6.6x [calculated from the table above] ([stage3.md section 2](../benchmarks/stage3.md)).
- Rates [calculated]: A 10,240 B / 261 us = 39.2 MB/s; B 7,680 B / 324 us = 23.7 MB/s; C 7,680 B / 49.1 us = 156 MB/s ([stage3.md section 2](../benchmarks/stage3.md)).
- DMA2D copy runs at 156 MB/s, at the SDRAM peak of 160 MB/s (S1.2) ([stage3.md summary](../benchmarks/stage3.md)).
- Fill of the whole 240x320 panel (153,600 B), [measured]: CPU 1049 us (146 MB/s), DMA2D 965 us (159 MB/s); DMA2D fill gains 8%, because SDRAM bandwidth is the limit ([stage3.md section 2](../benchmarks/stage3.md)).
- DMA2D cannot transpose, so the fast path needs a portrait-native logical screen. Landscape 320x240 stays on the CPU transpose, 3.9 ms per frame at band 16 [261 us x 15 bands, calculated] ([stage3.md sections 2 and summary](../benchmarks/stage3.md)).
- Anomaly, not investigated: portrait CPU memcpy to SDRAM is slower per byte (23.7 MB/s) than the transposing copy (39.2 MB/s) ([stage3.md summary](../benchmarks/stage3.md)).

### 3.4 Band size (R1) and full-height buffer

Variant A, ms per frame [measured] ([stage3.md section 3](../benchmarks/stage3.md)):

| Item | 8 lines (5,120 B) | 16 lines (10,240 B) | 32 lines (20,480 B) | 240 lines in SDRAM (153,600 B) |
|---|---|---|---|---|
| flush_only | 5.05 | 4.81 | 4.69 | 13.23 |
| sprites_opaque | 9.47 | 7.82 | 7.04 | 15.16 |
| tiles8_static | 13.87 | 13.60 | 13.48 | 23.46 |
| tiles8_scroll | 15.01 | 14.20 | 13.81 | 23.54 |

- Overhead against 32 lines [calculated]: tiles8_static +2.9% (8) / +0.9% (16); tiles8_scroll +8.7% / +2.8%; sprites_opaque +34.5% / +11.0%. Sprite cost grows because every sprite is clipped against every band ([stage3.md section 3](../benchmarks/stage3.md)).
- Full height in SDRAM against 16 lines in SRAM [calculated]: sprites 1.94x, tiles8 static 1.72x, tiles8 scroll 1.66x, flush_only 2.75x. The cause (slow SDRAM) is assumed, not isolated ([stage3.md section 3](../benchmarks/stage3.md)).
- Conclusion: use banded SRAM buffers on the F429 even in the LARGE profile ([stage3.md summary](../benchmarks/stage3.md)).

### 3.5 Headroom against one refresh

- Landscape (A) worst scene: 14.2 ms of a 15.3 ms refresh, 1.1 ms (7%) headroom [calculated from the measured values] ([stage3.md section 5](../benchmarks/stage3.md)).
- Portrait + DMA2D (C) worst scene: 9.9 ms, 5.4 ms headroom [calculated] ([stage3.md sections 5 and 6](../benchmarks/stage3.md)).

## 4 Platformer on the F429

Setup: STM32F429I-DISCO, 180 MHz, Release, arm-none-eabi-gcc 10.3.1, logical 320x240 landscape + CPU transpose flush, RGB565, LARGE profile (band 32 lines), scale 1, 60 Hz tick. Autoplay bot walks the whole world; 1800 samples after a 60-tick warm-up; one run per mode ([stage4.md summary and section 1](../benchmarks/stage4.md), [f429-platformer.txt](../benchmarks/raw/f429-platformer.txt)).

Build flags: `-O2 -g -O3 -DNDEBUG`; the last `-O3` wins, so the effective level is -O3. Whether the S3 numbers had the same flags was not verified ([stage4.md summary](../benchmarks/stage4.md)).

Microseconds [measured 2026-11-15, STM32F429I-DISCO, 180 MHz, Release, band 32, landscape + CPU transpose] ([stage4.md section 2](../benchmarks/stage4.md)):

| Quantity | A: vsync back buffer | B: no vsync |
|---|---|---|
| tick avg | 13 | 13 |
| tick max | 19 | 19 |
| band avg | 12,494 | 12,504 |
| band max | 12,642 | 12,653 |
| flush avg (pre-clear + flush + wait) | 16,172 | 4,915 |
| frame avg | 28,666 | 17,419 |
| frame max | 29,547 | 17,579 |
| ticks_dropped / tick_overruns / draw_dropped | 0 / 0 / 0 | 0 / 0 / 0 |

Calculated figures ([stage4.md section 3](../benchmarks/stage4.md)):

| Figure | Formula | Value | Tag |
|---|---|---|---|
| Frames/s without vsync | 1e6 / 17,419 | 57.4 | [calculated] |
| Frames/s with vsync | 1e6 / 28,666 | 34.9 | [calculated]; the HUD fps was not recorded |
| Tick share of the tick period | 13 / 16,667 | 0.08% | [calculated] |
| Extra flush time with vsync | 16,172 - 4,915 | 11,257 us | [calculated] |
| Two refresh periods | 2 x 15,306 | 30,612 us | [calculated] |

- Game logic is negligible; render is the cost: band 12.5 ms + flush 4.9 ms ([stage4.md summary](../benchmarks/stage4.md)).
- Why flush grows with vsync: a render pass longer than one panel refresh (17.4 ms > 15.3 ms) waits for the second vblank latch [calculated reasoning, not separately measured] ([stage4.md section 3](../benchmarks/stage4.md)).
- The 30 fps F429 landscape target (stage3.md section 6) is met in both modes [calculated]. The 65 fps light-scene case is not met by this scene ([stage4.md section 3](../benchmarks/stage4.md)).
- Total frame 17.4 ms exceeds the 15.3 ms refresh by 2.1 ms, so unlike the B1 worst scene (14.2 ms) this scene does not fit one refresh in landscape [calculated] ([stage4.md section 4](../benchmarks/stage4.md)).

### 4.1 Platformer memory and flash

arm-none-eabi-size, Release, bytes [measured] ([stage4.md section 6](../benchmarks/stage4.md)):

| Firmware | text | data | bss |
|---|---|---|---|
| pge_f429_platformer | 43,468 | 40,616 | 79,232 |
| pge_f429_platbench | 43,864 | 40,616 | 79,232 |
| pge_f429_platbench_nobb | 43,872 | 40,616 | 79,232 |

- bss 79,232 B contains the two static engine arenas (65,536 + 4,096 B per S4.1) [calculated from S4.1] ([stage4.md section 6](../benchmarks/stage4.md)).
- The vsync back buffer lives in SDRAM and is not in the table: +153,600 B (S3.2b) ([stage4.md section 6](../benchmarks/stage4.md)).

## 5 Sandbox on the F429

Setup: STM32F429I-DISCO, 180 MHz, Release, logical 320x240 landscape + CPU transpose flush, RGB565, LARGE profile (band 32 lines), 60 Hz tick, vsync back buffer. Firmware `pge_f429_sandbench`; 60 warm-up + 1800 samples, one run ([stage6.md summary and section 1](../benchmarks/stage6.md), [f429-sandbox.txt](../benchmarks/raw/f429-sandbox.txt)).

Microseconds [measured 2026-11-16, STM32F429I-DISCO, 180 MHz, Release, band 32, landscape + CPU transpose, vsync back buffer] ([stage6.md section 2](../benchmarks/stage6.md)):

| Quantity | Sandbox | Stage4 platformer run A (for comparison) |
|---|---|---|
| tick avg | 430 | 13 |
| tick max | 1,362 | 19 |
| band avg | 13,311 | 12,494 |
| band max | 13,361 | 12,642 |
| flush avg (pre-clear + flush + wait) | 14,595 | 16,172 |
| frame avg | 27,906 | 28,666 |
| frame max | 29,169 | 29,547 |
| ticks_dropped / tick_overruns / draw_dropped | 0 / 0 / 0 | 0 / 0 / 0 |

Calculated figures ([stage6.md section 3](../benchmarks/stage6.md)):

| Figure | Formula | Value | Tag |
|---|---|---|---|
| Tick share (avg) | 430 / 16,667 | 2.6 % | [calculated] |
| Tick share (max) | 1,362 / 16,667 | 8.2 % | [calculated] |
| Frames/s with vsync | 1e6 / 27,906 | about 35.8 | [calculated]; HUD fps not recorded |
| Max chunk generation share of one tick period | 1,100 / 16,667 | 6.6 % | [calculated] |
| Band difference to stage4 platformer | 13,311 - 12,494 | +817 us | [calculated] |
| Frame difference to stage4 platformer | 27,906 - 28,666 | -760 us | [calculated] |
| Free init arena | 65,536 - 58,176 | 7,360 B | [calculated] |

- The 30 fps F429 landscape target of stage3.md section 6 is met in this mode (35.8 > 30) [calculated] ([stage6.md section 3](../benchmarks/stage6.md)).
- The tick average includes 7 entities (physics + AI) and the autoplay bot; no separate entity tile-sweep figure was measured ([stage6.md section 3](../benchmarks/stage6.md)).

### 5.1 Chunk generation

[measured] on the board, `PGE_MAX_CHUNK_LOADS_PER_TICK` = 1 ([stage6.md section 2](../benchmarks/stage6.md), [f429-sandbox.txt](../benchmarks/raw/f429-sandbox.txt)):

| Row | n | avg us | max us |
|---|---|---|---|
| all | 128 | 757 | 1,100 |
| 0 | 32 | 165 | 172 |
| 1 | 32 | 684 | 981 |
| 2 | 32 | 1,091 | 1,100 |
| 3 | 32 | 1,088 | 1,098 |

- Decision based on these numbers: keep `PGE_MAX_CHUNK_LOADS_PER_TICK` = 1; the max tick of 1,362 us already includes one load and is far below the 16,667 us budget ([stage6.md summary](../benchmarks/stage6.md)).

### 5.2 Sandbox memory and flash

arm-none-eabi-size, Release, bytes [measured] ([stage6.md section 4](../benchmarks/stage6.md)):

| Firmware | text | data | bss |
|---|---|---|---|
| pge_f429_sandbench | 67,172 | 39,384 | 79,232 |
| pge_f429_sandbox (playable) | 65,664 | not recorded | not recorded |

Board counters from the raw log [measured] ([stage6.md section 4](../benchmarks/stage6.md), [f429-sandbox.txt](../benchmarks/raw/f429-sandbox.txt)):

| Item | Value |
|---|---|
| RAM used | 118,608 of 196,608 B, incl. 8,708 B linker-reserved |
| Init arena | high-water 58,176 of 65,536 B (host build predicted the same value); 7,360 B free = about 3 more 2 KiB cache slots; cache kept at 12 slots |
| Frame arena | high-water 0 of 4,096 B |
| Stack | high-water 1,168 of 86,704 B (painted-RAM scheme, may read high if malloc is used) |
| Change log | 8 of 2,048 |
| Entity pool | high 7 of 32 |

- Band buffer 320 x 32 x 2 = 20,480 B [calculated] ([stage6.md section 1](../benchmarks/stage6.md)).
- The raw log prints data = 39,376 B at run time while the size output says 39,384 B; the 8 B difference was not analysed ([stage6.md section 4](../benchmarks/stage6.md)).
- The vsync back buffer lives in SDRAM and is not in the table: +153,600 B (S3.2b) ([stage6.md section 4](../benchmarks/stage6.md)).

## 6 Audio

No audio benchmark exists. The S5.5 audio board check (MAX98357A on PE4/PE5/PE6) was not done in S6.2b and is still open ([stage6.md sections 5 and 7](../benchmarks/stage6.md)). Audio is also listed as not measured in stage 3 ([stage3.md section 7](../benchmarks/stage3.md)).

## 7 Frame-rate targets (proposals for G3)

Targets (render plus flush of the B1 scenes only; game logic, world streaming, audio and USB/SD access will take part of the budget) ([stage3.md section 6](../benchmarks/stage3.md)):

- F429 landscape (A) with vsync: 30 fps design target (shows as 32.7 fps = 2 refreshes); 65 fps only for light scenes.
- F429 portrait + DMA2D (C): 60 fps possible, 9.9 ms worst scene, 5.4 ms left for logic.
- ESP32-S3 + SPI ILI9341: no target before S4.5; calculated bus ceiling only, 8.1 fps at 10 MHz (in spec), 32.6 fps at 40 MHz (out of spec).
- Host and Tier B boards: no target (host not representative; Tier B untested).

## 8 Limits and not measured

- One run per variant or mode, no variance figure, on every board number ([stage3.md section 1](../benchmarks/stage3.md), [stage4.md section 1](../benchmarks/stage4.md), [stage6.md section 5](../benchmarks/stage6.md)).
- B1: 30 frames per item; the prototype has no tile transparency, sprites are 8-bit indexed with palette, no scaling; render plus flush only, no game logic, world, entities or audio ([stage3.md section 1](../benchmarks/stage3.md)).
- Host numbers are "host, not representative" ([host.txt](../benchmarks/raw/host.txt) header).
- No ESP32-S3 number, no SPI ILI9341 number from a real board, no TE pin ([stage3.md section 7](../benchmarks/stage3.md)).
- Not measured in stage 3: async flush overlap, band-skip, windowed flush, DMA2D alpha blend, integer scale (K15), a full-height buffer in internal SRAM, cause of the memcpy anomaly ([stage3.md section 7](../benchmarks/stage3.md)).
- Platformer: K15 scale cost, portrait + DMA2D with the platformer, HUD fps on screen, per-band split of tiles vs sprite vs text not measured; the numbers are stats of the last pass, not a per-pass trace ([stage4.md section 7](../benchmarks/stage4.md)).
- Sandbox: no save media on the board (no diskio / block device; storage stubs return Io, so SAVE and LOAD show NO STORAGE; USB MSC not implemented); save / load verified on host only; no no-vsync run; entity tile-sweep CPU not measured separately; HUD fps not recorded ([stage6.md section 5](../benchmarks/stage6.md)).
- Gate G4 (platformer play check and tearing by eye) and gate G6 (sandbox play check) are pending the user; no result is claimed ([stage4.md section 8](../benchmarks/stage4.md), [stage6.md section 6](../benchmarks/stage6.md)).
