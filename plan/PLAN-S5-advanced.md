# Part S5 — Advanced features (Stage 5)
_Eight sessions. Each is an optional module switched by the capability profile. Each is done when host tests pass and the `STATUS.md` feature tracker is updated. Index and rules: `PLAN.md`._

## S5.1a Chunked world: store and cache
- **Steps:** Chunk store (K8); LRU cache; lazy load around the camera; world bounds; tile-activation hook for interactive tiles; a trivial flat generator as placeholder. Tests: cache eviction, an edit survives unload / reload, bounds.

## S5.1b Procedural generation
- **Steps:** Generation hook + default generator (seeded 1D height noise, caves, ore). Tests: determinism (same seed → same chunk bytes on every run). Chunk generation time measured on the host; boards in S6.2.

## S5.2 Entities, physics, animation
- **Steps:** Fixed pools (K9); spawn / despawn without heap; defined update order; gravity + tile sweep collision; entity-vs-entity overlap queries (uniform grid); animation state per entity; particles as a pooled entity kind. Tests: pool exhaustion behaviour, collision cases. Health component and damage event (engine level; game decides what dies and what drops). Tests: damage, death event, invulnerability frames.

## S5.3a Save format, host storage, resources
- **Steps:** Storage interface + host file backend. Save format: header (magic, version, CRC), seed, change lists (run-length packed), player / inventory blob. Write-then-rename so a power loss keeps the previous save. Configuration file (key = value settings) through the same interface. Resource manager: assets compiled into flash or loaded from storage through one interface. Tests: round trip, corrupt file rejected, truncated write recovers the previous save.

## S5.3b Embedded storage backends
- **Steps:** FatFs (SD / USB) and LittleFS (flash) backends behind the S5.3a interface; the S5.3a tests rerun on the host against a RAM-disk fake of each; on-board check deferred to S6.2.

## S5.4 Asset tools + memory report
- **Steps:** Python scripts in `tools/`: PNG → indexed sprite sheet (C array and binary); palette conversion to RGB565; tilemap import from CSV and Tiled JSON; linker map → per-module RAM / flash report. Each script has a self-check on a tiny sample asset.

## S5.5 Audio
- **Steps:** Mixer with N channels (N from the profile); 8-bit / 16-bit PCM effects; simple pattern music. Backends: host SDL audio, ESP32-S3 I2S; an F429 backend only if S1.2 found a usable output, otherwise documented as absent. CPU cost measured.

## S5.6 UI, debug overlay, render options
- **Steps:** HUD / menu widgets (label, bar, icon grid, list menu); debug overlay (fps, frame / band / flush time, pool usage). Optional: dirty-tile skipping for static scenes; tile lighting (capability-gated; cost measured before it is enabled by default anywhere).
- **Gate G5 (user):** review the feature tracker before the example game starts.
