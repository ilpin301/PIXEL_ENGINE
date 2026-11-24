// Sandbox tests (null host backend, no Engine::run): ground contact, dig / place rules, edit persistence across chunk
// eviction (the S6.1 done-criterion), slime AI and damage, inventory, crafting, the menu, save / load (incl. a
// hostile game blob), the language menu item and render passes.
#include <cstdio>
#include <cstring>
#include "game.h"

#define CHECK(c)                                                          \
  do {                                                                    \
    if (!(c)) {                                                           \
      std::printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); \
      return 1;                                                           \
    }                                                                     \
  } while (0)

static SandboxGame g_main, g_persist, g_ai, g_a, g_b, g_bot;  // large: keep off the stack

// Every game in this file uses this settings file, never the default settings.txt, so a settings.txt in the cwd
// cannot change the language under test.
static const char* kLangFile = "test_sandbox_tmp_settings.txt";

// Each game allocates its world from the init arena; free the previous game's share first.
static pge::Error create(SandboxGame& g) {
  pge::engine().initArena().reset();
  g.settings_path = kLangFile;
  return g.onCreate(pge::engine());
}

static void tick(SandboxGame& g, int n) {
  for (int i = 0; i < n; i++) g.onTick(pge::Input{});
}

static bool onGround(SandboxGame& g) { return (g.entities().meta(g.player())->flags & pge::kEntOnGround) != 0; }

static int testMain() {
  g_main.change_cap = 2;  // so the third distinct edit hits "log full"
  CHECK(create(g_main) == pge::Ok);
  const int col = SandboxGame::kSpawnCol, surf = g_main.surfaceY(col) / 16;
  tick(g_main, 120);
  // (a) standing on the surface
  const pge::Body& pb = *g_main.entities().body(g_main.player());
  CHECK(onGround(g_main));
  CHECK(pb.y + pb.h == surf * 16 && pb.x / 16 == col);
  pge::World& w = g_main.world();
  CHECK(w.get(col, surf, pge::Layer::Fg) == pge::kGenGrass && w.get(col, surf - 1, pge::Layer::Fg) == pge::kTileAir);
  // (b) dig rules
  CHECK(!g_main.dig(col, surf - 1));                 // air
  CHECK(!g_main.dig(col + 8, surf));                 // breakable but 128 px away
  CHECK(!g_main.dig(-1, -1));                        // outside the world
  CHECK(w.set(col + 1, surf, pge::Layer::Fg, 200) == pge::Ok);  // id 200: solid-free, not breakable
  CHECK(!g_main.dig(col + 1, surf) && w.get(col + 1, surf, pge::Layer::Fg) == 200);
  CHECK(w.changeCount() == 1);
  CHECK(w.set(col + 1, surf, pge::Layer::Fg, pge::kGenGrass) == pge::Ok);  // overwrite of the same cell: log stays 1
  CHECK(g_main.dig(col, surf + 1));                  // dirt below the feet, in reach
  CHECK(w.get(col, surf + 1, pge::Layer::Fg) == pge::kTileAir && g_main.held() == pge::kGenDirt);
  // log full: the cap is 2 entries, (col+1, surf) and (col, surf+1) hold them
  CHECK(!g_main.dig(col, surf + 2) && g_main.logFull());
  CHECK(w.get(col, surf + 2, pge::Layer::Fg) != pge::kTileAir);
  // (d) placing: not inside the player (own tile), not on a solid tile, not out of reach
  CHECK(!g_main.place(col, surf - 1));
  CHECK(!g_main.place(col + 1, surf));
  CHECK(!g_main.place(col + 8, surf - 1));
  // (f) a touching slime hurts the player (once, then invulnerable); stomping from above hurts the slime instead
  pge::Entities& ent = g_main.entities();
  pge::Body* sb = ent.body(g_main.slime(0));
  CHECK(sb && ent.health(g_main.player())->hp == 10);
  sb->x = pb.x;
  sb->y = pb.y + 4;
  tick(g_main, 2);
  CHECK(ent.health(g_main.player())->hp == 9);
  tick(g_main, 5);
  CHECK(ent.health(g_main.player())->hp == 9);
  *ent.body(g_main.slime(0)) = pge::Body{16, 16, 12, 10};  // out of the way (unloaded chunk: frozen)
  sb = ent.body(g_main.slime(1));
  CHECK(sb);
  sb->x = pb.x;
  sb->y = g_main.surfaceY(pb.x / 16) - 10;
  const pge::Body* sb1 = sb;
  g_main.teleportTo(pb.x, sb1->y - pb.h - 3);
  ent.motion(g_main.player())->vy = 5 * 256;
  const int hp_before = ent.health(g_main.player())->hp;
  CHECK(ent.health(g_main.slime(1))->hp == 3);
  tick(g_main, 1);
  CHECK(ent.health(g_main.slime(1))->hp == 2);
  CHECK(ent.health(g_main.player())->hp == hp_before);
  CHECK(ent.motion(g_main.player())->vy < 0);
  return 0;
}

static int testPlaceOverlap() {
  // place is refused on a tile a slime overlaps, allowed after it moved away
  CHECK(create(g_ai) == pge::Ok);
  tick(g_ai, 30);
  pge::Entities& ent = g_ai.entities();
  const pge::Body& pb = *ent.body(g_ai.player());
  const int col = pb.x / 16, surf = pb.y / 16 + 1;
  pge::Body* sb = ent.body(g_ai.slime(2));
  CHECK(sb);
  sb->x = col * 16 + 2;
  sb->y = (surf - 3) * 16 + 4;  // floats in the air tile above the player's head
  CHECK(!g_ai.place(col, surf - 3));
  sb->y = 0;
  CHECK(g_ai.place(col, surf - 3));
  return 0;
}

// One tick with the aim stick (raw axes 2 / 3) held and B freshly pressed.
static void digTick(SandboxGame& g, int a2, int a3) {
  pge::Input in;
  pge_raw_input r = {};
  r.axis[2] = (int16_t)a2;
  r.axis[3] = (int16_t)a3;
  in.update(r);
  r.buttons = PGE_BTN_B;
  in.update(r);
  g.onTick(in);
}

static int testAim() {
  CHECK(create(g_ai) == pge::Ok);
  tick(g_ai, 30);
  pge::World& w = g_ai.world();
  const pge::Body& pb = *g_ai.entities().body(g_ai.player());
  CHECK(onGround(g_ai));
  const int col = pb.x / 16, surf = (pb.y + pb.h) / 16;  // feet on the top of row surf; body mid height in row surf - 1
  const pge::Layer fg = pge::Layer::Fg;
  // right / left: the side tile at mid height (made breakable solid first)
  CHECK(w.set(col + 1, surf - 1, fg, pge::kGenDirt) == pge::Ok);
  digTick(g_ai, 32767, 0);
  CHECK(w.get(col + 1, surf - 1, fg) == pge::kTileAir);
  CHECK(w.set(col - 1, surf - 1, fg, pge::kGenDirt) == pge::Ok);
  digTick(g_ai, -32767, 0);
  CHECK(w.get(col - 1, surf - 1, fg) == pge::kTileAir);
  // above the head from the real standing position (body top 2 px into row surf - 1): the tile is row surf - 2
  CHECK(w.set(col, surf - 2, fg, pge::kGenDirt) == pge::Ok);
  digTick(g_ai, 0, -32767);
  CHECK(w.get(col, surf - 2, fg) == pge::kTileAir);
  // diagonal up-right: column of the right side, row above the head
  CHECK(w.set(col + 1, surf - 2, fg, pge::kGenDirt) == pge::Ok);
  digTick(g_ai, 32767, -32767);
  CHECK(w.get(col + 1, surf - 2, fg) == pge::kTileAir);
  // diagonal down-right: column of the right side, row below the feet
  CHECK(w.set(col + 1, surf, fg, pge::kGenStone) == pge::Ok);
  digTick(g_ai, 32767, 32767);
  CHECK(w.get(col + 1, surf, fg) == pge::kTileAir && g_ai.held() == pge::kGenStone);
  // below the feet from the real standing position: the tile stood on, centre column
  CHECK(onGround(g_ai) && w.get(col, surf, fg) != pge::kTileAir);
  digTick(g_ai, 0, 32767);
  CHECK(w.get(col, surf, fg) == pge::kTileAir);
  // above the head with the body top mid-tile (8 px into row surf - 4): the tile is row surf - 5
  g_ai.teleportTo(col * 16 + 2, (surf - 4) * 16 + 8);
  CHECK(w.set(col, surf - 5, fg, pge::kGenDirt) == pge::Ok);
  digTick(g_ai, 0, -32767);
  CHECK(w.get(col, surf - 5, fg) == pge::kTileAir);
  // out of reach is still refused
  CHECK(w.set(col + 8, surf - 4, fg, pge::kGenDirt) == pge::Ok);
  CHECK(!g_ai.dig(col + 8, surf - 4) && w.get(col + 8, surf - 4, fg) == pge::kGenDirt);
  return 0;
}

static int testPersistence() {
  g_persist.chunk_cache = 4;  // exactly the chunks one view needs: anything else is evicted
  CHECK(create(g_persist) == pge::Ok);
  tick(g_persist, 60);
  pge::World& w = g_persist.world();
  const int col = SandboxGame::kSpawnCol, surf = g_persist.surfaceY(col) / 16;
  CHECK(onGround(g_persist));
  CHECK(w.chunksLoaded() <= 4);
  CHECK(g_persist.dig(col, surf));  // grass under the feet becomes the held tile
  CHECK(g_persist.held() == pge::kGenGrass);
  CHECK(g_persist.place(col, surf - 2));  // above the head: in reach, not overlapping
  CHECK(w.get(col, surf, pge::Layer::Fg) == pge::kTileAir && w.get(col, surf - 2, pge::Layer::Fg) == pge::kGenGrass);
  CHECK(w.changeCount() == 2);
  CHECK(w.isLoaded(col >> 5, surf >> 5));
  // far away: the spawn chunk is evicted
  const int far = 100;
  g_persist.teleportTo(far * 16 + 2, g_persist.surfaceY(far) - 14);
  CHECK(!w.isLoaded(col >> 5, surf >> 5));
  CHECK(!w.isLoaded(col >> 5, (surf - 2) >> 5));
  CHECK(w.get(col, surf, pge::Layer::Fg) == pge::kTileVoid);
  tick(g_persist, 30);
  CHECK(w.changeCount() == 2 && w.chunksLoaded() <= 4);
  // and back: regenerated from the generator plus the change log
  g_persist.teleportTo(col * 16 + 2, g_persist.surfaceY(col) - 14);
  CHECK(w.isLoaded(col >> 5, surf >> 5));
  CHECK(w.get(col, surf, pge::Layer::Fg) == pge::kTileAir);
  CHECK(w.get(col, surf - 2, pge::Layer::Fg) == pge::kGenGrass);
  CHECK(w.get(col + 3, surf, pge::Layer::Fg) != pge::kTileAir || w.get(col + 3, surf + 1, pge::Layer::Fg) != pge::kTileAir);
  return 0;
}

static int testSlimes() {
  g_ai.seed = 1234;
  CHECK(create(g_ai) == pge::Ok);
  pge::Entities& ent = g_ai.entities();
  const int world_w = 32 * 32 * 16;
  int min_x = world_w, max_x = 0, start_x[SandboxGame::kMaxSlimes];
  for (int i = 0; i < SandboxGame::kMaxSlimes; i++) {
    CHECK(ent.alive(g_ai.slime(i)));
    start_x[i] = ent.body(g_ai.slime(i))->x;
  }
  CHECK(g_ai.turns() == 0);
  for (int t = 0; t < 600; t++) {
    g_ai.onTick(pge::Input{});
    for (int i = 0; i < SandboxGame::kMaxSlimes; i++) {
      const pge::Body* b = ent.body(g_ai.slime(i));
      if (!b) continue;
      if (b->x < min_x) min_x = b->x;
      if (b->x > max_x) max_x = b->x;
    }
  }
  int moved = 0;
  for (int i = 0; i < SandboxGame::kMaxSlimes; i++) {
    const pge::Body* b = ent.body(g_ai.slime(i));
    if (b && (b->x - start_x[i] > 16 || start_x[i] - b->x > 16)) moved++;
  }
  std::printf("slimes: turns=%u moved=%d x=[%d,%d] deaths=%u\n", (unsigned)g_ai.turns(), moved, min_x, max_x,
              (unsigned)g_ai.deaths());
  CHECK(min_x >= 0 && max_x + 12 <= world_w);
  CHECK(moved >= 1);
  CHECK(g_ai.turns() >= 1);
  return 0;
}

static pge::Color g_buf[PGE_LOGICAL_W * PGE_LOGICAL_H], g_buf2[PGE_LOGICAL_W * PGE_LOGICAL_H];

// Renders one full-screen band; true if nothing was dropped and at least 6 distinct colours show up.
static bool renderOk(SandboxGame& g, pge::Color* buf) {
  pge::Canvas c(buf, PGE_LOGICAL_W, PGE_LOGICAL_H, 0, PGE_LOGICAL_H, 0);
  g.onRender(c);
  if (c.dropped() != 0) return false;
  int distinct = 0;
  pge::Color seen[8] = {};
  for (uint32_t i = 0; i < PGE_LOGICAL_W * PGE_LOGICAL_H && distinct < 8; i++) {
    bool known = false;
    for (int k = 0; k < distinct; k++) known |= seen[k] == buf[i];
    if (!known) seen[distinct++] = buf[i];
  }
  return distinct >= 6;  // sky, terrain, player, outline, text, ...
}

static int testRender() {
  CHECK(renderOk(g_main, g_buf));
  return 0;
}

// One tick with a persistent Input, so pressed() sees real edges between calls.
struct Pad {
  pge::Input in;
  void tick(SandboxGame& g, uint32_t buttons) {
    pge_raw_input r = {};
    r.buttons = buttons;
    in.update(r);
    g.onTick(in);
  }
};

static bool msgIs(SandboxGame& g, const char* m) { return std::strcmp(g.message(), m) == 0; }

struct Snap {
  uint8_t item[8], count[8];
  int sel, hp, px, py;
  uint32_t ticks, changes;
};
static Snap snap(SandboxGame& g) {
  Snap s = {};
  for (int i = 0; i < 8; i++) {
    s.item[i] = g.slotItem(i);
    s.count[i] = g.slotCount(i);
  }
  const pge::Body& b = *g.entities().body(g.player());
  s.sel = g.selected();
  s.hp = g.entities().health(g.player())->hp;
  s.px = b.x;
  s.py = b.y;
  s.ticks = g.ticks();
  s.changes = g.world().changeCount();
  return s;
}
static bool sameInv(const Snap& a, const Snap& b) {
  for (int i = 0; i < 8; i++)
    if (a.item[i] != b.item[i] || a.count[i] != b.count[i]) return false;
  return true;
}
static bool same(const Snap& a, const Snap& b) {
  return sameInv(a, b) && a.sel == b.sel && a.hp == b.hp && a.px == b.px && a.py == b.py && a.ticks == b.ticks &&
         a.changes == b.changes;
}
static bool isDefaultInv(const Snap& s) {
  if (s.item[0] != pge::kGenDirt || s.count[0] != 10 || s.sel != 0) return false;
  for (int i = 1; i < 8; i++)
    if (s.item[i] != 0 || s.count[i] != 0) return false;
  return true;
}

// Fresh game standing on the surface at the spawn column.
static int standUp(SandboxGame& g) {
  CHECK(create(g) == pge::Ok);
  tick(g, 30);
  CHECK(onGround(g));
  return 0;
}

// One-stick board: Up alone aims above the head (and jumps); Up + B digs there and does not jump.
static int testDigUp() {
  CHECK(standUp(g_ai) == 0);
  SandboxGame& g = g_ai;
  const pge::Body& pb = *g.entities().body(g.player());
  const int col = pb.x / 16, surf = (pb.y + pb.h) / 16;
  Pad pad;
  pad.tick(g, PGE_BTN_UP | PGE_BTN_B);  // B press edge tick, nothing above: no jump
  CHECK(onGround(g) && g.entities().motion(g.player())->vy >= 0);
  pad.tick(g, 0);
  CHECK(g.world().set(col, surf - 2, pge::Layer::Fg, pge::kGenDirt) == pge::Ok);
  const int dirt = g.itemCount(pge::kGenDirt);
  pad.tick(g, PGE_BTN_UP | PGE_BTN_B);
  CHECK(g.world().get(col, surf - 2, pge::Layer::Fg) == pge::kTileAir && g.itemCount(pge::kGenDirt) == dirt + 1);
  CHECK(onGround(g) && g.entities().motion(g.player())->vy >= 0);
  pad.tick(g, 0);
  const int headRow = pb.y / 16;
  pad.tick(g, PGE_BTN_UP);
  CHECK(g.entities().motion(g.player())->vy < 0);
  CHECK(g.targetY() == headRow - 1 && g.targetX() == col);  // Up alone already aims above the head
  return 0;
}

static int testInventory() {
  CHECK(standUp(g_ai) == 0);
  SandboxGame& g = g_ai;
  pge::World& w = g.world();
  const pge::Body& pb = *g.entities().body(g.player());
  const int col = pb.x / 16, surf = (pb.y + pb.h) / 16;
  const pge::Layer fg = pge::Layer::Fg;
  // start state
  CHECK(g.slotItem(0) == pge::kGenDirt && g.slotCount(0) == 10 && g.selected() == 0);
  CHECK(g.held() == pge::kGenDirt && g.itemCount(pge::kGenDirt) == 10);
  CHECK(isDefaultInv(snap(g)));
  // stacking: 10 + 89 = 99, the next 5 spill into slot 1
  CHECK(g.addItem(pge::kGenDirt, 89) && g.slotCount(0) == 99 && g.slotItem(1) == 0);
  CHECK(g.addItem(pge::kGenDirt, 5) && g.slotCount(0) == 99 && g.slotItem(1) == pge::kGenDirt && g.slotCount(1) == 5);
  CHECK(g.itemCount(pge::kGenDirt) == 104);
  // takeItem is atomic
  CHECK(!g.takeItem(pge::kGenDirt, 105) && g.itemCount(pge::kGenDirt) == 104);
  // fill the other six slots with ids 3..8, then top up dirt: every slot full
  for (int id = 3; id <= 8; id++) CHECK(g.addItem((uint8_t)id, 99) && g.slotItem(id - 1) == id);
  CHECK(g.addItem(pge::kGenDirt, 94) && g.slotCount(1) == 99);
  const Snap full = snap(g);
  CHECK(!g.addItem(pge::kGenStone, 1) && !g.addItem(pge::kGenDirt, 1) && same(full, snap(g)));
  // dig with a full inventory is refused for a tile that needs a new slot (grass) or a full stack (dirt)
  CHECK(!g.dig(col, surf) && w.get(col, surf, fg) == pge::kGenGrass && msgIs(g, "INV FULL"));
  CHECK(!g.dig(col, surf + 1) && w.get(col, surf + 1, fg) == pge::kGenDirt);
  CHECK(same(full, snap(g)) && w.changeCount() == 0);
  // place decrements; an emptied slot is cleared
  CHECK(g.takeItem(pge::kGenStone, 97) && g.slotCount(2) == 2);
  g.selectSlot(2);
  CHECK(g.held() == pge::kGenStone);
  CHECK(g.place(col, surf - 2) && w.get(col, surf - 2, fg) == pge::kGenStone);
  CHECK(g.slotCount(2) == 1 && g.slotItem(2) == pge::kGenStone);
  CHECK(g.place(col, surf - 3) && g.slotCount(2) == 0 && g.slotItem(2) == 0 && g.held() == 0);
  // place with count 0 is refused
  CHECK(!g.place(col + 1, surf - 2) && w.get(col + 1, surf - 2, fg) == pge::kTileAir);
  // dig selects the slot the tile went into (slot 2 is the only free one)
  g.selectSlot(0);
  CHECK(g.dig(col, surf) && w.get(col, surf, fg) == pge::kTileAir);
  CHECK(g.selected() == 2 && g.slotItem(2) == pge::kGenGrass && g.slotCount(2) == 1 && g.held() == pge::kGenGrass);
  return 0;
}

static int testCraft() {
  CHECK(standUp(g_ai) == 0);
  SandboxGame& g = g_ai;
  pge::World& w = g.world();
  const pge::Body& pb = *g.entities().body(g.player());
  const int col = pb.x / 16, surf = (pb.y + pb.h) / 16;
  const pge::Layer fg = pge::Layer::Fg;
  const uint8_t dirt = pge::kGenDirt, stone = pge::kGenStone, ore = pge::kGenOre;
  // bad index, missing items
  CHECK(g.craft(-1) == SandboxGame::CraftBad && g.craft(3) == SandboxGame::CraftBad);
  Snap s0 = snap(g);
  CHECK(g.craft(0) == SandboxGame::CraftNeedItems && same(s0, snap(g)) && msgIs(g, "NEED ITEMS"));
  // BRICK: 2 STONE + 1 DIRT -> 2 BRICK, exactly
  CHECK(g.addItem(stone, 5));
  CHECK(g.craft(0) == SandboxGame::CraftOk && msgIs(g, "CRAFTED"));
  CHECK(g.itemCount(stone) == 3 && g.itemCount(dirt) == 9 && g.itemCount(SandboxGame::kBrick) == 2);
  CHECK(g.slotItem(0) == dirt && g.slotCount(0) == 9 && g.slotItem(1) == stone && g.slotCount(1) == 3);
  CHECK(g.slotItem(2) == SandboxGame::kBrick && g.slotCount(2) == 2 && g.slotItem(3) == 0);
  // FURNACE: 8 STONE -> 1 FURNACE
  CHECK(g.addItem(stone, 5) && g.craft(1) == SandboxGame::CraftOk);
  CHECK(g.itemCount(stone) == 0 && g.itemCount(SandboxGame::kFurnace) == 1);
  // METAL: 3 ORE near a furnace tile. A furnace in the inventory is not a station.
  CHECK(g.addItem(ore, 3));
  s0 = snap(g);
  CHECK(g.craft(2) == SandboxGame::CraftNeedStation && same(s0, snap(g)) && msgIs(g, "NEED FURNACE"));
  CHECK(w.set(col + 2, surf - 1, fg, SandboxGame::kFurnace) == pge::Ok);
  CHECK(g.craft(2) == SandboxGame::CraftOk);
  CHECK(g.itemCount(ore) == 0 && g.itemCount(SandboxGame::kMetal) == 1);
  // a furnace inside the 11x11 box but out of reach (5 tiles = 80 px) does not count
  CHECK(w.set(col + 2, surf - 1, fg, pge::kTileAir) == pge::Ok);
  CHECK(w.set(col + 5, surf - 1, fg, SandboxGame::kFurnace) == pge::Ok);
  CHECK(g.addItem(ore, 3));
  s0 = snap(g);
  CHECK(g.craft(2) == SandboxGame::CraftNeedStation && same(s0, snap(g)));
  // no room: slots hold ids 1..8 at 99; BRICK needs a free place for its output and the inventory must be restored
  for (int id = 1; id <= 8; id++) CHECK(g.takeItem((uint8_t)id, g.itemCount((uint8_t)id)));
  for (int id = 1; id <= 8; id++) CHECK(g.addItem((uint8_t)id, 99) && g.slotItem(id - 1) == id);
  s0 = snap(g);
  CHECK(g.craft(0) == SandboxGame::CraftNoRoom && same(s0, snap(g)) && msgIs(g, "NO ROOM"));
  return 0;
}

static int testMenu() {
  CHECK(standUp(g_ai) == 0);
  SandboxGame& g = g_ai;
  const pge::Body& pb = *g.entities().body(g.player());
  Pad pad;
  g.teleportTo(pb.x, pb.y - 48);  // in the air: physics would move it
  const int x0 = pb.x, y0 = pb.y;
  const uint32_t t0 = g.ticks();
  CHECK(!g.menuOpen());
  pad.tick(g, PGE_BTN_SELECT);
  CHECK(g.menuOpen() && !g.overlay);
  pad.tick(g, 0);
  for (int i = 0; i < 10; i++) pad.tick(g, PGE_BTN_RIGHT);  // Right pressed once (slot 1), then held
  CHECK(pb.x == x0 && pb.y == y0 && g.menuOpen());
  CHECK(g.ticks() == t0 + 12);  // time still runs for the message timers / quit_after
  CHECK(g.selected() == 1);
  // Left / Right cycle the selected slot with wrap
  pad.tick(g, 0);
  g.selectSlot(0);
  pad.tick(g, PGE_BTN_LEFT);
  CHECK(g.selected() == 7);
  pad.tick(g, 0);
  pad.tick(g, PGE_BTN_RIGHT);
  CHECK(g.selected() == 0);
  pad.tick(g, 0);
  // A activates item 0 (BRICK): not enough stone first, then crafted
  pad.tick(g, PGE_BTN_A);
  CHECK(msgIs(g, "NEED ITEMS") && g.itemCount(SandboxGame::kBrick) == 0);
  pad.tick(g, 0);
  CHECK(g.addItem(pge::kGenStone, 10));
  pad.tick(g, PGE_BTN_A);
  CHECK(g.itemCount(SandboxGame::kBrick) == 2 && g.itemCount(pge::kGenStone) == 8 && msgIs(g, "CRAFTED"));
  pad.tick(g, 0);
  // Down + Start (also activates) = FURNACE
  pad.tick(g, PGE_BTN_DOWN);
  pad.tick(g, 0);
  pad.tick(g, PGE_BTN_START);
  CHECK(g.itemCount(SandboxGame::kFurnace) == 1 && g.itemCount(pge::kGenStone) == 0);
  pad.tick(g, 0);
  // Up twice from index 1 wraps to the last item (OVERLAY): A toggles the debug overlay, twice = off again
  pad.tick(g, PGE_BTN_UP);
  pad.tick(g, 0);
  pad.tick(g, PGE_BTN_UP);
  pad.tick(g, 0);
  pad.tick(g, PGE_BTN_A);
  CHECK(g.overlay);
  pad.tick(g, 0);
  pad.tick(g, PGE_BTN_A);
  CHECK(!g.overlay);
  pad.tick(g, 0);
  // B closes without digging; the world did not tick while the menu was open or on the closing tick
  CHECK(g.world().changeCount() == 0);
  pad.tick(g, PGE_BTN_B);
  CHECK(!g.menuOpen() && g.world().changeCount() == 0 && pb.y == y0);
  pad.tick(g, 0);
  tick(g, 3);
  CHECK(pb.y > y0);  // gravity is back
  // Select opens, Select closes; Select no longer toggles the overlay
  pad.tick(g, PGE_BTN_SELECT);
  CHECK(g.menuOpen());
  pad.tick(g, 0);
  pad.tick(g, PGE_BTN_SELECT);
  CHECK(!g.menuOpen() && !g.overlay);
  return 0;
}

static bool streq(const char* a, const char* b) { return std::strcmp(a, b) == 0; }

// The whole file is exactly `want`.
static bool fileIs(const char* path, const char* want) {
  char buf[32] = {};
  std::FILE* f = std::fopen(path, "rb");
  if (!f) return false;
  const size_t n = std::fread(buf, 1, sizeof buf - 1, f);
  std::fclose(f);
  return n == std::strlen(want) && std::memcmp(buf, want, n) == 0;
}

static void putFile(const char* path, const char* text) {
  std::FILE* f = std::fopen(path, "wb");
  if (f) {
    std::fputs(text, f);
    std::fclose(f);
  }
}

// Opens the menu and moves to the LANGUAGE item: Up from item 0 wraps to OVERLAY (the last), then LANGUAGE.
static void toLanguageItem(Pad& pad, SandboxGame& g) {
  pad.tick(g, PGE_BTN_SELECT);
  pad.tick(g, 0);
  for (int i = 0; i < 2; i++) {
    pad.tick(g, PGE_BTN_UP);
    pad.tick(g, 0);
  }
}

// The LANGUAGE menu item cycles EN -> RU -> DE -> EN; menu rows and messages follow, the choice is stored in the
// settings file and read again at the next start; every text fits the space it is drawn in.
static int testLanguage() {
  const char* const ru_save = "\xD0\xA1\xD0\x9E\xD0\xA5\xD0\xA0\xD0\x90\xD0\x9D\xD0\x98\xD0\xA2\xD0\xAC";  // RU: SAVE
  const char* const ru_need = "\xD0\x9D\xD0\x95 \xD0\xA5\xD0\x92\xD0\x90\xD0\xA2\xD0\x90\xD0\x95\xD0\xA2";  // RU: NEED
  const char* const en[7] = {"BRICK: 2 STONE 1 DIRT", "FURNACE: 8 STONE", "METAL: 3 ORE (NEAR FURNACE)", "SAVE", "LOAD",
                             "LANGUAGE: ENGLISH", "OVERLAY"};
  std::remove(kLangFile);
  CHECK(standUp(g_ai) == 0);
  SandboxGame& g = g_ai;
  CHECK(g.lang() == 0 && g.text().nlang == 3);
  for (int i = 0; i < 7; i++) CHECK(streq(g.menuItem(i), en[i]));
  Pad pad;
  toLanguageItem(pad, g);
  CHECK(g.menuOpen());
  pad.tick(g, PGE_BTN_A);  // EN -> RU
  CHECK(g.lang() == 1 && !g.overlay && streq(g.menuItem(3), ru_save));
  CHECK(fileIs(kLangFile, "lang = ru\n"));
  CHECK(g.craft(0) == SandboxGame::CraftNeedItems);
  CHECK(streq(g.message(), ru_need));
  CHECK(renderOk(g, g_buf));  // the menu and the message draw in Russian
  pad.tick(g, 0);
  pad.tick(g, PGE_BTN_A);  // RU -> DE: the message that is still shown follows the language
  CHECK(g.lang() == 2 && streq(g.menuItem(3), "SPEICHERN") && streq(g.message(), "ZU WENIG MATERIAL"));
  CHECK(fileIs(kLangFile, "lang = de\n"));
  CHECK(renderOk(g, g_buf));
  // the stored language is read at the next start
  CHECK(create(g) == pge::Ok && g.lang() == 2 && streq(g.menuItem(5), "SPRACHE: DEUTSCH"));
  Pad pad2;
  toLanguageItem(pad2, g);
  pad2.tick(g, PGE_BTN_A);  // DE -> EN: the English texts are back, byte for byte
  CHECK(g.lang() == 0 && fileIs(kLangFile, "lang = en\n"));
  for (int i = 0; i < 7; i++) CHECK(streq(g.menuItem(i), en[i]));
  CHECK(g.craft(0) == SandboxGame::CraftNeedItems && msgIs(g, "NEED ITEMS"));
  // an unknown language in the file keeps English; the file is read only at start
  putFile(kLangFile, "lang = xx\n");
  CHECK(create(g) == pge::Ok && g.lang() == 0 && streq(g.menuItem(5), "LANGUAGE: ENGLISH"));
  putFile(kLangFile, "lang = ru\n");
  CHECK(create(g) == pge::Ok && g.lang() == 1);
  std::remove(kLangFile);
  CHECK(create(g) == pge::Ok && g.lang() == 0);  // the other tests run in English

  // every text fits: menu rows into the 200 px panel (1 px border), the help line and the messages into the screen
  const pge::StringTable& t = g.text();
  for (uint8_t l = 0; l < t.nlang; l++) {
    for (uint16_t id = 0; id < TXT_COUNT; id++) {
      const char* s = t.get(l, id);
      CHECK(s[0] != 0);
      CHECK(pge::textWidth(pge::kFont5x7, s) <= (id <= TXT_MENU_OVERLAY ? 200 - 2 : PGE_LOGICAL_W - 4));
    }
  }
  return 0;
}

static const char* kSave = "test_sandbox_tmp.sav";
static const char* kSave2 = "test_sandbox_tmp2.sav";
static const char* kSave3 = "test_sandbox_tmp3.sav";
static const char* kMissing = "test_sandbox_missing.sav";

static void rmSave(const char* p) {
  char b[96];
  std::remove(p);
  std::snprintf(b, sizeof b, "%s.tmp", p);
  std::remove(b);
  std::snprintf(b, sizeof b, "%s.bak", p);
  std::remove(b);
}
static void rmAll() {
  rmSave(kSave);
  rmSave(kSave2);
  rmSave(kSave3);
  rmSave(kMissing);
}

static int testSaveLoad() {
  rmAll();
  g_a.save_path = kSave;
  CHECK(create(g_a) == pge::Ok);
  tick(g_a, 30);
  const int col = SandboxGame::kSpawnCol, surf = g_a.surfaceY(col) / 16;
  CHECK(g_a.dig(col, surf + 1) && g_a.dig(col, surf) && g_a.place(col, surf - 2));  // 3 edits at the spawn
  CHECK(g_a.addItem(pge::kGenStone, 5) && g_a.craft(0) == SandboxGame::CraftOk);
  const int far = 100;
  g_a.teleportTo(far * 16 + 2, g_a.surfaceY(far) - 14);
  tick(g_a, 30);
  CHECK(onGround(g_a));
  const pge::Body& pb = *g_a.entities().body(g_a.player());
  const int fc = pb.x / 16, fs = (pb.y + pb.h) / 16;
  CHECK(g_a.dig(fc, fs));  // a 4th edit where the player ends up
  g_a.selectSlot(2);
  g_a.entities().health(g_a.player())->hp = 7;
  CHECK(g_a.saveToStorage() && msgIs(g_a, "SAVED"));
  const Snap sa = snap(g_a);
  CHECK(sa.changes == 4 && sa.hp == 7 && sa.sel == 2 && sa.ticks == 60 && !isDefaultInv(sa));
  uint32_t keys[8], tiles[8];
  for (uint32_t i = 0; i < sa.changes; i++) {
    keys[i] = g_a.world().changeKeyAt(i);
    tiles[i] = g_a.world().changeTileAt(i);
  }

  // a fresh game loads it: inventory, selection, hp, position, ticks and the whole change log are equal
  CHECK(create(g_b) == pge::Ok);
  g_b.save_path = kSave;
  CHECK(!same(snap(g_b), sa) && isDefaultInv(snap(g_b)));
  CHECK(g_b.loadFromStorage() && msgIs(g_b, "LOADED"));
  CHECK(same(snap(g_b), sa));
  for (uint32_t i = 0; i < sa.changes; i++)
    CHECK(g_b.world().changeKeyAt(i) == keys[i] && g_b.world().changeTileAt(i) == tiles[i]);
  CHECK(g_b.world().get(fc, fs, pge::Layer::Fg) == pge::kTileAir);  // loaded around the new position
  g_b.teleportTo(col * 16 + 2, g_b.surfaceY(col) - 14);
  CHECK(g_b.world().get(col, surf, pge::Layer::Fg) == pge::kTileAir &&
        g_b.world().get(col, surf - 2, pge::Layer::Fg) == pge::kGenGrass);

  // load_on_start does the same from onCreate
  g_a.load_on_start = true;
  CHECK(create(g_a) == pge::Ok);
  g_a.load_on_start = false;
  CHECK(same(snap(g_a), sa));
  return 0;
}

// Copies kSave to kSave2 with the last byte flipped (a CRC failure).
static int makeFlipped() {
  static uint8_t buf[4096];
  FILE* f = std::fopen(kSave, "rb");
  CHECK(f);
  const size_t n = std::fread(buf, 1, sizeof buf, f);
  std::fclose(f);
  CHECK(n > 32 && n < sizeof buf);
  buf[n - 1] ^= 0xFF;
  f = std::fopen(kSave2, "wb");
  CHECK(f);
  CHECK(std::fwrite(buf, 1, n, f) == n);
  std::fclose(f);
  return 0;
}

// Layout of the game blob (see SandboxGame::saveToStorage).
static void putLE(uint8_t* o, int v) {
  for (int i = 0; i < 4; i++) o[i] = (uint8_t)((uint32_t)v >> (8 * i));
}
static void goodBlob(uint8_t* b, int px, int py) {
  std::memset(b, 0, SandboxGame::kBlob);
  b[0] = 1;
  putLE(b + 1, px);
  putLE(b + 5, py);
  b[9] = 5;
  b[10] = 1;
  b[11] = pge::kGenStone;
  b[12] = 10;
}

// g_b: fresh game with an edit, a non-default inventory, hp 3, moved far away and 3 ticks old.
static int dirtyB(const char* path) {
  CHECK(create(g_b) == pge::Ok);
  g_b.save_path = path;
  const int col = SandboxGame::kSpawnCol, surf = g_b.surfaceY(col) / 16;
  CHECK(g_b.world().set(col, surf + 3, pge::Layer::Fg, pge::kTileAir) == pge::Ok);  // solid underground normally
  CHECK(g_b.addItem(pge::kGenOre, 5));
  g_b.selectSlot(4);
  g_b.entities().health(g_b.player())->hp = 3;
  g_b.teleportTo(100 * 16 + 2, g_b.surfaceY(100) - 14);
  tick(g_b, 3);
  return 0;
}

static int testLoadFailures() {
  const int col = SandboxGame::kSpawnCol, sx = col * 16 + 2;
  // missing file: not an error for the world or the player
  CHECK(dirtyB(kMissing) == 0);
  Snap s0 = snap(g_b);
  CHECK(!g_b.loadFromStorage() && msgIs(g_b, "NO SAVE") && same(s0, snap(g_b)));
  // one flipped byte: the CRC fails before anything is consumed, so the world is untouched
  CHECK(makeFlipped() == 0 && dirtyB(kSave2) == 0);
  s0 = snap(g_b);
  CHECK(s0.changes == 1);
  CHECK(!g_b.loadFromStorage() && msgIs(g_b, "LOAD FAILED") && same(s0, snap(g_b)));
  // a save of another world (seed): InvalidArg, world and player untouched
  g_b.seed = 999;
  CHECK(dirtyB(kSave) == 0);
  g_b.seed = 1234;
  s0 = snap(g_b);
  CHECK(s0.changes == 1);
  CHECK(!g_b.loadFromStorage() && msgIs(g_b, "LOAD FAILED") && same(s0, snap(g_b)));

  // valid files whose blob is not: written directly with saveGame
  static uint8_t iobuf[512];
  pge::Storage st;
  st.setBuffer(iobuf, sizeof iobuf);
  CHECK(st.mount() == pge::Ok);
  typedef void (*Mut)(uint8_t*);
  struct Case { const char* name; Mut mut; uint32_t len; bool good; };
  const Case cases[] = {
      {"control", [](uint8_t*) {}, SandboxGame::kBlob, true},
      {"item id 99", [](uint8_t* b) { b[11 + 2 * 3] = 99; b[12 + 2 * 3] = 5; }, SandboxGame::kBlob, false},
      {"count 100", [](uint8_t* b) { b[12] = 100; }, SandboxGame::kBlob, false},
      {"item without count", [](uint8_t* b) { b[11 + 2 * 3] = 2; }, SandboxGame::kBlob, false},
      {"count without item", [](uint8_t* b) { b[12 + 2 * 3] = 2; }, SandboxGame::kBlob, false},
      {"hp 0", [](uint8_t* b) { b[9] = 0; }, SandboxGame::kBlob, false},
      {"hp over max", [](uint8_t* b) { b[9] = 11; }, SandboxGame::kBlob, false},
      {"version 2", [](uint8_t* b) { b[0] = 2; }, SandboxGame::kBlob, false},
      {"selection 8", [](uint8_t* b) { b[10] = 8; }, SandboxGame::kBlob, false},
      {"x outside", [](uint8_t* b) { putLE(b + 1, 32 * 32 * 16 - 11); }, SandboxGame::kBlob, false},
      {"y negative", [](uint8_t* b) { putLE(b + 5, -1); }, SandboxGame::kBlob, false},
      {"too short", [](uint8_t*) {}, SandboxGame::kBlob - 1, false},
  };
  for (const Case& cs : cases) {
    rmSave(kSave3);
    CHECK(dirtyB(kSave3) == 0);
    const int sy = g_b.surfaceY(col) - 14;
    uint8_t blob[SandboxGame::kBlob];
    goodBlob(blob, sx, sy);
    cs.mut(blob);
    CHECK(pge::saveGame(st, kSave3, g_b.world(), 77, blob, cs.len) == pge::Ok);
    const uint32_t ticks_before = g_b.ticks();
    const bool ok = g_b.loadFromStorage();
    const Snap s = snap(g_b);
    if (cs.good) {
      CHECK(ok && msgIs(g_b, "LOADED"));
      CHECK(s.item[0] == pge::kGenStone && s.count[0] == 10 && s.item[1] == 0 && s.sel == 1 && s.hp == 5);
      CHECK(s.px == sx && s.py == sy && s.ticks == 77);
    } else {
      if (ok || !msgIs(g_b, "LOAD FAILED") || !isDefaultInv(s) || s.hp != 10 || s.px != sx || s.py != sy ||
          s.changes != 0 || s.ticks != ticks_before ||
          g_b.world().get(col, g_b.surfaceY(col) / 16 + 3, pge::Layer::Fg) == pge::kTileAir) {
        std::printf("bad blob case '%s' not rejected / reset\n", cs.name);
        return 1;
      }
    }
  }
  // a blob larger than the game's buffer: the engine fails the load after touching the world and clears the log;
  // the game keeps its state
  {
    rmSave(kSave3);
    CHECK(dirtyB(kSave3) == 0);
    uint8_t blob[SandboxGame::kBlob + 1];
    goodBlob(blob, sx, g_b.surfaceY(col) - 14);
    blob[SandboxGame::kBlob] = 0;
    CHECK(pge::saveGame(st, kSave3, g_b.world(), 77, blob, sizeof blob) == pge::Ok);
    const Snap before = snap(g_b);
    CHECK(!g_b.loadFromStorage() && msgIs(g_b, "LOAD FAILED"));
    const Snap after = snap(g_b);
    CHECK(sameInv(before, after) && before.sel == after.sel && before.hp == after.hp && before.px == after.px);
    CHECK(after.changes == 0 && after.ticks == before.ticks);
  }
  rmAll();
  return 0;
}

static int testRenderMenu() {
  CHECK(standUp(g_ai) == 0);
  CHECK(g_ai.takeItem(pge::kGenDirt, 10));
  for (int id = 1; id <= 8; id++) CHECK(g_ai.addItem((uint8_t)id, 99));  // full hotbar
  CHECK(renderOk(g_ai, g_buf));
  Pad pad;
  pad.tick(g_ai, PGE_BTN_SELECT);
  CHECK(g_ai.menuOpen());
  CHECK(renderOk(g_ai, g_buf2));
  int diff = 0;
  for (uint32_t i = 0; i < PGE_LOGICAL_W * PGE_LOGICAL_H; i++) diff += g_buf[i] != g_buf2[i];
  CHECK(diff > 1000);  // the menu panel (200 x 63) plus its help line
  return 0;
}

// The scripted bench bot: 3000 ticks on a 6-slot chunk cache (so chunks are evicted and reloaded) without a crash,
// samples taken after the warm-up, the player moved, stayed inside the world, and the bot's dig / place edited the log.
static int testAutoplay() {
  g_bot.autoplay = true;
  g_bot.chunk_cache = 6;
  CHECK(create(g_bot) == pge::Ok);
  const pge::Body& pb = *g_bot.entities().body(g_bot.player());
  const int x0 = pb.x, ww = g_bot.world().chunksW() * pge::kChunkSize * 16, wh = g_bot.world().chunksH() * pge::kChunkSize * 16;
  tick(g_bot, 3000);
  CHECK(g_bot.bench().n == 3000 - 60);
  CHECK(pb.x >= 0 && pb.x + pb.w <= ww && pb.y >= 0 && pb.y + pb.h <= wh);
  CHECK(pb.x != x0);
  CHECK(g_bot.world().changeCount() > 0);
  return 0;
}

int main() {
  CHECK(pge::init() == pge::Ok);
  std::remove(kLangFile);
  if (testMain()) return 1;
  if (testRender()) return 1;
  if (testPlaceOverlap()) return 1;
  if (testAim()) return 1;
  if (testDigUp()) return 1;
  if (testPersistence()) return 1;
  if (testSlimes()) return 1;
  if (testInventory()) return 1;
  if (testCraft()) return 1;
  if (testMenu()) return 1;
  if (testLanguage()) { std::remove(kLangFile); return 1; }
  if (testSaveLoad()) { rmAll(); return 1; }
  if (testLoadFailures()) { rmAll(); return 1; }
  if (testRenderMenu()) return 1;
  if (testAutoplay()) return 1;
  std::remove(kLangFile);
  std::printf("test_sandbox: ok\n");
  return 0;
}
