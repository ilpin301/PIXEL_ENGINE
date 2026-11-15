# Plan: Pixel Game Engine architecture (review digest)
_Round 0 — digest by Claude for GLM review at gate S2.4_

## Goal
Portable C++17 engine for microcontrollers. No heap after init. Each frame is drawn in horizontal bands through a small C HAL. First targets: host SDL2 simulator and STM32F429I-DISCO (portrait 240x320 panel). Second: ESP32-S3 with SPI ILI9341 (S4.5), to prove the core needs no change. Other boards are Tier B, untested skeletons.

## Documents to read
- `docs/architecture/01-overview.md`: module map (18 modules, 6 layers), dependency rules R-1..R-6, RAM budgets, profiles, directory tree.
- `docs/architecture/02-hal-render-game-api.md`: 6 C HAL headers, flush contract, band render callback, game template, error policy, D7-D12.
- `docs/architecture/03-world-entity-storage-api.md`: chunked world, entities, physics, save format, storage, audio, UI, RAM additions, D13-D19.

Reviewer: read these three files plus `PLAN.md` lines 41-56 only. Do not Grep or Glob outside `docs/` and `PLAN.md`. Never read `.env` or any secrets file.

## Approach
1. Five layers (L0 hardware to L4 game systems); dependencies point down only (R-2). `core/` and `hal/` include no vendor header (R-1).
2. Hardware is reached only through six C HAL headers (display, time, input, audio, storage, debug). One implementation is linked per binary (K7, R-4).
3. Render path: one band renderer (K2). `onRender(Canvas&)` runs once per band under a clip rectangle. Band buffer is RGB565 (K3).
4. Display flush: one outstanding flush; caller owns the buffer until `display_wait` returns; platform byte-swaps and transposes for the portrait panel (D7, D8). Renderer clears each band before `onRender`. Present: with PGE_DISP_VSYNC flush writes the back buffer and `frame_end` swaps; otherwise tearing is accepted (R2).
5. Integer scale 1/2/3 (K15): bands are expanded into a staging buffer at flush (D12).
6. Memory: init arena, frame arena, fixed `Pool<T,N>`; no heap after `pge_init` (K5, D2).
7. Compile-time profiles SMALL / MEDIUM / LARGE in `pge_profile.h` (K14, D4). Optional modules compile out (D5).
8. Game API: static-dispatch `run<G>`; `onTick(const Input&)` at fixed rate; no float in core (K4, D10).
9. World: 32x32 chunks, 2 layers, 1 B per tile. Regenerated from seed plus sparse change log; eviction without write-back (K8, D13).
10. Entities: fixed pools, parallel arrays. Health and damage are engine code; AI and crafting are game code (K9, D15).
11. Save: 32 B header with CRC-32, TLV sections, streaming two-slot write (remove `.bak`, then rename; D16). Change-log cap is a gameplay limit (D19). Assets: compile-time ids, zero-copy ROM pointers (D17).

## Key decisions & tradeoffs
K1 Host SDL2 plus F429I-DISCO first; ESP32-S3 in S4.5 (PLAN.md 42)
K2 One band render path; callback re-run per band (PLAN.md 43)
K3 RGB565 band buffer; assets indexed 4/8 bpp (PLAN.md 44)
K4 Fixed timestep; integer and fixed-point maths; no float in core (PLAN.md 45)
K5 No heap after init; arenas and pools (PLAN.md 46)
K6 C++17, no exceptions, no RTTI, no allocating STL (PLAN.md 47)
K7 HAL is C free functions, link-time bound (PLAN.md 48)
K8 World: 32x32 chunks, regenerate plus change list (PLAN.md 49)
K9 Entities: fixed pools, parallel arrays, no ECS (PLAN.md 50)
K10 Host-only CTest unit tests, no framework (PLAN.md 51)
K11 Tier A boards measured; Tier B untested (PLAN.md 52)
K12 Every number tagged calculated or measured (PLAN.md 53)
K13 MIT license (PLAN.md 54)
K14 Capability profile switches optional modules (PLAN.md 55)
K15 Integer output scale only, no free zoom (PLAN.md 56)
D1 Six HAL headers, C-compatible (01 s9)
D2 Three allocators, no general malloc (01 s9)
D3 Placement macros PGE_RAM_FAST/DMA/EXT/ROM (01 s9)
D4 Named profiles SMALL/MEDIUM/LARGE (01 s9)
D5 Optional modules compile out; host builds all three profiles (01 s9)
D6 Asset data as pointer plus size (01 s9)
D7 Core renders in asked orientation; platform transposes (02 s7)
D8 One outstanding flush; buffer undefined afterwards (02 s7)
D9 Canvas passed per band; no global renderer (02 s7)
D10 Static-dispatch game template, Input as parameter (02 s7)
D11 Draw calls silent-clip with counters; init and I/O return Error (02 s7)
D12 Scale staging buffer; profile gains scale_staging_bytes (02 s7)
D13 Change log plus regenerate; evict without write-back (03 s9)
D14 Tile ids 0 air and 255 void (03 s9)
D15 Engine owns Health and events; AI, crafting, inventory are game code (03 s9)
D16 Two-slot streaming save, CRC-32, TLV sections; remove plus rename-to-free-name (03 s9)
D17 Asset ids compile-time; no compression in v1 (03 s9)
D18 Audio SPSC queue plus HAL fill callback; stateless UI widgets (03 s9)
D19 Change-log cap is a gameplay limit; ChangeLogNearFull at 90%; snapshot compaction deferred (03 s9)

## Risks / open questions
- R1 Per-band render cost; measure in S3.1a (02 s3.3, s6).
- R2 Tearing handling; S3.2.
- R3 ESP32-S3 band buffer placement in internal RAM (01 s6).
- R6 Flash wear; engine does no periodic writes (03 s4.1).
- R8 DISCO audio: SAI1 or TIM9 PWM; DAC_OUT2 conflicts with stick ADC; confirm on board (STATUS S1.2).
- R10 Portrait panel on DISCO (D7); transposing copy cost measured in S3.2.
- R11 ILI9341 SPI clock: datasheet 10 MHz, so about 8 fps ceiling at 320x240 (STATUS S1.3).
- R12 STM32 numbers come from mirrors, not st.com; re-check (STATUS S1.4).
- R13 Engine/game split for health, AI, crafting; resolved at G1 (STATUS).
- Open: Fix 16.16 world limit (2,047 tiles at 16 px) acceptable for v1, confirm at G2.
- Open: tick overrun and catch-up policy (S4.1). Change-log insert O(n) (S5.1a).
- Open: ESP32-S3 display interface, 8080 or SPI. TE pin on user module. UNKNOWN: DMA reach on F429, chunk generation time, flash size.

## What to attack
This attack list is the round-0 record; round 1 and 2 outcomes are in REVIEW-LOG.md.
1. Render callback re-run per band: total cost = per-band cost x bands (R1). Is this acceptable on SPI-bound DISCO and ESP32-S3? The no-state-change rule is a contract only; nothing enforces it.
2. One outstanding flush (D8): does the two-buffer overlap really hide draw time, or does `display_wait` stall it?
3. Fix 16.16 world limit: 2,047 tiles per axis at 16 px. Is it acceptable for v1 without a fallback?
4. No heap after init vs LRU chunk cache: fixed cache and change log. A full log makes `set` fail with OutOfMemory. Is there a real recovery path?
5. Regenerating chunks without write-back: relies on a pure generator and on every edit going through the log. Chunk generation time is UNKNOWN, and only 1 load per tick is allowed.
6. Static-dispatch game template (D10): compile-time coupling per game, and unusual for Arduino-style users. Is that a fair tradeoff?
7. RAM budgets across 01/02/03: 03 totals exclude scale staging (D12) for every profile. Are the "new totals" really the cap?
8. HAL link-time binding (K7): one platform per binary. Can DISCO and ESP32-S3 drivers coexist in one test matrix, or does the matrix grow with every board?

## Out of scope
Flash size estimate (no code yet). Real board measurements (S3 onward). Tier B boards. Tick overrun policy (S4.1). Audio output on DISCO (S5.5). Lighting, animated or liquid tiles, asset compression. Arduino, PlatformIO and STM32CubeIDE wrappers (S7.2). Prose style of the docs.
