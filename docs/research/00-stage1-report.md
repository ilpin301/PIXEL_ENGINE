# 00 Stage 1 feasibility report (session S1.4, 2026-11-15)

Tags: [verified] source named; [calculated] formula shown; [assumed] judgement or proposal; UNKNOWN = not found. No fps figure below is a promise (K12): every one is a bus ceiling, tagged.

## Summary for later sessions
- First target (K1) CONFIRMED: desktop simulator (SDL2) + STM32F429I-DISCO, then ESP32-S3 + SPI ILI9341 in S4.5. Reasons in section 11.
- No Key decision K1-K15 changed in S1.4. S1.3 amendments to K12/K15 stand (see 03).
- Biggest number: ILI9341 over SPI in spec (10 MHz) caps at 8.1 fps for a full 320x240 RGB565 frame [calculated: 10e6 / 1,228,800 bits]. Breadboard code runs 40 MHz = out of spec [verified, 01 and 03].
- DISCO has no such ceiling: LTDC scans out of SDRAM at 9.2 MB/s of 160 MB/s peak [calculated, 02]. Its panel is portrait 240x320 [verified, 02]; landscape games need a rotate step (new risk R10).
- Neither reference repo has measured numbers [verified, 01]. ESP32-DOOMGAD has no engine code; DISCO DOOM port never ran on hardware.
- Board ownership (Q2): only F429I-DISCO and ESP32-S3 N16R8. Everything else is Tier B, datasheet-only.
- Realistic game scale (section 4, all [assumed] until S3/S6 measure): Class A (F429, S3, H7) small sandbox; Class B (ESP32, RP2350) smaller sandbox with chunk streaming; Class C (RP2040, F401/F411/F446) platformer plus tiny streamed world.
- Band buffer 320x16 RGB565 = 10,240 B; full frame 153,600 B [calculated].
- World sizes [calculated, 1 B/tile, 2 layers]: 256x128 = 65,536 B; 512x256 = 262,144 B; 1024x512 = 1,048,576 B. 16-chunk cache (K8, 2,048 B per chunk) = 32,768 B.
- Spec gap found: enemies/AI, health/damage and crafting are in spec section 3 but in no session. Proposal in section 9; user decides at G1.
- Open items carried: TE pin on user's SPI module; STM32 numbers re-check vs st.com; DISCO audio pin confirm on board; ESP32-DOOMGAD final display interface.
- STATUS.md feature tracker is seeded (every spec section 3/5/6/7 feature has a row).
- Gate G1 (user): approve this report; Q1-Q3 already answered (MIT; two boards; ESP32-S3 uses SPI module).

## 1 Analysis of the two reference repositories (spec item 1)
Full table: `01-reference-repos.md`. Short form:
- ESP32-DOOMGAD [verified, 01]: hardware design plus a breadboard test. ESP32-S3-WROOM-1-N16R8, ILI9341 (SPI on breadboard, 8080 parallel in the hardware doc, conflict unresolved), I2S amp, 8 GPIO buttons, analog stick. One setup()/loop() file; no engine; SPI bench result never stored.
- STM32F429I-DISCO [verified, 01]: CMake + arm-none-eabi, 168 MHz, LTDC RGB565 240x320 in SDRAM with two framebuffers flipped on vsync, USB-stick storage, touch/buttons/stick input, no audio. DMA2D enabled in config but never called. DOOM port never run on hardware.
- Principles reused [assumed]: CMake + arm-none-eabi build; LTDC scan-out from SDRAM; poll-based input behind a mapping table; link-time board binding.
- Not reused: CPU rotate loop per pixel, blocking audio, single-file structure, GPLv2 DOOM tree (K13).
- Absent in both: measured fps, DMA/DMA2D use, game sound, engine architecture.

## 2 Platform comparison (spec item 2)
Full tables with sources: `02-hardware-comparison.md`. Condensed (STM32 values from mirrors, re-check vs st.com):

| Platform | Core / clock | FPU | SRAM | External RAM | Display paths | Tier |
|---|---|---|---|---|---|---|
| ESP32 | 1-2x LX6, up to 240 MHz | yes, precision UNKNOWN | 520 KB | up to 4 MB mapped (QSPI) | SPI, I2S LCD mode | B |
| ESP32-S3 N16R8 | 2x LX7, up to 240 MHz | single | 512 KB | 8 MB octal PSRAM | SPI, LCD_CAM 8080/RGB | A (from S4.5) |
| STM32F429 | M4, 180 MHz (ref repo 168) | single | 256 KB incl. 64 KB CCM | 8 MB SDRAM on DISCO | LTDC RGB, FMC 8080, SPI | A (DISCO) |
| STM32F4 generic | M4, 84-180 MHz | yes | 96-384 KB | none (F469: FMC) | SPI; LTDC only F429/F469 | B |
| STM32H743/H750 | M7, up to 480 MHz | double | 1 MB in 7 domains | FMC | LTDC, SPI up to 133 MHz | B |
| RP2040 | 2x M0+, 133 MHz | none [assumed] | 264 kB | none | SPI, PIO 8080 | B |
| RP2350 | 2x M33, 150 MHz | single | 520 kB | PSRAM 16 MB per QMI CS | SPI, PIO, HSTX | B |

SPI ceilings for 320x240 RGB565 (1,228,800 bits) [calculated, 02]: 10 MHz = 8.1 fps (ILI9341 datasheet limit), 20 MHz = 16.3, 26 MHz = 21.2, 40 MHz = 32.6, 80 MHz = 65.1. Only the 10 MHz row is within the ILI9341 datasheet [verified, 03].

## 3 Reading the board facts that decide the design
- DISCO: 8 MB SDRAM holds a full framebuffer and a whole world; SRAM (192 KB + 64 KB CCM) holds band buffer and hot data [verified, 01/02].
- ESP32-S3: PSRAM DMA is burst-limited and slow for pixel work; band buffers belong in internal RAM [verified, 02]. Two band buffers (draw one, send one) is the intended DMA pattern (S4.5).
- H7: LTDC reaches only AXI SRAM, FMC and QSPI; DTCM is invisible to DMA [verified, 02]. Tier B notes go into the porting guide.
- RP2040: no FPU, so K4 fixed-point is the right choice [assumed].

## 4 Achievable game scale per platform (spec item 3)
All entries [assumed] until S3 (render cost) and S6.2 (whole game) measure them. World sizes [calculated].
- Tile view: 16x16 tiles on 320x240 show 20x15 = 300 tiles per layer; 8x8 tiles show 40x30 = 1,200 per layer [calculated].
- Class A, STM32F429I-DISCO: whole 512x256 world (262,144 B) or 1024x512 (1,048,576 B) fits in 8 MB SDRAM; chunk cache optional. Target: small sandbox with dig/place, one parallax layer, simple lighting only if S5.6 measures it affordable. Tearing needs handling (R2).
- Class A, ESP32-S3 + SPI ILI9341: world in PSRAM, bands in internal RAM. The panel bus, not the CPU, sets the ceiling: 8.1 fps full-screen in spec; a 160x120 logical window drawn 1:1 is 4x less data (32.5 fps ceiling at 10 MHz) [calculated, 03]. Target: slow-paced sandbox or platformer with windowed or band-skipped updates.
- Class A, STM32H7 (Tier B): most headroom (480 MHz, 1 MB SRAM, LTDC); untested.
- Class B, ESP32 classic and RP2350 (Tier B): 256x128 world (65,536 B) in RAM or chunk streaming from flash/SD; no lighting by default.
- Class C, RP2040 and F401/F411/F446 (Tier B): a 512x256 world (262,144 B) does not fit RAM (RP2040 has 264 kB total); platformer or tiny streamed world with a sparse change list.
- Not claimed: any fps for any class (K12). Recorded after S3.4, S4.4, S4.5, S6.2.

## 5 Recommended rendering architecture (spec item 4)
Confirmed K2/K3/K15 from 03:
- One path: band rendering. The game's render callback runs once per band under a clip rectangle; a band of N lines is flushed through the display HAL `flush(rect, pixels)`. N = full height is classic framebuffer mode; N = 8-40 is low-RAM mode.
- Band format RGB565; assets indexed 4/8 bpp with palette, converted at blit time.
- Integer output scale 1x/2x/3x at flush (saves render CPU, not SPI time).
- Proposals for S2 [assumed]: band-skip when nothing in a band changed; windowed flush for small logical screens; async double band buffer where DMA exists; on DISCO band in SRAM then DMA2D copy into the SDRAM framebuffer (measured in S3.2).
- Direct-to-display, scanline, dirty-rectangle, tile-based and DMA-assisted rendering are all special cases of this path (N = 1, skip rules, DMA flush). Comparison in 03.

## 6 Proposed memory strategy (spec item 5)
- K5: no heap after init. Arenas and fixed pools sized in a compile-time config header; a script reports RAM/flash per module from the linker map (S5.4).
- Per-frame budget [calculated]: band 320x16 RGB565 = 10,240 B; two bands = 20,480 B; full frame 153,600 B.
- World: 32x32-tile chunks, two layers, 1 B per tile = 2,048 B per chunk; LRU cache sized by capability profile; save = seed + run-length-packed change lists (K8).
- Assets: read-only data stays in flash (XIP where available); sprites indexed, converted at blit time.
- Placement rules [assumed]: band buffers in DMA-capable internal RAM; world and framebuffers in external RAM where it exists (SDRAM, PSRAM); hot entity pools in internal RAM.
- Fragmentation: none by construction (pools, arenas reset per frame or per scene).

## 7 Modular engine architecture (spec item 6)
Rule: dependencies point downward only. `core` includes no platform header; every board is reached through HAL (K7).

| Layer | Spec modules | Notes |
|---|---|---|
| HAL / platform | HAL, Platform, Display (driver side) | `hal/*.h` declarations; one implementation linked per platform; `drivers/` for ILI9341 etc. |
| Base | Core, Debug | lifecycle, fixed-timestep loop (K4), config, arenas, pools, counters |
| Graphics | Graphics, Renderer, Sprite, Tilemap, Camera | band renderer, primitives, sprites, tile draw, camera |
| Services | Input, Audio, Storage, Resource Manager | unified APIs over HAL |
| Game systems | World, Entity, Physics, UI | chunk world (K8), pools (K9), AABB/tile collision, widgets |
| Game | example code | uses only public headers |

Optional modules (audio, lighting, particles, UI, storage backends) are switched by the capability profile (K14). Interfaces are written in S2.1-S2.3.

## 8 Language and build system (spec item 7)
- C++17, `-fno-exceptions -fno-rtti`, no allocating STL, no iostream; C-compatible HAL headers (K6, K7).
- CMake canonical; `pge_core` static library with zero platform includes. STM32: CMake + arm-none-eabi (as the reference repo). ESP32: ESP-IDF component, not Arduino. PlatformIO and STM32CubeIDE are wrappers added in S7.2b (K6).
- Tests: host-only, assert macros + CTest, no framework (K10). Hardware checks are bench programs printing to UART/LCD.
- Tradeoff: Arduino users wait until S7.2b.

## 9 Biggest technical risks and unknowns (spec item 8)
Risks R1-R9 are in `PLAN.md`. New or sharpened in S1:
- R10 Portrait panel [verified, 02]: DISCO LCD is 240x320. Landscape logical screens need a rotate at flush, or the game runs portrait. Decide in S2.2 and measure in S3.2.
- R11 ILI9341 clock [verified, 03]: 10 MHz in spec; 26 and 40 MHz out of spec. A stated fps must name the clock used.
- R12 STM32 datasheet numbers come from mirrors [verified, 02]; re-check vs st.com.
- R13 Scope gap: enemies/AI, health/damage, crafting are in spec section 3 but in no session. Proposal [assumed]: health + damage as a component and event in S5.2; enemy AI as example code in S6.1; crafting as a recipe table in the example (S6.2) or out of scope for v1. User decides at G1.
- Unknowns: TE pin on the user's SPI module; DMA2D gain on DISCO; SDRAM contention between LTDC and CPU; ESP32-S3 PSRAM + SPI contention; DISCO audio pin confirmation; ESP32-DOOMGAD final display interface; H7 cache coherency (Tier B).

## 10 Prioritized MVP feature list (spec item 9)
P0 (Stages 3-4, minimum engine, platformer on host + DISCO + ESP32-S3):
1. HAL: display flush, time, input. 2. Core loop (fixed timestep), config, arenas, pools. 3. Band renderer with clip. 4. Primitives, text. 5. Sprites (flip, transparency, animation). 6. Tilemap, camera, parallax. 7. Unified input. 8. AABB tile collision. 9. Host SDL2 backend. 10. DISCO backend. 11. ESP32-S3 backend (S4.5).
P1 (Stage 5): chunk world, procedural generation, entities, physics, save/load, asset tools, memory report.
P2 (Stage 5-6): audio, UI/HUD, debug overlay, lighting (optional), sandbox example.

## 11 Proposed project directory structure (spec item 10)
```
PIXEL_ENGINE/
  CMakeLists.txt  LICENSE  README.md  PLAN.md  STATUS.md
  cmake/                 toolchain files, options
  core/include/pge/      public headers
  core/src/              base, gfx, world, entity, physics, input, audio, storage, resource, ui, debug
  hal/include/pge/hal/   display.h input.h time.h audio.h storage.h (declarations only)
  drivers/               ili9341 (SPI, 8080); no engine logic
  platform/              host/  stm32f429i_disco/  esp32s3/   (Tier B skeletons added in S7.2a)
  examples/              platformer/  sandbox/
  tools/                 asset scripts, memory report
  bench/                 b1/ and raw results
  tests/                 host unit tests (CTest)
  docs/                  research/ architecture/ benchmarks/ dev/ user/
  plan/                  session plans
```
Final tree is fixed in S2.1.

## 12 First implementation target (final recommendation)
Start with the desktop simulator (SDL2) and the STM32F429I-DISCO, only those until the minimum engine works; ESP32-S3 + SPI ILI9341 follows in S4.5 as the portability proof (K1).
Why:
- The user owns the board and the reference repo already has a working CMake + arm-none-eabi + OpenOCD flow [verified, 01].
- LTDC scans out of 8 MB SDRAM; the bus ceiling that limits SPI panels (8.1 fps in spec) does not apply, so engine problems show up as engine problems, not as bus problems [verified, 02; calculated].
- It still exercises the real embedded path: SRAM band buffer, DMA2D copy, no heap, fixed pools.
- Host first gives fast tests and a debugger before hardware time is spent.
Caveat: DISCO's roomy SDRAM could hide low-RAM mistakes. Mitigation (K1): the host build runs 16-line bands and a simulated slow bus from S3.1b, long before the second board.

## 13 Key decisions after S1
| Key | Status | Note |
|---|---|---|
| K1 | confirmed | reasons in section 12 |
| K2, K3 | confirmed | 03; indexed band deferred to G3 |
| K4-K11, K13, K14 | unchanged | provisional until G1/G2/G3 |
| K12 | amended in S1.3 | every bus number states datasheet limit and clock used |
| K15 | amended in S1.3 | flush scaling saves CPU, not SPI time |
New items for S2 (proposals, not decisions): R10 portrait handling, R13 scope gap, band-skip, windowed flush, async band buffers.

## 14 Gate G1
User: approve this report and decide the R13 proposal (health/damage in S5.2, enemy AI as example code, crafting in example or out of scope). Q1-Q3 are answered (see STATUS.md). After approval, the next session is S2.1.
