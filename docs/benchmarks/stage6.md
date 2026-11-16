# Stage 6 benchmark report: sandbox game on the F429 (session S6.2b, 2026-11-16)

## Summary for later sessions

- Data: sandbox autoplay benchmark on the STM32F429I-DISCO (180 MHz, Release, logical 320x240 landscape + CPU transpose flush, RGB565, LARGE profile = band 32 lines, 60 Hz tick, vsync back buffer). Firmware `pge_f429_sandbench` = `examples/f429/sandbox_main.cpp` + `examples/sandbox/game.h` autoplay. Raw output: `docs/benchmarks/raw/f429-sandbox.txt`. One run, 60 warm-up + 1800 samples, no variance figure.
- Timing [measured]: avg tick 430 us (max 1,362 us), band 13,311 us (max 13,361), flush 14,595 us, frame 27,906 us (max 29,169). ticks_dropped 0, tick_overruns 0, draw_dropped 0.
- Tick share [calculated]: 430 / 16,667 = 2.6 % of the tick period, max 1,362 / 16,667 = 8.2 %. The tick average includes 7 entities (physics + AI) and the autoplay bot; no separate entity tile-sweep figure was measured.
- Frame rate [calculated from frame_us, the HUD fps was not recorded]: 1e6 / 27,906 = about 35.8 frames/s in vsync mode (same vsync effect as stage4.md section 3).
- Chunk generation [measured]: n=128 chunks, avg 757 us, max 1,100 us (= 6.6 % of one tick period, calculated). Deeper rows cost more: row 0 avg 165 us, row 1 684 us, rows 2 and 3 about 1,090 us.
- Decision (based on these numbers): keep `PGE_MAX_CHUNK_LOADS_PER_TICK` = 1. The max tick of 1,362 us already includes one load and is far below the 16,667 us budget; raising it is not needed.
- Memory [measured]: size text 67,172 / data 39,384 / bss 79,232 B (playable build text 65,664 B). RAM used 118,608 of 196,608 B incl. 8,708 B linker-reserved. Init arena 58,176 of 65,536 B high-water measured on the board (host build predicted the same value), 7,360 B free (about 3 more 2 KiB cache slots; kept 12). Stack high-water 1,168 of 86,704 B (painted-RAM scheme, may read high if malloc is used). Change log 8/2048, entity pool high 7/32, chunk cache 12 slots.
- Compared with the stage4 platformer (run A, vsync): tick 430 vs 13 us, band 13,311 vs 12,494 us, frame 27,906 vs 28,666 us.
- Not done: no save media on the board (no diskio / block device; storage stubs return Io, so SAVE / LOAD show NO STORAGE; USB MSC not implemented); save / load verified on host only; no no-vsync run; one run only; S5.5 audio board check not done in this session; ESP32-S3 deferred.
- Gate G6 (user plays the playable build `pge_f429_sandbox`, flashed on the board now): pending user. No result is claimed.

## 1 Setup and method

- Program: `examples/sandbox/game.h` (platform-free) with the F429 main `examples/f429/sandbox_main.cpp`. Bench firmware `pge_f429_sandbench` runs the game autoplay; playable firmware is `pge_f429_sandbox`.
- Bench: 60 warm-up ticks, then 1800 samples, one per tick, each copying the engine `Stats` of the last render pass (same method as stage4.md section 1). Stats definitions as in stage4.md: `band_us` = sum of `onRender` over all bands, `frame_us` = whole render pass, `flush_us` = `frame_us - band_us` (pre-clear + flush + transposing copy + wait for the vsync latch), `tick_us` = tick callback.
- Configuration as given for the session: Release, 180 MHz, vsync back buffer on, LARGE profile (band 32 lines, band buffer 320 x 32 x 2 = 20,480 B), landscape 320x240 + CPU transpose. Timing source DWT cycle counter via `pge_hal_time_us`.
- Raw log line `arena init hw=58176 cap=65536 band=20480 | frame cap=4096` and `world cache_slots=12 loaded=12 chunks=32x4` give the arena and world setup.
- Limits: one run, no variance; numbers are stats of the last pass, not a per-pass trace; the stack figure comes from the painted-RAM scheme.

## 2 Results

Averages and maxima over 1800 samples, microseconds [measured 2026-11-16, STM32F429I-DISCO, 180 MHz, Release, band 32, landscape + CPU transpose, vsync back buffer]:

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

Chunk generation [measured, `PGE_MAX_CHUNK_LOADS_PER_TICK` = 1]:

| Row | n | avg us | max us |
|---|---|---|---|
| all | 128 | 757 | 1,100 |
| 0 | 32 | 165 | 172 |
| 1 | 32 | 684 | 981 |
| 2 | 32 | 1,091 | 1,100 |
| 3 | 32 | 1,088 | 1,098 |

## 3 Calculated figures

| Figure | Formula | Value | Tag |
|---|---|---|---|
| Tick share (avg) | 430 / 16,667 | 2.6 % | calculated |
| Tick share (max) | 1,362 / 16,667 | 8.2 % | calculated |
| Frames/s with vsync | 1e6 / 27,906 | about 35.8 | calculated; HUD fps not recorded |
| Max chunk generation share of one tick period | 1,100 / 16,667 | 6.6 % | calculated |
| Band difference to stage4 platformer | 13,311 - 12,494 | +817 us | calculated |
| Frame difference to stage4 platformer | 27,906 - 28,666 | -760 us | calculated |
| Free init arena | 65,536 - 58,176 | 7,360 B | calculated |

- Vsync effect: as in stage4.md section 3, the render pass is longer than one panel refresh (15.3 ms), so flush_us includes the wait for the second vblank latch; frame is about 27.9 ms [calculated reasoning, not separately measured here]. The 30 fps F429 landscape target of stage3.md section 6 is met in this mode (35.8 > 30).
- Chunk generation is the largest single cost in a tick: the max tick of 1,362 us is consistent with one load of up to 1,100 us plus the normal 430 us average work [calculated reasoning, not separately measured]. Even then it uses 8.2 % of the tick period, so one load per tick is enough.
- Decision [based on the numbers above]: `PGE_MAX_CHUNK_LOADS_PER_TICK` stays 1.
- Entity tile-sweep CPU is not separately measured. The tick avg of 430 us includes 7 entities (physics + AI) and the autoplay bot; there is no separate figure.

## 4 Memory and flash

arm-none-eabi-size, Release, bytes [measured]:

| Firmware | text | data | bss |
|---|---|---|---|
| pge_f429_sandbench | 67,172 | 39,384 | 79,232 |
| pge_f429_sandbox (playable) | 65,664 | not recorded | not recorded |

Board counters from the raw log [measured]:

| Item | Value |
|---|---|
| RAM used | 118,608 of 196,608 B, incl. 8,708 B linker-reserved |
| Init arena | high-water 58,176 of 65,536 B (host build predicted the same value); 7,360 B free = about 3 more 2 KiB cache slots; cache kept at 12 slots |
| Frame arena | high-water 0 of 4,096 B |
| Stack | high-water 1,168 of 86,704 B (painted-RAM scheme, may read high if malloc is used) |
| Change log | 8 of 2,048 |
| Entity pool | high 7 of 32 |

- The raw log prints data = 39,376 B at run time while the size output says 39,384 B; the 8 B difference was not analysed.
- The vsync back buffer lives in SDRAM and is not in this table (+153,600 B, S3.2b).

## 5 Not done and limits

- No save media on the board: there is no diskio / block device, the storage stubs return Io, so SAVE and LOAD show NO STORAGE. USB MSC is not implemented. Save / load is verified on host only.
- No no-vsync run; one run only, no variance.
- S5.5 audio board check was not done in this session.
- ESP32-S3 deferred (S4.5).
- Entity tile-sweep CPU not measured separately (see section 3).
- HUD fps on screen not recorded.

## 6 Gate G6 status

- Play check on the board (playable firmware `pge_f429_sandbox`, flashed on the board now): pending user, G6. No result is claimed here.
- After the user answers, the result goes into the STATUS.md session table and decision log.

## 7 Open items

- Gate G6 by the user.
- Save media on the board (diskio / block device or USB MSC), then on-board save / load check.
- S5.5 audio board check (MAX98357A on PE4/PE5/PE6).
- Optional: no-vsync run and a repeat for variance.
