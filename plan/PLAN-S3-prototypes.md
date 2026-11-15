# Part S3 — Technical prototypes (Stage 3)
_First code. Prototype code is kept only if it matches the architecture; otherwise it is rewritten in S4. Five sessions (the ESP32-S3 prototype moved to S4.5). Index and rules: `PLAN.md`._

**Benchmark set B1** (identical on all backends): full-screen clear; 100 opaque 16x16 sprites; 100 transparent 16x16 sprites; full-screen tilemap with 8x8 and with 16x16 tiles, two layers, static; the same tilemap scrolling 1 pixel per frame; flush only. Reported per item: time per frame, time per band, bytes of RAM used. Run with bands of 8, 16 and 32 lines, and full height where RAM allows.

## S3.0 Toolchain check and skeleton
- **Steps:** 1) Check what is installed: cmake, ninja, a host C++17 compiler, SDL2, arm-none-eabi-gcc, OpenOCD, ESP-IDF; record versions; list what is missing with its install command (the user installs). 2) `git init`, `.gitignore`, license file (after Q1), directory tree from 01-overview, top-level CMake with an empty `pge_core`, host test runner (CTest + one passing assert test).
- **Deliverable:** skeleton that builds on the host; `docs/dev/toolchain.md`.
- **Done when:** host configure + build + `ctest` output shows 1 passing test.

## S3.1a Host prototype: band renderer core
- **Steps:** Band renderer core (clip, fill, opaque / transparent blit, tile blit); SDL2 display backend implementing the display HAL; unit tests comparing band output against full-frame output (must be byte-identical).
- **Deliverable:** `platform/host/**` display part, `core/gfx` prototype, tests.
- **Done when:** tests pass; a window shows the tile + sprite scene.

## S3.1b Host prototype: input and benchmark harness
- **Steps:** Keyboard → raw input HAL; the B1 harness as portable code reused unchanged on the boards; B1 on the host with a forced 16-line band; a simulated slow bus (the host flush waits as long as a chosen bus would need, for example SPI at 40 MHz) so frame pacing on slow displays can be tried without a board.
- **Deliverable:** `platform/host/**` input part, `bench/b1/**`.
- **Done when:** B1 host numbers saved in `docs/benchmarks/raw/host.txt`, tagged "host, not representative".

## S3.2 STM32F429I-DISCO prototype (board needed)
- **Steps:** Platform glue (clock, SDRAM, LTDC through the Cube BSP as in the reference repo). Decide and record the orientation first: the panel is 240x320 portrait, so either portrait-native bands or landscape with a transposed copy — B1 numbers name the orientation used. Display HAL = copy band into the LTDC framebuffer; a variant using DMA2D for fill / copy. Record the tearing handling (accept, sync the copy to the scan line, or back buffer + swap). Timing with the DWT cycle counter; B1 printed on LCD + UART; input HAL for the USER button and external buttons / stick (touch arrives in S4.3).
- **Deliverable:** `platform/stm32f429i_disco/**`, flash target.
- **Done when:** the user confirms the scene on the board and the printed B1 numbers are saved in `docs/benchmarks/raw/f429.txt`.

## S3.4 Benchmark report and decision review
- **Steps:** Tabulate B1 for host and F429; compare with the calculated bounds from S1.3; confirm or revise K2, K3, K4, K15 and the default band size per profile; set the first realistic frame-rate targets per hardware class from the measurements; log decisions in `STATUS.md`.
- **Deliverable:** `docs/benchmarks/stage3.md`.
- **Gate G3 (user).**
