# Part S6 — Example game (Stage 6)
_Two sessions. The game uses the public engine API only; an engine gap found here is fixed in the engine, not worked around in the game. Index and rules: `PLAN.md`._

## S6.1 Sandbox core
- **Steps:** `examples/sandbox`: generated world (S5.1), controllable character with gravity and collisions, camera scrolling, tile destruction and placement with a reach limit; on the host. Simple enemy AI (walk, turn at ledge, chase within range) as game code on engine entity pools and health.
- **Done when:** a host run shows dig / place persisting across chunk unload / reload; the example includes nothing outside the public headers (grep check).

## S6.2 Inventory, HUD, save, hardware (boards needed)
- **Steps:** Basic inventory (stack counts, selected slot); HUD; save / load; run on F429 and S3; benchmark frame time, chunk generation time and RAM high-water marks. Crafting as a recipe table in game code (inputs, output, station tile) with a craft menu using engine UI widgets.
- **Deliverable:** `docs/benchmarks/stage6.md`.
- **Gate G6 (user).**
