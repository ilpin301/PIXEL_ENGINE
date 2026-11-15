# Plan: Pixel Game Engine (PGE) for microcontrollers — staged, session-sized
_Round 1 — approved by GLM-5.3 with no HIGH findings; all 15 follow-ups applied by Claude; then revised 2026-11-15 after the user's answers (STM32F429I-DISCO first, MIT confirmed). The post-approval edits were not re-reviewed._

Spec: `prompt_part01.txt` + `prompt_part02.txt` (repo root). This file is the index. Session details: `plan/PLAN-S1-research.md` … `plan/PLAN-S7-docs-release.md`. Writing this plan creates no code.

## Goal
Deliver the spec's seven stages (research → architecture → prototypes → minimum engine → advanced features → sandbox example → docs/release) as 32 small sessions. Every session fits inside one 5-hour usage window, starts from a cold context, reads a short fixed set of files, and ends with a saved deliverable plus an updated `STATUS.md`. The engine itself: portable C++17 core, per-platform HAL, measured (not promised) performance.

## Verified facts (reference repos, read 2026-11-15 via GitHub API)
- `il721/ESP32-DOOMGAD`: hardware design only (KiCad schematic, no PCB). Target module ESP32-S3-WROOM-1-N16R8 (16 MB flash, 8 MB octal PSRAM). Planned: ILI9341 320x240 on an 8-bit 8080 bus (connector pinout is a placeholder), microSD on SDMMC 4-bit, MAX98357A I2S amp, 74HC165 for 8 buttons, analog stick on ADC. Existing firmware is a breadboard test only (`breadboard/src/main.cpp`: PlatformIO + Arduino, Adafruit ILI9341 over SPI, SD over SPI, legacy I2S driver, an SPI full-frame fps bench for 320x200). No engine code. The bench result is not stored in the repo.
- `il721/STM32F429I-DISCO`: CMake + arm-none-eabi + Ninja + OpenOCD, STM32CubeF4 as a submodule, HAL + board BSP. Demo (`Core/Src/main.c`, `game.c`): draws straight to the LTDC layer framebuffer through `BSP_LCD_*` calls, 240x320, touch + gyro input, self-throttled to about 30 Hz. DOOM port (`DOOM/port/`): doomgeneric (GPLv2), CPU at 168 MHz, 8 MB SDRAM, 320x200 8-bit indexed, rotated double-buffered output, USB-host MSC + FatFs, external buttons + KY-023 stick.
- Not yet verified (S1.1 resolves): DMA2D use, how the stick ADC is read, the linker memory map, SPI bench numbers, free audio-capable pins on the DISCO board.
- Hardware the repos show the user has: STM32F429I-DISCO, ESP32-S3 N16R8 devkit, an old ESP-WROOM-32 devkit, an SPI ILI9341 module, KY-023 stick, buttons, MAX98357A. Ownership of RP2040/RP2350 and STM32H7 boards: unknown.

## Session protocol (the anti-token-leak rules)
1. One session = one row of a part file. Start it in a fresh session (`/clear` or a new window) with: "Run session S<x.y> from PLAN.md".
2. Read set at start, nothing else by default: this file's sections "Session protocol" and "Key decisions", the one part file, `STATUS.md`, and the inputs that session lists. From earlier deliverables read only the top block "Summary for later sessions" (max 40 lines, mandatory in every deliverable).
3. Never read: `DOOM/engine/**`, the `STM32CubeF4` submodule, build output, `.wad/.bin/.apk` files, vendored SDK sources. Look up vendor APIs through documentation (context7 or a datasheet page), not by reading SDK trees.
4. Exploring a repo goes through a read-only investigator subagent that returns a compressed table. All file writes go through a cheap-model subagent (user rule); the main session reports summaries only.
5. Size caps: a doc deliverable ≤ 250 lines; a code session ≤ about 600 new lines in ≤ 8 files. If a session needs more, split it and record the split in `STATUS.md` instead of pushing on.
6. Checkpoint rule: when the session's checklist is not finished and the context has been compacted once, write the resume point into `STATUS.md` and stop. The next session continues from that resume point.
7. End of every session: update `STATUS.md` (done / resume point / new decisions / open questions), then stop. No carrying on into the next session.
8. GLM review runs at two gates only: this plan, and the architecture (S2.4). Human gates: G1 after S1.4, G2 after S2.4, G3 after S3.4, G4 after S4.4, G5 after S5.6, G6 after S6.2.
9. Decisions are append-only: a change to a Key decision is a new dated line in `STATUS.md` → "Decision log" with the reason, never a silent edit.

`STATUS.md` (created in S1.1) holds: session table with state, resume point, decision log, open questions, feature tracker (implemented / planned / incomplete).

## Parts
| Part file | Stage | Sessions | Output |
|---|---|---|---|
| `plan/PLAN-S1-research.md` | 1 Research + feasibility | S1.1–S1.4 | `docs/research/*.md`, the spec's 10 first-response items |
| `plan/PLAN-S2-architecture.md` | 2 Architecture | S2.1–S2.4 | `docs/architecture/*.md`, API specs |
| `plan/PLAN-S3-prototypes.md` | 3 Prototypes | S3.0, S3.1a, S3.1b, S3.2, S3.4 | repo skeleton, host + F429 backends, measured benchmarks |
| `plan/PLAN-S4-mve.md` | 4 Minimum engine | S4.1, S4.2a, S4.2b, S4.3, S4.4, S4.5 | engine + platformer example + second port (ESP32-S3) |
| `plan/PLAN-S5-advanced.md` | 5 Advanced features | S5.1a, S5.1b, S5.2, S5.3a, S5.3b, S5.4–S5.6 | world, entities, storage, tools, audio, UI |
| `plan/PLAN-S6-example-game.md` | 6 Sandbox example | S6.1–S6.2 | Terraria-like demo |
| `plan/PLAN-S7-docs-release.md` | 7 Docs + release | S7.1, S7.2a, S7.2b | docs, porting guide, release |

Order is strict by stage; inside S5 the sessions S5.4 and S5.5 may swap.

## Key decisions & tradeoffs (K1–K15, provisional until G1/G2/G3 confirm them)
- **K1 First target** = desktop simulator (SDL2) plus STM32F429I-DISCO, and only that board until the minimum engine works (user direction 2026-11-15). The ESP32-S3 with the SPI ILI9341 module is the second port, done in S4.5 as the proof that the engine is universal: the platformer must run there with no change to the core. The engine stays board-independent from the first line of code: the core includes no platform header, every board is reached only through the HAL (K7), and the host build runs with a 16-line band and a simulated slow display bus from S3.1b on, so the low-RAM / slow-bus path is exercised long before the second board. Tradeoff: real SPI problems surface later (S4.5) than in the first draft; the simulated bus reduces that risk but does not remove it.
- **K2 One render path: band rendering.** The scene is drawn into a band buffer of N lines and flushed through the display HAL `flush(rect, pixels)`. N = full height gives classic framebuffer mode; N = 8–40 gives low-RAM mode. Immediate mode: the game's render callback runs once per band under a clip rectangle; no command list. Tradeoff: render code must be repeatable within a frame and is re-run per band (CPU) in exchange for zero command-buffer RAM. Sizes (calculated): 320x240 RGB565 full frame = 153,600 bytes; a 320x16 band = 10,240 bytes.
- **K3 Band buffer format RGB565**; assets stored indexed 4/8 bpp with a palette, converted to RGB565 at blit time. An 8-bit indexed band buffer (half the RAM, native L8 on LTDC, palette effects) is deferred and revisited at G3 with measurements. Tradeoff: one simple path now, more RAM per band.
- **K4 Fixed-timestep logic** (tick callback, integer / fixed-point positions), rendering decoupled. No `float deltaTime` in the core API. Reason: RP2040 has no FPU, behaviour is deterministic, saves stay stable. Tradeoff: less familiar than the spec's sample; a float convenience layer may sit outside the core.
- **K5 No heap after init.** Arenas and fixed pools sized in a compile-time config header; a script reports RAM/flash per module from the linker map.
- **K6 Language and build:** C++17 with `-fno-exceptions -fno-rtti`, no allocating STL containers, no iostream. CMake is the canonical build: `pge_core` static library with zero platform includes. STM32 = CMake + arm-none-eabi (as in the reference repo). ESP32 = ESP-IDF component (not Arduino). PlatformIO manifest and STM32CubeIDE import notes come in S7.2 as wrappers. Tradeoff: Arduino users wait until S7.2.
- **K7 HAL** = C-compatible free functions declared in `pge/hal/*.h`, one implementation linked per platform (link-time binding, no virtual calls in hot paths). Tradeoff: one display/platform per binary, no runtime backend switching.
- **K8 World** = 32x32-tile chunks, two layers (foreground + wall), 1 byte per tile per layer (2,048 bytes per chunk). LRU cache of chunks sized by the capability profile. A chunk is rebuilt from the world seed, then a stored sparse list of player changes is applied; a save is seed + change lists, run-length packed. Tradeoff: regeneration CPU at chunk load instead of storing the whole world. Numbers confirmed in S5.1.
- **K9 Entities** = fixed pools with parallel arrays per component kind, not a general ECS framework.
- **K10 Tests:** host-only unit tests for all portable code, plain assert macros + CTest, no test-framework dependency. Hardware checks are bench programs that print to UART/LCD.
- **K11 Hardware tiers.** Tier A (measured on real boards): host and STM32F429I-DISCO first, ESP32-S3 from S4.5. Tier B (HAL skeleton, labeled untested): RP2040/RP2350, STM32H7, ESP32 classic, generic STM32F4. Tier B moves to A only when a board is available.
- **K12 Honest numbers.** Every performance number is tagged "calculated" (formula shown) or "measured" (board, build flags, date). No fps target is stated before S3.4. Example of "calculated": a full 320x240 RGB565 frame is 1,228,800 bits, so a 40 MHz SPI bus caps at 32.5 full frames/s before any overhead.
- **K13 License** MIT (confirmed by the user 2026-11-15). Nothing is copied from the reference repo's `DOOM/` tree (GPLv2).
- **K14 Capability profile** = compile-time constants per board (RAM budget, band lines, has-FPU, display bus, audio, storage) that switch optional modules (lighting, audio, number of parallax layers, chunk cache size) on or off.
- **K15 Camera scaling** = integer output scale only (1x / 2x / 3x, pixels repeated at flush), so a 160x120 logical screen can fill 320x240 at about a quarter of the render cost. Free zoom is out of scope. Specified in S2.2, measured in S3.

## Risks / open questions
- R1 Band rendering cost: re-running the render callback per band may dominate CPU on small bands. Measured in S3.1a–S3.2 and again in S4.5.
- R2 SPI tearing and partial-update artifacts on ILI9341 (common modules expose no TE pin). The same applies to LTDC: bands copied into a framebuffer that is being scanned out tear on moving content; S3.2 records the handling chosen (accept, sync the copy to the scan line, or back buffer + swap).
- R3 ESP32-S3 PSRAM is slow for pixel work; the band buffer must sit in internal RAM, and DMA transfer size limits apply. Unmeasured.
- R4 Toolchains on this Windows machine (SDL2, ESP-IDF, arm-none-eabi, OpenOCD) are not yet checked — S3.0 does that first.
- R5 Hardware sessions (S3.2, S4.3, S4.4, S4.5, S6.2) need the board connected and the user present to confirm what the screen shows.
- R6 Save data growth: change lists grow with player edits; flash wear on internal flash. Addressed in S5.3.
- R7 Fixed-point API ergonomics (K4) may annoy game authors.
- R8 Audio on STM32F429I-DISCO: no onboard codec; a usable DAC/PWM pin is unverified (S1.2).
- R9 A session overruns its window anyway — handled by protocol rules 5 and 6.
- Q1 answered 2026-11-15: MIT.
- Q2 answered 2026-11-15: the user owns only the STM32F429I-DISCO and an ESP32-S3-WROOM-1-N16R8. All other boards stay K11 Tier B (untested).
- Q3 answered 2026-11-15 (user asked Claude to choose): the ESP32-S3 port uses the SPI ILI9341 module that already exists. The 8080 parallel screen needs a board that is not built yet; it is added later as one more display driver, with no engine change.

## Out of scope
- Recreating Terraria or its content; networking / multiplayer; 3D; a GUI tile editor (converters only); embedding LVGL (studied in S1.3 only); an RTOS requirement in the core; USB HID host input (listed in the porting guide as a future item); free camera zoom (integer scale only, K15); any code in this planning step.
