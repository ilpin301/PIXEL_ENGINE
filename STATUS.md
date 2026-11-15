# STATUS (project: Pixel Game Engine)

## Session table

| Session | Part file | State |
|---|---|---|
| S1.1 | plan/PLAN-S1-research.md | done 2026-11-15 |
| S1.2 | plan/PLAN-S1-research.md | done 2026-11-15 |
| S1.3 | plan/PLAN-S1-research.md | done 2026-11-15 |
| S1.4 | plan/PLAN-S1-research.md | done 2026-11-15 |
| S2.1 | plan/PLAN-S2-architecture.md | done 2026-11-15 |
| S2.2 | plan/PLAN-S2-architecture.md | done 2026-11-15 |
| S2.3 | plan/PLAN-S2-architecture.md | done 2026-11-15 |
| S2.4 | plan/PLAN-S2-architecture.md | done 2026-11-15 (G2 pending) |
| S3.0 | plan/PLAN-S3-prototypes.md | done 2026-11-15 |
| S3.1a | plan/PLAN-S3-prototypes.md | done 2026-11-15 (window checked by user 2026-11-15) |
| S3.1b | plan/PLAN-S3-prototypes.md | done 2026-11-15 |
| S3.2a | plan/PLAN-S3-prototypes.md | done 2026-11-15 (scene confirmed by user on the board, rotated 180 from first try) |
| S3.2b | plan/PLAN-S3-prototypes.md | done 2026-11-15 (B1 numbers in docs/benchmarks/raw/f429.txt; input and tearing confirmed by user on the board) |
| S3.4 | plan/PLAN-S3-prototypes.md | todo |
| S4.1 | plan/PLAN-S4-mve.md | todo |
| S4.2a | plan/PLAN-S4-mve.md | todo |
| S4.2b | plan/PLAN-S4-mve.md | todo |
| S4.3 | plan/PLAN-S4-mve.md | todo |
| S4.4 | plan/PLAN-S4-mve.md | todo |
| S4.5 | plan/PLAN-S4-mve.md | todo |
| S5.1a | plan/PLAN-S5-advanced.md | todo |
| S5.1b | plan/PLAN-S5-advanced.md | todo |
| S5.2 | plan/PLAN-S5-advanced.md | todo |
| S5.3a | plan/PLAN-S5-advanced.md | todo |
| S5.3b | plan/PLAN-S5-advanced.md | todo |
| S5.4 | plan/PLAN-S5-advanced.md | todo |
| S5.5 | plan/PLAN-S5-advanced.md | todo |
| S5.6 | plan/PLAN-S5-advanced.md | todo |
| S6.1 | plan/PLAN-S6-example-game.md | todo |
| S6.2 | plan/PLAN-S6-example-game.md | todo |
| S7.1 | plan/PLAN-S7-docs-release.md | todo |
| S7.2a | plan/PLAN-S7-docs-release.md | todo |
| S7.2b | plan/PLAN-S7-docs-release.md | todo |

## Resume point

Next: S3.4 (benchmark report and decision review). Read set: PLAN.md protocol + Key decisions, plan/PLAN-S3-prototypes.md, STATUS.md (S3.2b decision-log line), docs/benchmarks/raw/host.txt, docs/benchmarks/raw/f429.txt, docs/research/03 section 5 (calculated bounds). Output docs/benchmarks/stage3.md, then Gate G3 (user). Start in a fresh session: "Run session S3.4 from PLAN.md". Board needed only if a number looks wrong (re-run: flash_f429_b1, read log as in docs/dev/toolchain.md).

## Decision log (append-only)

- 2026-11-15 S1.1: no Key decision changed. Observation: neither reference repo has measured performance numbers; K12 stands.
- 2026-11-15 Q2 answered by user: owns exactly two boards - STM32F429I-DISCO (USB ST-LINK seen connected) and ESP32-S3-WROOM-1-N16R8. No RP2040/RP2350, STM32H7 or ESP32 classic. Effect: K11 Tier A = host + F429I-DISCO + ESP32-S3 only; every other board (RP2040/RP2350, STM32H7, ESP32 classic, generic STM32F4) stays Tier B, HAL skeleton labeled untested, no measured numbers. S1.2 compares those chips from datasheets only.
- 2026-11-15 S1.2: no Key decision changed. Findings: R8 resolved by datasheet - DISCO has free SAI1 block A on PE2/PE4/PE5/PE6 (I2S amp feasible), PWM fallback TIM9 PE5/PE6; DAC_OUT2 (PA5) conflicts with the stick ADC; confirm on board in S3. STM32 numbers come from mirrors, not st.com - re-check before relying (see 02 section Source quality). All bus fps figures are calculated ceilings (K12).
- 2026-11-15 S1.3: no Key decision changed; K2 and K3 confirmed, K15 and K12 amended in docs/research/03 section 5. Key finding: ILI9341 datasheet limits 4-line SPI write to 10 MHz (twc 100 ns) = 8.1 fps ceiling at 320x240 RGB565, and 8080 to ~15.15 MHz (66 ns); the 40 MHz in the breadboard code is out of spec. Proposals for S2 (not yet decisions): band-skip for unchanged bands, windowed flush (1:1 small logical screen cuts SPI bus cost 4x), async double band buffer where DMA exists, DISCO path = band in SRAM then DMA2D copy to SDRAM framebuffer. Rule added to K12: every bus number states the datasheet limit and the clock used. 2x scale at flush saves render CPU, not SPI bus time.
- 2026-11-15 S1.4: no Key decision K1-K15 changed. K1 confirmed (host SDL2 + STM32F429I-DISCO first, ESP32-S3 + SPI ILI9341 in S4.5). Findings: (a) DISCO panel is portrait 240x320, landscape needs a rotate step - new risk R10, decide in S2.2; (b) scope gap - enemies/AI, health/damage and crafting are in spec section 3 but in no session - new risk R13, proposal: health+damage in S5.2, enemy AI as example code in S6.1, crafting as example recipe table in S6.2 or out of scope for v1, awaiting user at G1; (c) new risks R11 (ILI9341 clock) and R12 (STM32 numbers from mirrors) recorded in docs/research/00 section 9.
- 2026-11-15 S2.1: no Key decision K1-K15 changed. Proposals D1-D6 recorded in docs/architecture/01-overview.md section 9 (6 HAL headers, three allocators, placement macros PGE_RAM_FAST/DMA/EXT/ROM, named profiles SMALL/MEDIUM/LARGE, optional modules compile out, asset data as pointer plus size); accepted or rejected at G2. Engine RAM budgets (assumed allocations): SMALL 14,976 B, MEDIUM 70,528 B, LARGE 76,160 B internal + 284,672 B external. Flash: no estimate before code; design caps 64/128/256 KB checked by S5.4. Session ran before an explicit G1 reply from the user; R13 stays open.
- 2026-11-15 G1 passed (user: "approved, health in S5.2, AI in example, crafting in S6.2"): docs/research/00-stage1-report.md approved; R13 resolved: health + damage = engine component and event in S5.2; enemy AI = example-game code in S6.1; crafting = recipe table in the example, S6.2. All Key decisions K1-K15 stay provisional until G2/G3.
- 2026-11-15 S2.2: no Key decision K1-K15 changed. Proposals D7-D12 in docs/architecture/02-hal-render-game-api.md section 7 (platform transposes for the portrait DISCO panel = R10 answer; one outstanding flush; Canvas passed per band; static-dispatch game template with Input as parameter; draw calls silent-clip with counters; scale staging buffer). Finding: integer scale (K15) needs a staging buffer (N x s) x (W x s) x 2 B, which the 01-overview RAM budget did not include; S4.2a adds scale_staging_bytes to the profile. The spec sample game is expressible with the listed API; 8 differences listed with reasons. Accepted or rejected at G2.
- 2026-11-15 S2.3: no Key decision K1-K15 changed. Proposals D13-D18 in docs/architecture/03-world-entity-storage-api.md section 9 (change log + regenerate chunks with eviction without write-back; reserved tile ids 0 air and 255 void; engine owns Health + damage events while AI, crafting and inventory are game code per G1; two-slot atomic save with CRC-32 and TLV sections; compile-time asset ids, no compression in v1; audio SPSC command queue and stateless UI widgets). Findings: Fix 16.16 limits a world to +-32,767 px per axis = 2,047 tiles at 16 px / 4,095 at 8 px; chunk cache sizing rule matches the 01 counts (MEDIUM 16, SMALL 4). RAM added on top of 01: SMALL +3,764 B, MEDIUM +24,048 B, LARGE +11,344 B internal + 163,840 B external; new totals SMALL 18,740 B, MEDIUM 94,576 B, LARGE 87,504 B internal + 448,512 B external (scale staging from 02 still extra). Particles use a compact 8 B pool, not 24 B entities (small deviation from the S5.2 wording). Accepted or rejected at G2.
- 2026-11-15 S2.4: GLM architecture review, 2 rounds (round 1: 1 HIGH FatFs rename-over save, 9 MED, 7 LOW, all applied or deferred; round 2: VERDICT APPROVED). Log in docs/architecture/REVIEW-LOG.md. New proposal D19 (change-log cap is a gameplay limit; ChangeLogNearFull; snapshot-reset deferred to S5.1a). No Key decision K1-K15 changed; D1-D19 accepted or rejected at G2. Substantive changes: save sequence remove(.bak) + rename (target must not exist); streaming Producer/Consumer save API (no contiguous payload); renderer pre-clears bands (clear_color); back-buffer present path with frame_end latch (PGE_DISP_VSYNC); DISCO storage = USB-host MSC + FatFs [assumed]; AUTO_REAP opt-in health flag; TileSource chunkRow callback for TileLayer; new totals (01: 16,512 / 77,696 / 90,496; 03: 20,276 / 101,744 / 101,840 internal + 448,512 external for LARGE).
- 2026-11-15 G2 passed (user: "G2 approved, go ahead with S3.0"): docs/architecture/* approved; proposals D1-D19 accepted; Fix 16.16 world limit accepted for v1; K1-K15 stay provisional until G3.
- 2026-11-15 S3.1a: no Key decision changed. Prototype deviations from the 02 API spec: renderFrame free function + RenderFn instead of the engine/game template; camera passed as int offsets instead of Fix/Camera; sprites 8-bit indexed with a palette, index 0 transparent; no scale/rotation/line/circle/text yet; display HAL implemented with an SDL2 streaming RGB565 texture. Results: ctest 2/2 passed (version, gfx) with SDL2 and without it (SDL parts skipped); gfx test checks band output byte-identical to full-height output for frames {0,37,100} x band {240,16,8,7,1}, flush coverage, clipping with guard words, transparent vs opaque blit, InvalidArg for a too-small buffer; headless demo 60 frames, band 16 and band 240 BMPs are byte-identical (cmp). Window not yet viewed by the user.
- 2026-11-15 S3.1b: no Key decision changed. Added HAL headers input.h (buttons bitmask, 4 axes, touch, PGE_INPUT_QUIT) and time.h (pge_hal_time_us, delay) exactly as in 02 section 2. Host: SDL keyboard input (arrows/WASD, Z/J=A, X/K=B, Enter=START, Backspace/RShift=SELECT, Esc=QUIT), headless display backend pge_platform_host_null (no SDL needed), host-only slow-bus simulation (pge_host_bus_set(clock_hz, bits_per_clock): flush busy-waits bytes*8/(clock*bits)). B1 harness = bench/b1/b1.{h,cpp}, portable (core + HAL only), 8 items (clear, flush_only, sprites_opaque, sprites_transparent, tiles8/16 static and scroll; two tile layers, no tile transparency in the prototype, so layer 2 just overdraws) x bands 8/16/32/240; renderFrame pre-clears every band, so flush_only = pre-clear + flush. Host numbers (Release, desktop CPU, tag "host, not representative") in docs/benchmarks/raw/host.txt: e.g. band 16, 200 frames: 100 opaque sprites 68 us/frame, tiles8 scroll 118 us/frame. Slow-bus check agrees with the calculation: band 16 flush at 40 MHz 1-bit = 30.7 ms/frame (calc 30.72 ms), at 10 MHz = 122.9 ms/frame (calc 122.88 ms, the ILI9341 datasheet limit, S1.3); headless demo with --bus-hz 40000000 paced at fps=29. Release benchmarks need a separate build dir with -DCMAKE_BUILD_TYPE=Release -DPGE_BUILD_TESTS=OFF (NDEBUG would disable test asserts). ctest 3/3 passed (version, gfx, bench).
- 2026-11-15 S3.1a/S3.1b window check: user ran build/examples/proto/pge_proto_demo.exe, scene visible and the on-screen key indicator lights up on key presses (demo also has a movable box on arrows/WASD).
- 2026-11-15 S3.2 split (protocol rule 5, no Key decision changed): S3.2a = cross-build + bring-up (clock, SDRAM, LTDC, DWT time, display HAL, scene on LCD, user confirms); S3.2b = B1 on the board, DMA2D variant, input HAL, tearing record, docs/benchmarks/raw/f429.txt. S3.4 reads both. Prep notes for S3.2a/b: (a) first compile core/src/gfx/gfx.cpp and bench/b1/b1.cpp with arm-none-eabi-g++ -std=c++17 -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard -fno-exceptions -fno-rtti -c to prove the code is reusable unchanged; (b) root CMakeLists.txt always adds platform/host, pge_b1_host and tests and is CXX only: add a platform switch plus C and ASM languages for Cube + startup; reach the Cube checkout through a cache variable pointing at the sibling F:/____IL_CPP/STM32F429I-DISCO/STM32CubeF4 (do not vendor, do not read its tree); the reference CMakeLists.txt there lists the HAL modules; (c) number output: the probe 0483:3748 is likely the plain ST-LINK/V2 without virtual COM port - verify; if so use a USB-UART adapter per the reference WIRING.md, ask the user, or fall back to a RAM text log read through OpenOCD / semihosting; (d) pge_hal_time_us must wrap at 2^32 us but the DWT cycle counter at 168 MHz wraps every ~25.5 s: accumulate deltas or combine with SysTick; (e) check the LTDC layer pixel format (BSP default is likely ARGB8888, bands are RGB565) and record the orientation (portrait native vs landscape + transpose) before coding; DMA2D cannot transpose, so the DMA2D copy variant only fits the portrait path or fills; name the orientation in the B1 header; (f) K13: take nothing from the reference DOOM/ tree; Core/Src/main.c is fine as reference, read through an investigator subagent (rule 4); (g) input pins from the reference WIRING.md, not from memory (PA5 conflict already noted); (h) flashing replaces the firmware now on the board; the reference repo script load_doom restores it - tell the user in one line before flashing.
- 2026-11-15 S3.2a: no Key decision changed. Built: cmake/arm-none-eabi.cmake + PGE_PLATFORM switch in root CMake (host build unchanged, ctest 3/3), platform/stm32f429i_disco/{board.c,board.h,display_ltdc.c,stm32f4xx_hal_conf.h,linker script}, examples/f429/main.cpp; Cube reached via cache var PGE_CUBE_DIR (not vendored). core/gfx.cpp and bench/b1/b1.cpp compile unchanged with arm-none-eabi-g++ 10.3.1 (-Os: gfx 952 B text, b1 1556 B text + 6248 B bss). Decisions: clock is 180 MHz (HSE 8, PLL 8/360/2/7, overdrive), as in the reference demo, not 168 (168 was only the DOOM build); orientation = logical landscape 320x240 + transposing CPU copy at flush into an RGB565 LTDC layer at 0xD0000000 (SDRAM, 240x320 portrait panel), flags PGE_DISP_TRANSPOSED, no VSYNC so tearing is accepted for now (R2, S3.2b records the handling); layer configured directly with HAL_LTDC_ConfigLayer in RGB565 (BSP_LCD_LayerDefaultInit would be ARGB8888); rotation macro PGE_F429_ROT default 0 (user-confirmed upright; 1 was upside down). Time HAL: DWT CYCCNT accumulated every 1 ms in SysTick and on each call (wrap-safe, 23.9 s counter wrap passed in the check). No UART: ST-LINK 0483:3748 is plain V2 without virtual COM port, so numbers go to a RAM ring buffer (pge_dbg_log) read with OpenOCD dump_image; S3.2b may add an LCD text print. Checks on the board: SDRAM 256 KB pattern test PASS, HAL_Delay(100) = 100603 us, 3 s OpenOCD gap = 3,008,387 us, informal scene speed 104-106 fps at 16-line bands with the CPU transpose (not a B1 number). Stack-size check in the linker script raised 0x400 to 0x2000. Flashing replaced the original board firmware; a backup is in the reference repo backup/ dir.
- 2026-11-15 S3.2b: no Key decision changed. Built: display variants selectable at runtime (pge_f429_set_opts), portrait-native logical 240x320 with CPU memcpy or DMA2D M2M flush, vsync back buffer (FB1 0xD0040000, frame_end sets LTDC_Layer1->CFBAR + SRCR VBR and waits for the latch; +153,600 B SDRAM), input HAL platform/stm32f429i_disco/input_gpio.c, examples/f429/{b1_main,input_main}.cpp, debug log enlarged to 32,768 B (read offsets via arm-none-eabi-nm, see toolchain.md). Measured (docs/benchmarks/raw/f429.txt, 180 MHz, Release -O2, 30 frames, flush is synchronous): band 16 per frame, A landscape CPU-transpose / B portrait memcpy / C portrait DMA2D: 100 opaque sprites 7.8 / 10.9 / 5.4 ms, tiles8 static 13.6 / 15.3 / 9.8 ms, tiles8 scroll 14.2 / 15.4 / 9.9 ms, flush_only (pre-clear + flush) 4.8 / 7.4 / 1.9 ms. Flush alone per band 320x8/16/32: transposing copy 138/261/507 us; portrait memcpy 162/324/647 us; DMA2D 25/49/97 us (about 6.6x faster than memcpy). DMA2D cannot transpose, so the fast path needs a portrait-native logical screen (R10: landscape stays on the CPU transpose, about 3.9 ms per frame at band 16). DMA2D fill of the whole 240x320 panel 965 us vs CPU 1049 us (SDRAM-bandwidth bound, about 159 MB/s). Band re-run overhead (R1) is small: tiles8 static at 8 / 16 / 32 lines = 13.9 / 13.6 / 13.5 ms (landscape). Full-height band buffer in SDRAM is slower than SRAM bands (landscape, sprites: 15.2 ms at 240 lines in SDRAM vs 7.8 ms at 16 lines in SRAM). Tearing (R2) on LTDC: variant D, back buffer + vsync, frame time is constant 15,306 us (about 65.3 Hz panel refresh), scene fps 65-66, user saw no tear line; the no-vsync path was accepted in S3.2a. Input (user, board): USER button and PC11 both light A, stick works; PC12, PD2, PD5 not tested (may be unwired); PD4 and PB7 are read but unmapped (no free HAL bit). Deviations: 10 files touched in this session (cap 8, protocol rule 5); LCD text print skipped (numbers read through OpenOCD); examples/f429/main.cpp still prints the stale text "no vsync: tearing accepted" even with backbuf=1 (cosmetic).

## Open questions

- Q2: answered 2026-11-15 (see Decision log).
- DISCO audio pin: answered in 02 section 6 (confirm on board in S3).
- ESP32-DOOMGAD final display interface (8080 parallel vs SPI): conflict between docs and breadboard.
- TE pin routed on the user's SPI ILI9341 module? (check module, S3)
- R1 per-band render cost: measured S3.1a/S3.2b, small (see Decision log); recheck on ESP32-S3 in S4.5. R2 tearing: F429 answered by back buffer + vsync (S3.2b); SPI ILI9341 still open (S4.5).
- STM32 doc numbers verified via mirrors only - re-check against st.com.
- R10 portrait panel on DISCO: answered in S3.2b (see Decision log): landscape = CPU transpose; portrait-native + DMA2D is the fast option. Final choice at S3.4/G3.
- Verify external buttons PC12, PD2, PD5 on the board when wired (input test firmware pge_f429_input).
- Tick overrun/catch-up policy: decide in S4.1.
- Fix 16.16 world-size limit (2,047 tiles at 16 px): acceptable for v1? Confirm at G2.
- Change-log insert is O(n): measure in S5.1a; upgrade if > 1 ms.
- DISCO USB-host MSC + FatFs storage backend assumed, not verified (S3).
- SDL2 install needed before S3.1a window; unit tests for S3.1a can run without it. (SDL2 2.32.10 installed, see docs/dev/toolchain.md.)

## Feature tracker

Seeded in S1.4 from spec sections 3, 5, 6, 7. Status values: planned / implemented / incomplete / out of scope. GAP = in spec but in no session (see decision log, R13). Update the Status column at the end of each session.

| Feature | Status | Session |
|---|---|---|
| S3 Pixel-art graphics | planned | S4.2a, S4.2b |
| S3 Tile-based maps | planned | S4.3 |
| S3 Large scrolling worlds | planned | S5.1a |
| S3 Procedurally generated terrain | planned | S5.1b |
| S3 Multiple tile layers | planned | S4.3 (draw), S5.1a (store) |
| S3 Character movement and animation | planned | S4.4 (example), S5.2 |
| S3 Collision detection | planned | S4.4 (tile), S5.2 (entity) |
| S3 Physics and gravity | planned | S4.4 (example controller), S5.2 |
| S3 Inventory | planned (basic) | S6.2 |
| S3 Items and crafting | planned | S6.2 (items = inventory stacks, crafting = example recipe table) |
| S3 Interactive objects | planned | S5.1a (tile hook), S5.2 |
| S3 Enemies and basic AI | planned | S6.1 (example-game code; pools from S5.2) |
| S3 Health and damage | planned | S5.2 (engine component + damage event) |
| S3 Particle effects | planned | S5.2 |
| S3 Lighting and environment effects | planned (optional) | S5.6 |
| S3 Sound effects and music | planned | S5.5 |
| S3 Menus, HUD, UI | planned | S5.6 |
| S3 Save/load | planned | S5.3a |
| S3 Capability-based feature switching (K14) | planned | S4.1 |
| S5 Pixels, lines, rectangles, filled rectangles, circles | planned | S4.2a |
| S5 Text rendering | planned | S4.2a |
| S5 Sprites, transparent sprites | planned | S4.2b |
| S5 Sprite sheets, animated sprites | planned | S4.2b |
| S5 Tilemaps | planned | S4.3 |
| S5 Multiple rendering layers | planned | S4.3 |
| S5 Camera movement | planned | S4.3 |
| S5 Camera scaling (integer 1x/2x/3x, K15) | planned | S4.2a |
| S5 Free camera zoom | out of scope | - |
| S5 Parallax scrolling | planned | S4.3 |
| S5 Palette-based rendering | planned | S4.2a |
| S5 Clipping | planned | S4.2a |
| S5 Sprite flipping | planned | S4.2b |
| S5 Sprite rotation in 90 degree steps | planned | S4.2b |
| S5 Free sprite rotation | out of scope | - |
| S5 Basic particle effects | planned | S5.2 |
| S5 Color format evaluation | implemented (research) | S1.3, docs/research/03 |
| S5 Rendering strategy comparison | implemented (research) | S1.3, docs/research/03 |
| S5 Band renderer, DMA flush, band-skip, windowed flush | planned | S3.1a, S3.2, S4.5, S5.6 |
| S6 Chunk-based world storage | planned | S5.1a |
| S6 Tile properties and metadata | planned | S4.4 (flags) |
| S6 Chunk loading and unloading, lazy loading, cache | planned | S5.1a |
| S6 Procedural world generation | planned | S5.1b |
| S6 Collision maps | planned | S4.4 |
| S6 Interactive tiles | planned | S5.1a |
| S6 World boundaries | planned | S4.3 (camera clamp), S5.1a |
| S6 Camera-based rendering | planned | S4.3 |
| S6 Entity management, object pooling | planned | S4.1 (pools), S5.2 |
| S6 Compressed world storage (run-length) | planned | S5.3a |
| S6 External storage for worlds | planned | S5.3b |
| S6 Game code separated from engine | planned | S6.1 (grep check) |
| S7 GPIO buttons | planned | S4.3 |
| S7 Joysticks | planned | S4.3 |
| S7 Gamepads and keypads (as buttons) | planned | S4.3 |
| S7 Touchscreens | planned | S4.3 (F429) |
| S7 USB HID devices | out of scope for v1 (future port item) | S7.2a (porting guide) |
| S7 Unified input API | planned | S4.3 |
| S7 PWM and DAC audio | incomplete: not scheduled | documented in S7.2a; F429 options in 02 section 6 |
| S7 I2S audio | planned | S5.5 |
| S7 Sound effects, basic music, multiple channels | planned | S5.5 |
| S7 Internal flash, external SPI flash, SD card, LittleFS | planned | S5.3b |
| S7 Save games | planned | S5.3a |
| S7 Configuration files | planned | S5.3a |
| S7 Asset loading, unified resource interface | planned | S5.3a |
