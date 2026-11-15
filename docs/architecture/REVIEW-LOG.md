# Architecture review log (S2.4)

MAX_ROUNDS=3, reviewer GLM-5.3 read-only, 2026-11-15

## Round 1 — GLM

Read all four docs plus K1-K15. Recomputed every number first: all RAM sums (01 §5.3, 03 §8), staging example (02), audio CPU math, save worst case, tile-sweep counts, cache-size formula — arithmetic all correct. Findings below are logic, contract and hardware issues, not math.

## Findings

1. **[HIGH] Atomic save breaks on FatFs after first save.** 03 §4.1 sequence: rename `path`→`path.bak`, then `path.tmp`→`path`. Second save: `path.bak` already exists; FatFs `f_rename` returns FR_EXIST → HAL contract (02 §2.2) turns that into `Io` → every later save fails. D16 claim "works on any file system / needs only rename to a free name" is false — `.bak` is not free. Autosave silently dead from save #2 → power loss loses all progress. Fix: ping-pong slot files (`save.a`/`save.b` + tiny pointer file, no rename-over) or `remove(path.bak)` before rename.

2. **[MED] Save API forces whole payload in RAM, no budget holds it.** `saveAtomic(path, data, len)` / `writeFile(path, data, len)` take one contiguous buffer; worst-case LARGE payload = 98,304 B + game blob. Storage I/O buffer is 16 KB; frame-arena slab size specified nowhere. Fix: streaming section `Writer` (pull callback) for save, or add a save-staging budget row.

3. **[MED] Undefined band content + optional `clear()` = garbage bands.** D8: after flush "content undefined, platform may byte-swap in place". 02 §3.3 contract does not require `clear()`. Game that draws sprites without clear renders over byte-swapped residue. Fix: renderer clears each band buffer before invoking `onRender`.

4. **[MED] Present/tearing path undefined on LTDC.** HAL has no back-buffer or flip concept; `frame_begin/end` are "hint only; may be empty" (02 §2.2), yet 01 §5.3 budgets 2×153,600 B LTDC framebuffers — implying double buffering the API cannot drive. Bands written into scanned-out buffer = permanent tearing on DISCO. Fix: specify flush targets back buffer and `frame_end` = pointer swap when `PGE_DISP_VSYNC` set.

5. **[MED] "New totals" are not the cap.** Scale staging (~30 KB internal at s=3, LARGE) excluded — acknowledged — but frame-arena slab also unbudgeted anywhere, and full-height band mode (LARGE, "full height allowed") needs 153.6 KB with no placement stated. Fix: add `scale_staging_bytes`, `frame_arena_bytes`, full-height-band placement rows to the profile table now, not S4.2a.

6. **[MED] DISCO has no storage medium.** F429I-DISCO: no SD slot, no QSPI flash. Storage HAL + save format + two-slot write untestable on Tier A board #1; backend (internal-flash FS, USB, or "disabled") never chosen. Fix: state the DISCO storage backend in the platform sheet.

7. **[MED] Change log = lifetime edit budget, no compaction path.** MEDIUM caps player at 4,096 edits ever; full log makes `set` return OutOfMemory mid-gameplay (digest #4 asked for recovery — none designed; D13 only acknowledges). Fix: define overflow policy — bake oldest dirty chunks into snapshot save section, or document cap as gameplay limit.

8. **[MED] Cache formula ignores regions it promises to load.** 03 §2.2: `ensureLoaded` covers view + margin **plus active entity region**; cache count (16/64) derived from view-only formula. Entities or parallax layers outside view thrash LRU at 1 load/tick, UNKNOWN generation cost, unloaded reads as solid void. Fix: size cache on union(view, entity region) and state parallax layers never use the chunked World.

9. **[MED] Event queue overflow unstated.** Capacity 16/32/64; burst damage can drop Death events → 0-hp entities never die, no counter. Audio queue has a defined drop policy; events don't. Fix: drop-oldest + `events_dropped` counter, same as audio.

10. **[MED] `TileLayer{const uint8_t* tiles}` cannot address the chunked world.** Chunks live in scattered LRU slots; 02 §4.3 diff 4 promises chunked world "behind the same `tilemap` call". Signature and storage layout contradict. Fix: specify chunked tile-source variant (iterator/callback) now so D6 pointer+size story survives S5.1a.

11. **[LOW] UI budget contradiction.** 01 §5.3 budgets "UI widget state" 512-2,048 B engine RAM; module map and D18 say widgets stateless, state in game. Drop the row.

12. **[LOW] Phantom "128 B allowance".** 03 §5 cites a 128 B audio allowance "in the 01 table"; 01's audio rows are 2,176/4,224. Fix reference.

13. **[LOW] No quit path.** `run<G>` never returns; no `Engine::quit()`, no window-close surface in HAL. Host simulator needs it. Fix: quit bit in input poll or `requestQuit()`.

14. **[LOW] `frameSeed` source unspecified.** Time-derived seed breaks reproducible renders; host tests want determinism. Fix: seed = hash(tick, constant).

15. **[LOW] Physics CPU estimate assumes internal-RAM lookups.** LARGE puts chunks in SDRAM (DISCO) / PSRAM (ESP32-S3); 10 cycles/lookup won't hold — 1.5% estimate optimistic on both Tier A MCUs. Keep tag, measure S5.2 first; consider per-chunk solid-row cache.

16. **[LOW] No-mutation render contract unenforceable** (digest #1). Fix: host debug build — checksum world+entity state between band 1 and last band render, assert equal.

17. **[LOW] Writer/Reader types in `World::writeChanges` unspecified.** Fine to defer to S5, but list them.

Answering digest attack list directly: #2 two-buffer overlap is sound (flush(B) waits out A before returning, so A is safe to redraw) — no race found there; the real race is #4 (flush vs scan-out). #3 Fix world limit: acceptable for v1, keep fallback documented. #6 static dispatch: fair tradeoff, wrapper outside core already planned. #8 link-time binding: fine, host slow-bus sim covers the matrix. #1 per-band cost: honestly tagged, measured S3.1a.

Done: read ARCH-REVIEW + 01 + 02 + 03 + PLAN 41-56; verified all arithmetic; 1 HIGH, 9 MED, 7 LOW findings above.
Not done: no board/datasheet re-verification beyond facts already tagged in docs (R12 unchanged); no code exists yet to check against.
Your next step: apply the one-line fixes for findings 1-10 (finding 1 is blocking), then re-submit for round 1.

VERDICT: REVISE

### Claude's response

All 17 findings handled (none rejected). New decision: D19 (03 s9), change-log overflow policy.

1. ACCEPTED - 03 s4.1 (saveAtomic: remove path.bak first, rename path to path.bak, rename path.tmp to path; load order path, path.bak, path.tmp ignored), 03 D16 and summary, 02 s2.1 and s2.2 (rename target must not exist, callers remove first; `pge_hal_storage_remove` already existed), ARCH-REVIEW D16.
2. ACCEPTED - 03 s4.1 (streaming `Producer`/`Consumer`, `saveAtomic`/`loadSave` signatures changed, `SaveInfo`), 03 s8 and 01 s5.3 ("Save staging" row = 0, reuses the I/O buffer).
3. ACCEPTED - 02 s3.1 (`clear_color`), s3.2, s3.3 item 7, s6 cost row, D8 note.
4. ACCEPTED - 02 s2.1 (PGE_DISP_VSYNC, frame_end), s2.2 table row, s3.2, summary; 01 s5.3 note (2 x 153,600 B LTDC buffers = the two present buffers); ARCH-REVIEW item 4.
5. ACCEPTED - 01 s5.3 (rows `frame_arena_bytes` 2,048 / 8,192 / 16,384 [assumed]; `scale_staging_bytes` 0 / 20,480 / 30,720 [calculated] outside totals; full-height band placement note; new totals 16,512 / 77,696 / 90,496 internal), 01 s6 field list and band_lines row, 03 s8 and summary totals (20,276 / 101,744 / 101,840 internal), 02 D12.
6. ACCEPTED - 01 s6 (storage backend per Tier A board: DISCO USB-host mass storage + FatFs [assumed], NotSupported fallback; ESP32-S3 SD or LittleFS; host files), 01 s10 (S3.2 / S5.3b dependency).
7. ACCEPTED, DIFFERENT FORM - 03 s2.1 (`changeCount()`, `changeCapacity()`, `changeLogNearFull()`), s2.3 (policy), s9 D19, summary. No in-engine compaction (needs write-back, contradicts D13): the cap is a gameplay limit; the game stops edits with a message; the snapshot-section option is documented as deferred to S5.1a.
8. ACCEPTED - 03 summary cache-sizing rule (bounding box of view + margin and entity region; MEDIUM 16 / SMALL 4 unchanged [calculated]), s2.2 ensureLoaded; 02 s3.1 (parallax layers never use the chunked World).
9. ACCEPTED - 03 s3.1 (`eventsDropped()`), s3.2 (drop-oldest, hp authoritative, optional AUTO_REAP flag so engine-never-despawns of D15 stays the default), s6 Stats `events_dropped`, summary.
10. ACCEPTED - 02 s3.1 (`TileSource`, `TileLayer.source`, tile-source note), s4.3 difference 4; 03 s2.1 (`World::source`).
11. ACCEPTED - 01 s5.3 ("UI widget state" row deleted, totals recomputed with arithmetic).
12. ACCEPTED - 03 s5 State bullet (128 B state inside the 2,176 / 4,224 audio rows, mix buffer 2,048 / 4,096 B).
13. ACCEPTED - 02 s2.1 (`PGE_INPUT_QUIT`), s4.1 (`Engine::requestQuit()`, `run<G>` returns), summary.
14. ACCEPTED - 02 s3.3 items (frameSeed = hash(tick count, `PGE_GAME_SEED`), never wall-clock), summary.
15. ACCEPTED - 03 s8 CPU paragraph (SDRAM/PSRAM lookups cost more; 1.5% optimistic until S5.2; per-chunk solid-row cache fallback).
16. ACCEPTED - 02 s3.3 item 2 (host debug-build checksum of engine-owned world and entity state before first and after last band, assert equal).
17. DEFERRED - 03 s9 open items (Writer/Reader types for `World::writeChanges`/`readChanges` specified in S5.1a / S5.3a).

## Round 2 — GLM

Re-read all five files. Verified every changed number before judging: recomputed 01 §5.3 totals (SMALL 16,512, MEDIUM 77,696, LARGE internal 90,496 — all correct), 03 §8 additions and new totals (20,276 / 101,744 / 101,840 / 448,512 — correct), staging math (16×2×320×2 = 20,480; 16×3×320×2 = 30,720 — correct, matches the (N·s)×(W·s)×2 example), audio decomposition (2,176 = 128+2,048; 4,224 = 128+4,096 — now consistent), cache formula (both-sides margin reading: MEDIUM (320+1024)/512→3+1=4, (240+1024)/512→3+1=4 = 16 — matches; SMALL 2×2=4 — matches).

## Prior findings — all addressed

1. HIGH (FatFs save): **fixed.** New sequence remove(.bak) → rename(path→.bak) → rename(tmp→path); rename contract weakened to "target must not exist, callers remove first" (02 §2.1/§2.2). Crash windows all recoverable: after (4) path valid; after (5) bak valid and load order falls back to it; partial tmp ignored. No data loss beyond one save interval (inherent to two-slot).
2. MED (payload buffer): **fixed.** Streaming `Producer`/`Consumer`, staging rows = 0, I/O buffer reuse. Two-pass produce with "same bytes in both passes" assumption stated in D16.
3. MED (clear): **fixed.** `clear_color` in RenderConfig, band pre-cleared, contract §3.3.7, cost row added.
4. MED (present path): **fixed** — flush→back buffer, `frame_end` swaps, LTDC buffers tied to the budget note — but the fix itself opens the one MED below.
5. MED (totals): **fixed.** `frame_arena_bytes` in totals, `scale_staging_bytes` listed outside, full-height placement stated.
6. MED (DISCO storage): **fixed.** USB-host MSC + FatFs [assumed], NotSupported → RAM-only, dependency logged in 01 §10.
7. MED (change log): **fixed** as D19 — honest gameplay-limit framing, 90% `ChangeLogNearFull`, snapshot option deferred with the write-back conflict named.
8. MED (cache formula): **fixed.** Bounding box of view+margin and entity region; parallax pinned to flat arrays (02 §3.1).
9. MED (events): **fixed.** Drop-oldest + `eventsDropped()` + hp authoritative + AUTO_REAP so 0-hp entities never strand. AUTO_REAP keeps D15 default intact — consistent.
10. MED (TileLayer): **fixed.** `TileSource.chunkRow` callback, read-only, unloaded → void, "exactly one of tiles/source".
11-16. LOWs: **fixed** (UI row deleted, audio bytes reconciled, quit path, frameSeed = hash(tick, PGE_GAME_SEED), SDRAM honesty paragraph, host debug checksum enforcement).
17. LOW (Writer/Reader): deferred, logged as open item — acceptable.

## New findings from the fixes

1. **[MED] Present-path latch race.** 02 §2.2: "frame_end **may** wait for vsync if PGE_DISP_VSYNC". With LTDC shadow reload, the swap latches at the *next* vblank; if `frame_end` returns before the latch, the engine starts frame N+1 band 1 into the buffer LTDC is still scanning → top-of-frame tear, which is exactly what the VSYNC flag exists to prevent. Fix: one line — "PGE_DISP_VSYNC set: frame_end blocks until the reload is latched".

2. **[LOW] Misleading budget headline.** 01 §5.3 "Reading" still quotes "MEDIUM at 77,696 B" as the footprint; the real MEDIUM engine total is 101,744 (03 §8), 122,224 with staging. Fix: point the reading line at the 03 §8 totals.

3. **[LOW] Cache-formula wording.** "view + margin m chunks" is ambiguous (one side vs both sides). Only the both-sides reading (+2m total) yields the claimed 16. Fix: "m chunks on each side".

4. **[LOW] `remove(path.bak)` first-save tolerance.** Step (4) hits a missing file on first save and after a crash-between-(4)-and-(6) restart; "(if it exists)" guard is written for step (5) only. Fix: same guard on step (4).

5. **[LOW] Digest staleness.** ARCH-REVIEW.md still labeled "Round 0" with the round-0 attack list (e.g. #4 now answered by D19, #7 by the new rows). Fix: one line noting attack list = round 0 record.

No HIGH remains. The new save flow, streaming API, AUTO_REAP, TileSource and present path are all implementable and internally consistent after the fixes; the one MED is a contract sentence, not a structural change.

Done: re-read ARCH-REVIEW, 01, 02, 03, REVIEW-LOG; verified all 17 round-1 findings resolved (16 fixed, 1 deferred+logged); recomputed all new RAM totals and formulas; found 1 new MED, 4 new LOW.
Not done: nothing else pending on my side; board-level UNKNOWNs (DMA reach, chunk gen time, TE pin) stay open by design until S3/S5.
Your next step: apply the frame_end latch sentence (MED 1) and the four LOW wording fixes, log them in REVIEW-LOG as round 2, then this is ready for the human gate (G2).

VERDICT: APPROVED

### Claude's response

1. ACCEPTED - 02 s2.1 (flag text, flush-contract paragraph) and s2.2 frame_begin / frame_end row: with PGE_DISP_VSYNC, frame_end blocks until the swap / reload is latched.
2. ACCEPTED - 01 s5.3 Reading line now points to the 03 s8 totals (MEDIUM 101,744 B; 122,224 B with scale staging at 2x).
3. ACCEPTED - 03 summary cache-sizing rule: "margin of m chunks on each side".
4. ACCEPTED - 03 s4.1 saveAtomic step (4): remove(path.bak) ignores a missing file (NotFound).
5. ACCEPTED - ARCH-REVIEW.md, line under "What to attack": attack list = round-0 record, rounds 1 and 2 in REVIEW-LOG.md.

## Resolution

APPROVED in round 2 of 3 (VERDICT: APPROVED, no HIGH). Non-blocking follow-ups: round-1 #17 (Writer/Reader types, S5.1a/S5.3a); board UNKNOWNs (DMA reach, chunk gen time, TE pin) stay open until S3/S5. Awaiting Gate G2 (user).
