# Stage 4 benchmark report: platformer on the F429 (session S4.4, 2026-11-15)

## Summary for later sessions

- Data: platformer autoplay benchmark on the STM32F429I-DISCO (180 MHz, Release, arm-none-eabi-gcc 10.3.1, logical 320x240 landscape + CPU transpose flush, RGB565, LARGE profile = band 32 lines, scale 1, 60 Hz tick). Raw output: `docs/benchmarks/raw/f429-platformer.txt`. One run per mode, 1800 samples each, no variance figure.
- Build flags in effect [verified in build-f429/build.ninja]: `-O2 -g -O3 -DNDEBUG`. The last `-O3` wins, so the effective level is -O3, not -O2 (the toolchain file sets -O2, CMake appends its own -O3). stage3.md and raw/f429.txt say -O2 for the S3 numbers; whether they had the same flags was not verified.
- Run A, vsync back buffer [measured]: avg tick 13 us, band 12,494 us, flush 16,172 us, frame 28,666 us (max frame 29,547 us).
- Run B, no vsync, writes the displayed buffer [measured]: avg tick 13 us, band 12,504 us, flush 4,915 us, frame 17,419 us (max frame 17,579 us).
- Both runs: ticks_dropped 0, tick_overruns 0, draw_dropped 0.
- Game logic is negligible [calculated]: tick 13 us = 0.08% of the 16,667 us tick period (collision + controller + bot). Render is the cost: band 12.5 ms + flush 4.9 ms.
- Frame rate [calculated from frame_us, the on-screen HUD fps was not recorded]: about 57 frames/s without vsync (1e6/17,419), about 35 frames/s with the vsync back buffer (1e6/28,666). The 30 fps F429 landscape target (stage3.md section 6) is met in both modes.
- Why flush grows by 11.3 ms with vsync [calculated reasoning, not separately measured]: a render pass longer than one panel refresh (17.4 ms > 15.3 ms) waits for the second vblank latch.
- Collision module `core/include/pge/collision.h`: `moveAndCollide` on a `TileGrid`, integer pixels, X then Y, no tunnelling, solid / one-way / hazard flags. Deviations from 03 section 3.1: ints not Fix, TileGrid instead of World/Body. World overload comes with S5.1a.
- Host tests 10/10 passed, incl. 20,000 random collision cases against a 1-pixel-step oracle and 4000 autoplay ticks.
- Firmware sizes [measured, arm-none-eabi-size]: platformer text 43,468 / data 40,616 / bss 79,232 B.
- Not measured: K15 integer scale (the platformer uses scale 1), portrait + DMA2D with the platformer, ESP32-S3 (S4.5), HUD fps on screen, tiles-vs-sprite split of the band cost.
- Gate G4 (play on the board, tearing by eye): pending user, G4. No result is claimed here.
- Open for later: K15 scale cost still not measured (decide in S4.5 / S5.6 or by a small bench); `game.h` is platform-free so S4.5 reuses it unchanged.

## 1 Setup and method

- Program: `examples/platformer/game.h` (249 lines, platform-free, uses `PGE_LOGICAL_W/H`, no platform include) with the F429 main `examples/f429/platformer_main.cpp`. World 80x30 tiles of 16x16 (1280x480 px), two transparent parallax tile layers (near and far), one player sprite, HUD text.
- Bench mode (`PGE_PLATFORMER_BENCH=1`): a scripted bot walks the whole world, so the camera scrolls. It runs 1860 ticks: a 60-tick warm-up, then 1800 samples, one per tick. Each sample copies the engine `Stats` of the last render pass at that moment. If no render pass finished between two ticks, the previous pass is sampled again.
- Stats definitions (`engine.h`): `band_us` = sum of `onRender` time over all bands; `frame_us` = the whole render pass; `flush_us` = `frame_us - band_us` = pre-clear + flush + transposing copy + (run A only) the wait for the vsync latch; `tick_us` = the tick callback.
- Timing: DWT cycle counter through `pge_hal_time_us` (as in stage 3). Flush is synchronous (CPU transpose into the LTDC framebuffer).
- Configuration verified from the files, not assumed:
  - `platform/stm32f429i_disco/pge_profile.h`: `PGE_PROFILE_LARGE 1`, `PGE_LOGICAL_W 320`, `PGE_LOGICAL_H 240`.
  - `core/include/pge/config.h`: LARGE gives `PGE_BAND_LINES` 32 (band buffer 320 x 32 x 2 = 20,480 B, calculated), `PGE_SCALE` 1, `PGE_TICK_HZ` 60, `PGE_MAX_CATCHUP` 5.
  - `build-f429/CMakeCache.txt`: Release, `PGE_F429_ROT=0`; `build.ninja` compile flags: `-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard -fno-exceptions -fno-rtti -O2 -g -O3 -DNDEBUG -std=c++17`.
  - `platform/stm32f429i_disco/CMakeLists.txt`: `pge_f429_platbench` = bench 1 + backbuf 1, `pge_f429_platbench_nobb` = bench 1 + backbuf 0, `pge_f429_platformer` = bench 0 + backbuf 1 (playable).
- Limits: one run per mode, no variance; the numbers are stats of the last pass, not a per-pass trace; the HUD fps value was not recorded; the band buffer is the engine init arena in internal SRAM (static bss), checked by the size output only, not in the map.

## 2 Results

Averages and maxima over 1800 samples, microseconds [measured 2026-11-15, STM32F429I-DISCO, 180 MHz, Release flags in section 1, band 32, landscape 320x240 + CPU transpose]:

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

Panel refresh [measured S3.2b]: 15,306 us (65.3 Hz).

## 3 Calculated figures

| Figure | Formula | Value | Tag |
|---|---|---|---|
| Frames/s without vsync | 1e6 / 17,419 | 57.4 | calculated |
| Frames/s with vsync | 1e6 / 28,666 | 34.9 | calculated; the HUD fps was not recorded |
| Tick share of the tick period | 13 / 16,667 | 0.08% | calculated |
| Extra flush time with vsync | 16,172 - 4,915 | 11,257 us | calculated |
| Two refresh periods | 2 x 15,306 | 30,612 us | calculated; run A frame avg 28,666 us is below it |

- Reading (calculated reasoning, not separately measured): with the back buffer, `flush_us` includes the wait for the vblank latch. Run B's 17.4 ms pass is longer than one refresh (15.3 ms), so in run A the pass spans two refreshes and the wait explains the +11.3 ms.- The render rate is also capped by the 60 Hz tick (at most one render pass per loop pass, only after a tick). Run B at 57 frames/s is close to that cap; at 35 frames/s in run A two ticks run per pass and the catch-up policy absorbs it (no drops, no overruns).
- Design target of stage3.md section 6 (F429 landscape, 30 fps): met in both modes [calculated from the numbers above]. The 65 fps "light scene" case is not met by this scene.

## 4 Comparison with S3.4 B1

Comparison is indicative only; the scenes differ.

| Item | S3.4 B1 tiles8 scroll [measured] | S4.4 platformer run B [measured] |
|---|---|---|
| Frame (render + flush) | 14.20 ms | 17.42 ms |
| flush_only (pre-clear + flush, empty scene) | 4.81 ms | flush avg 4.92 ms |
| Render part | 9.39 ms (calculated: 14.20 - 4.81) | band avg 12.50 ms |
| Band lines | 16 | 32 |

- The flush part agrees within about 2% (4.81 vs 4.92 ms) although the band size differs (stage3.md section 3: flush_only 4.81 ms at 16 lines, 4.69 ms at 32 lines).
- The platformer renders 3.1 ms more than B1 tiles8 scroll [calculated]. Not isolated; candidates are transparent tile layers (index 0 skipped, B1 layer 2 overdrew), a sprite, and HUD text. A per-component split was not measured.
- Band 32 versus 16 lines: stage3.md section 3 shows 32 lines about 3% faster than 16 for tile scenes on the same board [measured in S3]. Not re-measured here.
- Total frame 17.4 ms exceeds the 15.3 ms refresh by 2.1 ms, so unlike the B1 worst scene (14.2 ms) this scene does not fit one refresh in landscape.

## 5 Collision API and deviations

Module: `core/include/pge/collision.h`, `core/src/physics/collision.cpp` (57 + 95 lines).

- `TileProps{flags, hardness, light}` (03 section 2.4 layout); flags `kTileSolid 0x01`, `kTileOneWay 0x02`, `kTileHazard 0x04`.
- `TileGrid{tiles, w, h, tile_w, tile_h, props[256]}`; `Body{x, y, w, h}` ints; `MoveResult{hit_x, hit_y, on_ground, touched_flags}`; `moveAndCollide`.
- Rules: X axis then Y axis; sub-steps of at most one tile (no tunnelling); one-way tiles block only when moving down with the box bottom at or above the tile top before the step; hazard is reported and never blocks; outside the grid counts as solid in every direction (03: outside = void 255, solid); floor division for negative coordinates.
- Precondition: the box does not start inside a blocking tile. `on_ground` = a 1 px down probe is blocked. `touched_flags` = OR of the flags of tiles overlapped by the final box.
- Deviations from 03 section 3.1: ints instead of `Fix`; `TileGrid` (fixed-size map) instead of `World` / `Body` with kind, vx, vy. The `World` overload or adapter arrives with S5.1a; entity collision S5.2.
- Game-side (not engine): 8.8 fixed-point gravity 0.25, jump -5 px/tick, coyote time 5 ticks, jump buffer 5 ticks, hazard = respawn, player box 12x14.
- Tests [measured on host]: ctest 10/10 passed (version gfx bench core prims sprites camera input collision platformer). `test_collision`: scenarios on 8x8 and 10x12 tiles plus 20,000 random cases compared with an independent 1-pixel-step oracle (position, hit flags, on_ground, touched_flags). Three mutation checks (one-way comparison `<=` to `<`, left snap without +1 tile, one-way rule disabled) each made the test fail. `test_platformer`: 4000 autoplay ticks driven manually, asserts every tick that the player overlaps no solid tile and stays inside the world; player reached x 1220, camera reached its right limit 960, 0 deaths; hand-fed input: Right held 120 ticks moves x 32 to 148, A jump rises 48 px.
- Host demo: `pge_platformer.exe [--auto] [--ticks N] [--shot f.bmp]`; headless shots at 600 and 1500 ticks checked by eye (sky, grass ground, hill steps, spikes, clouds, player sprite, HUD "FPS 61 X ... DEATHS 0"). The first level layout gave 11 bot deaths in 4000 ticks (bot walked off planks next to spikes); the level and the bot's 24 px spike lookahead were adjusted, now 0 deaths.

## 6 Memory and flash

arm-none-eabi-size, Release, bytes [measured]:

| Firmware | text | data | bss |
|---|---|---|---|
| pge_f429_platformer | 43,468 | 40,616 | 79,232 |
| pge_f429_platbench | 43,864 | 40,616 | 79,232 |
| pge_f429_platbench_nobb | 43,872 | 40,616 | 79,232 |

- bss 79,232 B contains the two static engine arenas (65,536 + 4,096 B per S4.1) [calculated from S4.1]. The vsync back buffer lives in SDRAM and is not in this table (+153,600 B, S3.2b).
- What the 40,616 B of data holds (probably game assets in writable memory) was not analysed.

## 7 Not measured

- K15 integer scale cost (the platformer uses scale 1).
- Portrait + DMA2D variant with the platformer.
- ESP32-S3 and SPI ILI9341 (S4.5).
- The HUD fps value on screen.
- Per-band cost split of tiles vs sprite vs text.
- Variance across runs; render rate under real load of USB / audio (not present yet).

## 8 Gate G4 status

- Play check on the board (firmware `pge_f429_platformer`, bench off, back buffer on): pending user, G4.
- Tearing by eye on the board: pending user, G4.
- After the user answers, the result goes into the STATUS.md session table and decision log.

## 9 How to reproduce

Build and flash from the repo root (board needed; build setup in `docs/dev/toolchain.md`, section "STM32F429I-DISCO build"):

```bash
cmake --build build-f429 --target flash_f429_platbench      # run A, vsync back buffer
cmake --build build-f429 --target flash_f429_platbench_nobb # run B, no vsync
cmake --build build-f429 --target flash_f429_platformer     # playable, no bench
```

- The bench runs about 31 s (1860 ticks at 60 Hz). Wait about 50 s after flashing, then read the debug ring buffer `pge_dbg_log` with OpenOCD as described in `docs/dev/toolchain.md` section "STM32F429I-DISCO build" (look up the `pge_dbg_log` and `pge_dbg_pos` addresses with `arm-none-eabi-nm` on the matching `.elf` first). The log ends with `# PLAT DONE`; LED4 lights after a finished bench, LED3 on failure.
- Host: `build/examples/platformer/pge_platformer.exe --auto --ticks 1500 --shot out.bmp`; tests: `ctest` in the host build dir.
