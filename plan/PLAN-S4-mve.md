# Part S4 — Minimum viable engine (Stage 4)
_Six sessions. Each ends with host tests passing. Public headers get doc comments. Index and rules: `PLAN.md`._

## S4.1 Core
- **Steps:** Engine lifecycle (init checks returning error codes, no exceptions); fixed-timestep loop with frame pacing and overrun handling; time HAL use; compile-time config + capability-profile header; arena and pool allocators; debug counters (frame time, band time, flush time, pool high-water marks); tests for pools, arena and the timestep accumulator.
- **Done when:** `ctest` passes; the host app runs an empty game at the configured tick rate and prints the counters.

## S4.2a Graphics: primitives and text
- **Steps:** Primitives (pixel, line, rectangle, filled rectangle, circle); clipping; RGB565 color helpers + palette; bitmap-font text (one built-in font); integer output scale (K15); golden-buffer tests including clip edges and band-vs-full equality.
- **Done when:** tests pass; the host demo shows every primitive and text.

## S4.2b Graphics: sprites
- **Steps:** Sprites (opaque, color-key transparent, horizontal / vertical flip); sprite sheets; frame-animation helper; rotation in 90° steps only (free rotation deferred); golden-buffer tests including partly clipped and flipped sprites across a band boundary.
- **Done when:** tests pass; the host demo shows an animated, flipped, clipped sprite.

## S4.3 Tilemap, camera, input (board needed)
- **Steps:** Fixed-size tilemap (the chunked world arrives in S5.1a behind the same draw call); multi-layer draw; camera with world-bounds clamp; parallax factor per layer; unified input (buttons: pressed / held / released; axes; touch point) with a mapping table per platform; host and F429 (buttons, stick, touch panel through the BSP) input backends finalized.
- **Done when:** tests for camera clamp and input edge detection pass; a scrolling map runs on the host; the board shows its inputs reacting.

## S4.4 Collision and platformer example (board needed)
- **Steps:** AABB-vs-tile collision with per-axis resolve; tile property flags (solid, one-way platform, hazard); a simple gravity / jump controller inside the example (game code, not engine code); `examples/platformer` on host and F429; frame-time benchmark while scrolling; tearing checked by eye on the board.
- **Deliverable:** working platformer; `docs/benchmarks/stage4.md`.
- **Gate G4 (user):** play it on a board.

## S4.5 Second port: ESP32-S3 (board needed)
- **Steps:** ESP-IDF component + app; ILI9341 over SPI with DMA, queued band transactions, band buffers in internal RAM (two buffers: draw one while the other is in flight); button input backend; SPI clocks 26 / 40 / 80 MHz tried; B1 over UART; run `examples/platformer` unchanged.
- **Deliverable:** `platform/esp32s3/**`; `docs/benchmarks/raw/esp32s3.txt`; a short comparison with the simulated-bus numbers from S3.1b.
- **Done when:** the platformer runs on the board with no change under `core/`. Any core change that was needed is logged in `STATUS.md` as a portability defect and fixed in the core, not in the port.
