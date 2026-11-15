# Plan Review Log: Pixel Game Engine (PGE) staged plan
Started 2026-11-15. MAX_ROUNDS=5.

Files under review: `PLAN.md` (index) and `plan/PLAN-S1-research.md` … `plan/PLAN-S7-docs-release.md`.

## Round 1 — GLM (session 8c410957-1044-4bb8-b8df-2f0ee5a7c105, 11 turns, 224 s, served by glm-5.3)

# Plan review — PGE staged plan (round 0)

Read all ten files. Plan solid overall: session protocol tight, K12 honest-numbers rule good, golden band-vs-full test good, spec's 7 stages + 27 sessions all present, all 18 modules from spec §8 mapped, GPLv2 contamination avoided. No HIGH findings. 15 non-blocking findings below.

## [MED] findings

1. **[MED] Camera scaling missing.** Spec §5 "Camera scaling, if feasible" — appears nowhere: not in S2.2 renderer API, S4.3 camera, or the out-of-scope list. Only spec §5 feature with no decision recorded. Fix: one line in S2.2 — defer free scaling, optional integer 2× in S5.6, or explicit out-of-scope.

2. **[MED] F429I-DISCO panel orientation unaddressed.** Board LCD is 240x320 portrait (ref demo is 240x320; DOOM port does rotated output). K2/S3.2 assume landscape 320x240 bands; physical framebuffer is transposed. Band flush needs a transpose blit or portrait-native UI — cost and numbers differ. Fix: S3.2 must state orientation strategy before its B1 numbers are trusted.

3. **[MED] LTDC tearing ignored.** R2 covers ILI9341/SPI tearing only. S3.2 copies bands into a live-scanning LTDC framebuffer — partial updates tear on moving content. Fix: extend R2 to LTDC; S3.2 notes accept-tearing or scanline-synced copy, and S4.4 gameplay checks visibility.

4. **[MED] Per-band render callback contract unstated.** K2 re-runs game render code once per band. Any non-idempotent render code (RNG, particle spawn, state mutation) → seams or divergent bands. S2.2 done-when covers blocking/ISR-safety but not repeatability. Fix: S2.2 add contract — render callback pure, RNG seeded per frame not per band.

5. **[MED] Interactive tiles absent.** Spec §6 lists "Interactive tiles" as world system. S4.4 gives tile flags (solid/one-way/hazard); S2.3 world API has no tile activation/interaction hook. Inventory/crafting/AI/health split is explicit — this one dropped. Fix: add tile-use/activate hook to S2.3 item 1.

6. **[MED] Spec-traceability root cause.** Findings 1 and 5 leaked because plan rows are the only carrier of spec requirements — no session after S1.4 reads the spec. STATUS.md already has a feature tracker but nothing seeds it with the full spec §3/§5/§6/§7 checklist. Fix: S1.4 seeds the STATUS.md feature tracker with every spec-listed feature mapped to a session.

7. **[MED] Five sessions exceed own size caps.** S3.1 (band core + SDL backend + input HAL + B1 + tests), S4.2 (primitives + flips + sheets + animation + font + golden tests), S5.1 (chunk store + LRU + lazy load + generator with noise/caves/ore + tests), S5.3 (three storage backends + save format + resource manager + tests — clearly >8 files), S7.2 (five Tier B skeletons + porting guide + PlatformIO + CubeIDE + VS Code + release). Protocol rule 5 splits them mid-flight anyway — better to pre-split. Fix: split each into two rows now (e.g. S3.1a core+SDL+tests, S3.1b input+B1; S5.3a format+host+resources, S5.3b FatFs+LittleFS; S7.2a Tier B skeletons, S7.2b docs+release).

## [LOW] findings

8. **[LOW] USB HID dangling.** Out-of-scope says "USB HID host input before Stage 7" — implies Stage 7 has it; S7 has none. Fix: mark fully out of scope or add Tier-B skeleton note in S7.2.

9. **[LOW] F429 touch never assigned.** S3.2 input HAL lists USER button + external stick only; S4.3 claims F429 input "finalized". Board has touch (ref demo uses it). Fix: assign STMPE811 touch to S3.2 or S4.3 explicitly.

10. **[LOW] B1 has no scrolling case.** Static benches under-represent real tilemap cost (chunk fetch during scroll). S4.4 frame-time bench partly covers. Fix: add scrolling-tilemap item to B1 or to S4.4 bench.

11. **[LOW] Gate numbering skips G5.** G1–G4, G6 exist; stage 5 end has no gate and the gap invites confusion. Fix: add G5 after S5.6 (natural checkpoint before example game) or note the skip deliberately.

12. **[LOW] S4.3 board work unmarked.** "F429 and S3 input backends finalized" needs boards flashed; session not tagged board-needed. Fix: tag it.

13. **[LOW] Cube BSP license unhandled.** K13 covers MIT + GPLv2 only; STM32CubeF4 HAL/BSP is BSD-3-Clause — needs attribution notice in the S7 release. Fix: add third-party-notice file to S7.2.

14. **[LOW] Spec §7 "Configuration files" missing** from S5.3 storage scope. Fix: one line in S5.3 (same LittleFS/FatFs backend).

15. **[LOW] Q3 fallback unstated.** If user answers "wait for 8080 board", S3.3/S4.4/S6.2 board sessions block. Fix: note fallback — SPI breadboard stays Tier A until parallel board exists.

## Checked and correct

- K12 math: 320×240×16 = 1,228,800 bits; 40 MHz → 32.55 fps ✓. Band 320×16 RGB565 = 10,240 B ✓. K8 chunk = 32×32×2 = 2,048 B ✓.
- All ten spec §15 first-response items present in S1.4; all spec §5 color formats and strategies get S1.3 rows; all spec §12 stage deliverables mapped; 4+4+5+4+6+2+2 = 27 sessions as claimed.
- Band-render model matches proven prior art (LVGL draw-buffer + partial render callback) — K2 viable; R1 honestly measured.
- ESP32-S3 double-buffered internal-RAM bands with DMA in flight correctly address R3; write-then-rename + CRC save addresses R6; determinism test in S5.1 addresses K4/K8.

Skipped: no cross-check of reference-repo file lists beyond PLAN.md's own verified-facts section (repos not re-fetched, read-only scope). Risk: findings 2/3 rest on the stated 240x320 portrait fact, which matches the F429I-DISCO board but was not re-verified against the repo.

VERDICT: APPROVED

### Non-blocking follow-ups (from the approval, all [MED]/[LOW])
1 camera scaling · 2 F429 panel orientation · 3 LTDC tearing · 4 render-callback contract · 5 interactive tiles · 6 spec traceability · 7 five oversized sessions · 8 USB HID dangling · 9 F429 touch unassigned · 10 no scrolling benchmark · 11 gate G5 missing · 12 S4.3 not tagged board-needed · 13 Cube BSP license notice · 14 configuration files · 15 Q3 fallback.

### Claude's response (after approval)
Verdict was APPROVED in round 1, so the loop ended (1 of 5 rounds used). All 15 follow-ups were accepted and applied; none rejected. These edits were not sent back to GLM.
- 1 → new K15 (integer output scale only; free zoom out of scope); S2.2, S4.2a, S3.4.
- 2, 3 → S3.2 records orientation and tearing handling before B1 numbers count; R2 extended to LTDC; S4.4 checks tearing by eye.
- 4 → S2.2 done-when now includes the render-callback contract.
- 5 → tile-activation hook in S2.3 and S5.1a.
- 6 → S1.4 seeds the `STATUS.md` feature tracker from spec §3, §5, §6, §7.
- 7 → pre-split: S3.1a/b, S4.2a/b, S5.1a/b, S5.3a/b, S7.2a/b. Sessions: 27 → 32.
- 8 → USB HID host fully out of scope, named in the porting guide as a future item.
- 9, 12 → F429 touch assigned to S4.3; S4.3 tagged "boards needed".
- 10 → scrolling tilemap item added to B1; S4.4 benchmarks while scrolling.
- 11 → gate G5 after S5.6.
- 13 → third-party notices file in S7.2b.
- 14 → configuration file in S5.3a.
- 15 → Q3 fallback written next to Q3.

## Result
APPROVED after 1 round. Waiting for the user's sign-off (human gate #2). No code written.

## After the review — user answers (2026-11-15)
- Q1: MIT accepted.
- Direction: focus on the STM32F429I-DISCO now, while the engine must stay universal for many microcontrollers.
- Q3: user asked Claude to choose → SPI ILI9341 module for the ESP32-S3 port; 8080 parallel later as one more display driver.
- Q2 (RP2040 / RP2350 / STM32H7 boards): not answered; default stays "skeleton, untested".

Changes made by Claude (not re-reviewed by GLM): K1 rewritten (F429 only until the minimum engine works; ESP32-S3 becomes session S4.5, the universality proof); former S3.3 removed; S3.1b gains a simulated slow bus on the host; S4.3 / S4.4 run on host + F429 only. Session count unchanged: 32.
