# Part S2 — Architecture (Stage 2)
_Docs and API specifications only. Header listings here are labeled "API spec — not implementation". Four sessions. Index and rules: `PLAN.md`._

## S2.1 Overview, modules, memory
- **Inputs:** `docs/research/00-stage1-report.md` (full), `STATUS.md`.
- **Steps:** 1) Module map for the spec's 18 modules: purpose, depends-on, core or optional, which memory it owns. 2) Dependency rules (core never includes platform headers; dependencies point one way; hardware code only under `platform/`). 3) Mermaid module diagram. 4) Memory strategy: arenas, pools, compile-time config, per-module RAM / flash budget table for three profiles (small ≈ 64 KB RAM class, medium ≈ 256–512 KB, large = external RAM). 5) Capability-profile definition. 6) Portability and naming rules. 7) Directory tree.
- **Deliverable:** `docs/architecture/01-overview.md`.
- **Done when:** each of the 18 modules appears in the map and the diagram; each major decision has a tradeoff line.

## S2.2 HAL, renderer and game API
- **Inputs:** 01-overview summary; K2–K7.
- **Steps:** 1) HAL interface specs: display (init, capabilities, flush, tear / vsync hint, async flush completion), time, raw input, audio out, storage, debug out. 2) Renderer API: band lifecycle, clip, primitives, sprite, sheet, tilemap, text, layers, camera, parallax, integer output scale (K15). 3) Game-facing API: lifecycle callbacks, fixed tick, input queries; an improved version of the spec's sample game with the reason for each difference. 4) Error-handling and init-check policy. 5) Per-call cost notes (what is O(pixels), what is O(1)).
- **Deliverable:** `docs/architecture/02-hal-render-game-api.md`.
- **Done when:** the sample game is expressible with the listed API only; every HAL function states its contract (blocking or not, ISR-safe or not, who owns the buffer). The render-callback contract is written down: it runs once per band, must not change game state, and any randomness it uses is seeded per frame, not per band.

## S2.3 World, entity, storage, resource, audio, UI API
- **Inputs:** 01-overview summary; K8, K9.
- **Steps:** 1) Chunk format, cache, generation hook, change-list format, world bounds, tile-activation hook for interactive tiles (doors, chests, switches). 2) Tile property table. 3) Entity pools, collision queries, physics step. 4) Storage and resource-loading interface; save format with a version field and an integrity check. 5) Audio mixer interface. 6) UI / HUD and debug-overlay interface. 7) RAM / flash / CPU estimate per subsystem (calculated, measured later).
- **Deliverable:** `docs/architecture/03-world-entity-storage-api.md`.
- **Done when:** the engine-level vs game-level split is explicit for inventory, crafting, AI and health.

## S2.4 Architecture review
- **Inputs:** the three architecture docs.
- **Steps:** Run `il-glm-review` with `rounds=3` and `PLAN_FILE=docs/architecture/ARCH-REVIEW.md` (a short digest that points GLM at the three docs; it must not overwrite `PLAN.md`). Apply accepted findings; log rejected ones with reasons.
- **Deliverable:** updated docs; `docs/architecture/REVIEW-LOG.md`.
- **Gate G2 (user):** approve the architecture before any code.
