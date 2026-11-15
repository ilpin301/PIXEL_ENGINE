# Part S1 — Research and feasibility (Stage 1)
_Docs only. No engine code. Four sessions. Index and rules: `PLAN.md`._

Every deliverable starts with "Summary for later sessions" (≤ 40 lines) and tags each statement **[verified]** (source named), **[calculated]** (formula shown) or **[assumed]**.

## S1.1 Reference repositories
- **Inputs:** GitHub `il721/ESP32-DOOMGAD`: `README.md`, `ESP-gadjet-hardware.md`, `breadboard/platformio.ini`, `breadboard/src/main.cpp`, `breadboard/wiring.md`. GitHub `il721/STM32F429I-DISCO`: `Core/**`, `DOOM/README.md`, `DOOM/port/doom_main.c`, `doom_hw.h`, `doom_glue.c`, `doom_rotate.h`, both `.ld` files, `CMakeLists.txt`, `cmake/arm-none-eabi.cmake`, `WIRING.md`, `Hardware.txt`. Read through `gh api`; do not clone the submodule.
- **Steps:** 1) One investigator subagent per repo returns a table: display path, memory map, input, audio, storage, DMA / DMA2D use, build setup. 2) Extract reusable principles and bottlenecks. 3) List what is absent (no engine code in ESP32-DOOMGAD; no measured numbers). 4) Create `STATUS.md`.
- **Deliverable:** `docs/research/01-reference-repos.md`; `STATUS.md`.
- **Done when:** each of the eight analysis points in spec §2 has a row for each repo, tagged verified or absent.

## S1.2 Hardware comparison and capability profile
- **Inputs:** S1.1 summary; vendor datasheets / reference manuals (web, cite section) for ESP32, ESP32-S3, STM32F429, the STM32F4 family range, STM32H7 (H743 / H750 as representatives), RP2040, RP2350.
- **Steps:** 1) Table: core, clock, FPU, SRAM, external RAM option, flash, DMA, display interfaces (SPI max clock, parallel, LTDC/RGB, PIO), timers, ADC, audio (DAC / I2S / PWM), USB, storage. 2) Typical display pairings and their bandwidth limits (calculated). 3) Draft the capability-profile field list (K14). 4) Answer the F429I-DISCO audio pin question (R8).
- **Deliverable:** `docs/research/02-hardware-comparison.md`.
- **Done when:** every cell has a source or an explicit "unknown"; no number without a tag.

## S1.3 Rendering strategies and prior art
- **Inputs:** S1.1 / S1.2 summaries; documentation of LVGL (draw buffers, partial flush), LovyanGFX or TFT_eSPI (sprite + DMA push), 32blit / PicoSystem SDK, Arduboy2, the SDL2 render model, TinyUSB HID host (scope only).
- **Steps:** 1) Compare full framebuffer, partial / band, scanline, tile-based, dirty rectangles, sprite batching, DMA-assisted flush, direct-to-display — RAM, CPU, tearing, complexity. 2) Memory tables: 160x128, 240x240, 320x240, 480x320 at 8-bit indexed, RGB565, RGB888; band sizes for 8 / 16 / 32 lines. 3) Bus throughput tables (calculated) for SPI 20 / 40 / 80 MHz, 8080 8-bit, LTDC. 4) Confirm or challenge K2 / K3 with reasons. 5) What to borrow from each library and what not.
- **Deliverable:** `docs/research/03-rendering-prior-art.md`.
- **Done when:** every strategy named in spec §5 has a row; every table carries its formula.

## S1.4 Feasibility report (the spec's "first response")
- **Inputs:** summaries of 01–03 only; spec §15.
- **Steps:** Write the ten required items in order — repo analysis (short, links to 01), platform comparison (links to 02), achievable game scale per platform, rendering architecture, memory strategy, module architecture outline, language + build system, top risks / unknowns, prioritized MVP list, directory structure — then the first-target recommendation (K1) with reasons. State which Key decisions are confirmed and which changed (log changes in `STATUS.md`). Seed the `STATUS.md` feature tracker with every feature the spec lists in §3, §5, §6 and §7, each mapped to the session that delivers it or marked out of scope, so later sessions need not reread the spec.
- **Deliverable:** `docs/research/00-stage1-report.md`.
- **Done when:** all ten items plus the final recommendation are present; no fps claim without a tag; every spec feature has a tracker row.
- **Gate G1 (user):** approve the report, answer Q1–Q3.
