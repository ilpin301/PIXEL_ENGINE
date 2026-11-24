// Sandbox example (S6.1 + S6.2a): generated tile world, a controllable character, digging and placing tiles within a
// reach limit, slimes (game-side AI on engine entity pools and health), an 8-slot inventory with a hotbar HUD, a craft
// menu and save / load through the engine's storage layer. Only public "pge/..." headers are used (checked by
// check_includes.cmake). The object must stay in place after onCreate (tilesets and the world point into it), so keep
// it static. Edits live in the world's change log, so they survive chunk eviction and reload.
// Menu and HUD message texts come from a text table (game_text.csv -> game_text.h by tools/pge_assets.py); the menu
// item LANGUAGE cycles English / Russian / German and stores the choice in settings.txt.
#pragma once
#include <stdint.h>
#include <string.h>
#include "pge/asset.h"
#include "pge/camera.h"
#include "pge/collision.h"
#include "pge/color.h"
#include "pge/debug.h"
#include "pge/engine.h"
#include "pge/entity.h"
#include "pge/save.h"
#include "pge/settings.h"
#include "pge/sprite.h"
#include "pge/storage.h"
#include "pge/ui.h"
#include "pge/world.h"
#include "pge/worldgen.h"
#include "game_text.h"

class SandboxGame {
 public:
  struct Bench {  // sums of Engine::stats() over the autoplay ticks after the 60-tick warm-up
    uint32_t n, sum_tick_us, sum_band_us, sum_flush_us, sum_frame_us, max_tick_us, max_band_us, max_frame_us;
  };
  static constexpr int kTile = 16, kMaxSlimes = 6, kSpawnCol = 512;
  static constexpr int kSlots = 8, kStack = 99, kBlob = 27, kRecipes = 3, kMenuItems = 7;
  enum : uint8_t { kBrick = 6, kFurnace = 7, kMetal = 8 };  // tile ids == item ids (1..5 are the generator's)
  enum CraftResult { CraftOk, CraftNeedItems, CraftNeedStation, CraftNoRoom, CraftBad };

  uint32_t quit_after = 0;    // request quit after this many ticks; 0 = run until the platform quits
  bool autoplay = false;      // ignore the Input and run the scripted bot (benchmarks and tests)
  bool overlay = false;       // debug overlay; toggled by a menu item (state changes in onTick only)
  uint32_t seed = 1234;       // world seed (set before onCreate)
  uint16_t chunk_cache = 0;   // chunk cache slots, 0 = kDefaultCache (the profile default of 64 does not fit the arena)
  uint32_t change_cap = 2048; // change log entries (5 B each)
  const char* save_path = "sandbox.sav";
  const char* settings_path = "settings.txt";  // "lang = xx"; read in onCreate, written by the LANGUAGE menu item
  bool load_on_start = false; // onCreate ends with loadFromStorage()

  pge::Error onCreate(pge::Engine& e) {
    using namespace pge;
    eng_ = &e;
    props_[kGenGrass] = TileProps{kTileSolid | kTileBreakable, 1, 0};
    props_[kGenDirt] = TileProps{kTileSolid | kTileBreakable, 2, 0};
    props_[kGenStone] = TileProps{kTileSolid | kTileBreakable, 4, 0};
    props_[kGenOre] = TileProps{kTileSolid | kTileBreakable, 6, 0};
    props_[kGenDeepOre] = TileProps{kTileSolid | kTileBreakable, 8, 0};
    for (int t = kBrick; t <= kMetal; t++) props_[t] = TileProps{kTileSolid | kTileBreakable, 3, 0};
    props_[kTileVoid] = TileProps{kTileSolid, 255, 0};
    makeArt();
    WorldConfig wc = {};
    wc.w_chunks = 32;
    wc.h_chunks = 4;
    wc.seed = seed;
    wc.gen = terrainGen;
    wc.props = props_;
    wc.cache_slots = chunk_cache ? chunk_cache : kDefaultCache;
    wc.change_capacity = change_cap;
    wc.tile_px = kTile;
    Error err = world_.init(wc, e.initArena());
    if (err != Ok) return err;
    err = ent_.init(e.initArena(), 32);
    if (err != Ok) return err;
    e.trackPool(0, &ent_.counters());  // entity pool high-water in Stats::pool_high[0] and the overlay
    err = storage_.init(e.initArena(), 512);  // the LARGE default io buffer (16 KiB) does not fit the init arena
    if (err != Ok) return err;
    storage_ok_ = storage_.mount() == Ok;
    err = assetStrings(kGameText, kGameText_size, text_);
    if (err != Ok) return err;
    lang_ = 0;
    if (storage_ok_) {
      Settings settings;
      if (settings.init(e.initArena(), 4) != Ok) return OutOfMemory;
      char buf[64];
      if (settings.load(storage_, settings_path, buf, sizeof buf) == Ok) {
        const int li = text_.langIndex(settings.getStr("lang", "en"));
        if (li >= 0) lang_ = static_cast<uint8_t>(li);
      }
    }
    applyLang();
    const int sx = kSpawnCol * kTile + 2, sy = surfaceY(kSpawnCol) - 14;
    load(Rect{sx - 448, 0, 896, 1024}, 16);  // the spawn area: 2 x 2 chunks, so slimes find their ground
    player_ = ent_.spawn(kKindPlayer, sx, sy, 12, 14, kEntGravity | kEntCollide);
    if (!isValid(player_) || !ent_.addHealth(player_, 10)) return OutOfMemory;
    for (int i = 0; i < kMaxSlimes; i++) {
      slimes_[i] = Slime{kNoEntity, i % 2 ? int8_t(-1) : int8_t(1), 0, kSpawnCol + kSlimeOff[i]};
      spawnSlime(slimes_[i]);
    }
    resetInventory();
    ticks_ = turns_ = deaths_ = 0;
    bench_ = Bench{};
    bot_in_ = Input{};
    bot_dir_ = 1;
    stuck_ = 0;
    last_x_ = -1;
    msg_ = MsgNone;
    msg_t_ = 0;
    menu_ = false;
    nav_.selected = 0;
    face_ = 1;
    cam_.centerOn(sx + 6, sy + 7);
    aim(Input{}, *ent_.body(player_));
    buildView();
    format();
    if (load_on_start) loadFromStorage();
    return Ok;
  }

  void onTick(const pge::Input& real) {
    using namespace pge;
    const Input& in = autoplay ? botInput() : real;
    const bool was_menu = menu_;  // a menu that was open at tick start swallows the whole tick (B must not also dig)
    if (in.pressed(Button::Select)) {
      menu_ = !menu_;
      nav_.selected = 0;
    } else if (menu_) {
      menuInput(in);
    }
    if (!was_menu && !menu_) worldTick(in);
    if (msg_t_ > 0) msg_t_--;
    ticks_++;
    if (autoplay && eng_ && ticks_ > 60) sample();
    format();
    if (quit_after != 0 && ticks_ >= quit_after && eng_) eng_->requestQuit();
  }

  // Runs once per band: everything drawn is a pure function of the state set in onCreate / onTick.
  void onRender(pge::Canvas& c) {
    using namespace pge;
    c.clear(rgb(110, 170, 250));
    c.tilemap(TileLayer{vis_wall_, kVisW, kVisH, &wall_set_}, cam_.x & 15, cam_.y & 15);
    c.tilemap(TileLayer{vis_fg_, kVisW, kVisH, &fg_set_}, cam_.x & 15, cam_.y & 15);
    ent_.forEach([&](EntityId id, Body& b) {
      if (ent_.meta(id)->kind == kKindSlime) {
        c.sprite(slime_sheet_, (ticks_ >> 4) & 1, b.x - cam_.x, b.y - cam_.y, None);
      } else if (!blink_) {
        c.sprite(player_sheet_, walk_ ? (ticks_ >> 3) & 1 : 0, b.x - cam_.x, b.y - cam_.y, face_ < 0 ? FlipX : None);
      }
    });
    c.rect(Rect{tx_ * kTile - cam_.x, ty_ * kTile - cam_.y, kTile, kTile}, reach_ ? rgb(0, 255, 0) : rgb(255, 0, 0));
    c.text(kFont5x7, 3, 3, hud_, rgb(0, 0, 0));
    c.text(kFont5x7, 2, 2, hud_, rgb(255, 255, 255));
    const Health* h = ent_.health(player_);
    ui::bar(c, style_, Rect{2, 22, 60, 8}, h ? static_cast<uint16_t>(h->hp > 0 ? h->hp : 0) : 0, h ? h->max_hp : 0,
            rgb(220, 40, 40));
    ui::label(c, style_, 66, 22, "HP");
    if (msg_t_ > 0) ui::label(c, style_, 2, 32, message());
    uint16_t frames[kSlots];
    uint8_t counts[kSlots];
    for (int i = 0; i < kSlots; i++) {
      const bool full = inv_[i].count > 0 && inv_[i].item < kFgTiles;
      frames[i] = full ? inv_[i].item : 0xFFFF;
      counts[i] = inv_[i].count;
    }
    ui::iconGrid(c, style_, Rect{2, PGE_LOGICAL_H - 20, kSlots * 18, 18}, fg_icons_, frames, counts, kSlots, 1, sel_);
    if (menu_) {
      ui::listMenu(c, style_, Rect{60, 50, 200, 63}, menu_text_, kMenuItems, nav_.selected);
      ui::label(c, style_, 60, 117, text_.get(lang_, TXT_MENU_HELP));
    }
    if (overlay && eng_) drawOverlay(c, style_, eng_->stats(), kOverlayAll);
  }

  // ---- inventory ----
  int itemCount(uint8_t item) const {
    int n = 0;
    for (const Slot& s : inv_)
      if (s.count > 0 && s.item == item) n += s.count;
    return n;
  }
  // Atomic: fills the item's existing stacks first, then empty slots; if not all n fit nothing changes.
  bool addItem(uint8_t item, int n) {
    if (item == 0 || n <= 0) return false;
    Slot bak[kSlots];
    memcpy(bak, inv_, sizeof inv_);
    while (n > 0) {
      const int i = findSlotFor(item);
      if (i < 0) {
        memcpy(inv_, bak, sizeof inv_);
        return false;
      }
      Slot& s = inv_[i];
      const int take = n < kStack - s.count ? n : kStack - s.count;
      s.item = item;
      s.count = static_cast<uint8_t>(s.count + take);
      n -= take;
    }
    return true;
  }
  // Atomic: all n or nothing.
  bool takeItem(uint8_t item, int n) {
    if (n < 0 || itemCount(item) < n) return false;
    for (Slot& s : inv_) {
      if (n == 0) break;
      if (s.count == 0 || s.item != item) continue;
      const int take = n < s.count ? n : s.count;
      s.count = static_cast<uint8_t>(s.count - take);
      if (s.count == 0) s.item = 0;
      n -= take;
    }
    return true;
  }
  uint8_t slotItem(int i) const { return inv_[i].item; }
  uint8_t slotCount(int i) const { return inv_[i].count; }
  int selected() const { return sel_; }
  int targetX() const { return tx_; }
  int targetY() const { return ty_; }
  void selectSlot(int i) { sel_ = static_cast<int8_t>(((i % kSlots) + kSlots) % kSlots); }
  uint8_t held() const { return inv_[sel_].count > 0 ? inv_[sel_].item : 0; }

  // Digs the fg tile if it is breakable and within reach; the dug tile goes into the inventory and its slot becomes
  // the selected one. False if refused, if the inventory has no room for it ("INV FULL"), or if the world change log
  // is full (then logFull() shows on the HUD).
  bool dig(int tx, int ty) {
    using namespace pge;
    if (!inReach(tx, ty)) return false;
    const uint8_t t = world_.get(tx, ty, Layer::Fg);
    if (!(world_.props(t).flags & kTileBreakable)) return false;
    const int slot = findSlotFor(t);
    if (slot < 0) {
      say(MsgInvFull);
      return false;
    }
    const Error err = world_.set(tx, ty, Layer::Fg, kTileAir);
    if (err == OutOfMemory) say(MsgLogFull);
    if (err != Ok) return false;
    addItem(t, 1);  // cannot fail: findSlotFor found a slot, and it picks the same one
    sel_ = static_cast<int8_t>(slot);
    return true;
  }

  // Places the held tile on an air tile within reach unless an entity (player or slime) overlaps the tile box.
  bool place(int tx, int ty) {
    using namespace pge;
    if (inv_[sel_].count == 0) return false;
    if (!inReach(tx, ty) || world_.get(tx, ty, Layer::Fg) != kTileAir) return false;
    const int x0 = tx * kTile, y0 = ty * kTile;
    bool blocked = false;
    ent_.forEach([&](EntityId, Body& b) {
      if (b.x < x0 + kTile && b.x + b.w > x0 && b.y < y0 + kTile && b.y + b.h > y0) blocked = true;
    });
    if (blocked) return false;
    const Error err = world_.set(tx, ty, Layer::Fg, inv_[sel_].item);
    if (err == OutOfMemory) say(MsgLogFull);
    if (err != Ok) return false;
    if (--inv_[sel_].count == 0) inv_[sel_].item = 0;
    return true;
  }

  // Order: bad index, station, items, then take inputs and add outputs; if the outputs do not fit the inventory is
  // restored exactly. Sets the HUD message.
  int craft(int recipe) {
    if (recipe < 0 || recipe >= kRecipes) return CraftBad;
    const Recipe& r = kRecipeTable[recipe];
    if (r.station && !nearTile(r.station)) {
      say(MsgNeedFurnace);
      return CraftNeedStation;
    }
    if (itemCount(r.in_a) < r.n_a || (r.n_b && itemCount(r.in_b) < r.n_b)) {
      say(MsgNeedItems);
      return CraftNeedItems;
    }
    Slot bak[kSlots];
    memcpy(bak, inv_, sizeof inv_);
    takeItem(r.in_a, r.n_a);
    if (r.n_b) takeItem(r.in_b, r.n_b);
    if (!addItem(r.out, r.n_out)) {
      memcpy(inv_, bak, sizeof inv_);
      say(MsgNoRoom);
      return CraftNoRoom;
    }
    say(MsgCrafted);
    return CraftOk;
  }

  // ---- save / load (explicit only: no autosave, flash wear) ----
  bool saveToStorage() {
    using namespace pge;
    if (!storage_ok_) return say(MsgNoStorage);
    uint8_t blob[kBlob];
    const Body& b = *ent_.body(player_);
    blob[0] = 1;
    wr32(blob + 1, b.x);
    wr32(blob + 5, b.y);
    blob[9] = static_cast<uint8_t>(ent_.health(player_)->hp);
    blob[10] = static_cast<uint8_t>(sel_);
    for (int i = 0; i < kSlots; i++) {
      blob[11 + 2 * i] = inv_[i].item;
      blob[12 + 2 * i] = inv_[i].count;
    }
    const bool ok = saveGame(storage_, save_path, world_, ticks_, blob, kBlob) == Ok;
    say(ok ? MsgSaved : MsgSaveFailed);
    return ok;
  }

  bool loadFromStorage() {
    using namespace pge;
    if (!storage_ok_) return say(MsgNoStorage);
    uint8_t blob[kBlob];
    uint32_t tick = 0, len = 0;
    const Error err = loadGame(storage_, save_path, world_, tick, blob, kBlob, len);
    if (err != Ok) {  // InvalidArg (other world): untouched; any other: the engine cleared the log again
      loadView(16);   // chunks may have been freed
      buildView();
      return say(err == Io ? MsgNoSave : MsgLoadFailed);
    }
    // The blob is a trust boundary (a valid CRC does not make the content sane).
    const Body& b = *ent_.body(player_);
    const Health* h = ent_.health(player_);
    const int ww = world_.chunksW() * kChunkSize * kTile, wh = world_.chunksH() * kChunkSize * kTile;
    const int px = rd32(blob + 1), py = rd32(blob + 5);
    bool ok = len == kBlob && blob[0] == 1 && blob[10] < kSlots && blob[9] >= 1 && blob[9] <= h->max_hp &&
              px >= 0 && px <= ww - b.w && py >= 0 && py <= wh - b.h;
    for (int i = 0; ok && i < kSlots; i++) {
      const uint8_t item = blob[11 + 2 * i], n = blob[12 + 2 * i];
      ok = item < kFgTiles && n <= kStack && (item == 0) == (n == 0);
    }
    if (!ok) {
      world_.clearChanges();
      resetInventory();
      ent_.health(player_)->hp = ent_.health(player_)->max_hp;
      teleportTo(kSpawnCol * kTile + 2, surfaceY(kSpawnCol) - 14);
      return say(MsgLoadFailed);
    }
    for (int i = 0; i < kSlots; i++) inv_[i] = Slot{blob[11 + 2 * i], blob[12 + 2 * i]};
    sel_ = static_cast<int8_t>(blob[10]);
    ent_.health(player_)->hp = blob[9];
    ticks_ = tick;
    teleportTo(px, py);  // re-runs ensureLoaded, required after loadGame
    say(MsgLoaded);
    return true;
  }

  // Target tile centre within 64 px of the player centre (squared comparison).
  bool inReach(int tx, int ty) {
    const pge::Body& b = *ent_.body(player_);
    const int dx = tx * kTile + kTile / 2 - (b.x + b.w / 2), dy = ty * kTile + kTile / 2 - (b.y + b.h / 2);
    return dx * dx + dy * dy <= 64 * 64;
  }

  // Moves the player (body top-left, world px), re-aims the camera and loads the chunks around the new place.
  void teleportTo(int px, int py) {
    pge::Body& b = *ent_.body(player_);
    b.x = px;
    b.y = py;
    *ent_.motion(player_) = pge::Motion{0, 0, 0, 0};
    cam_.centerOn(px + b.w / 2, py + b.h / 2);
    loadView(16);
    buildView();
  }

  const Bench& bench() const { return bench_; }
  pge::World& world() { return world_; }
  pge::Entities& entities() { return ent_; }
  pge::EntityId player() const { return player_; }
  pge::EntityId slime(int i) const { return slimes_[i].id; }
  uint32_t turns() const { return turns_; }     // direction changes made by slime AI at a wall or ledge
  uint32_t deaths() const { return deaths_; }
  uint32_t ticks() const { return ticks_; }
  bool logFull() const { return msg_t_ > 0 && msg_ == MsgLogFull; }
  bool menuOpen() const { return menu_; }
  const char* message() const {  // HUD message in the current language, "" when expired
    static_assert(TXT_MSG_NO_STORAGE - TXT_MSG_LOG_FULL == MsgNoStorage - MsgLogFull, "MSG_ rows follow the Msg enum");
    if (msg_t_ == 0 || msg_ == MsgNone) return "";
    return text_.get(lang_, static_cast<uint16_t>(TXT_MSG_LOG_FULL + msg_ - MsgLogFull));
  }
  uint8_t lang() const { return lang_; }  // index into the text table (0 = English)
  const pge::StringTable& text() const { return text_; }
  const char* menuItem(int i) const { return menu_text_[i]; }  // menu row text in the current language
  int cameraX() const { return cam_.x; }
  int surfaceY(int col) const { return pge::terrainHeight(col, seed) * kTile; }  // top pixel row of column col

 private:
  enum { kKindPlayer = 0, kKindSlime = 1 };
  enum Msg : uint8_t { MsgNone, MsgLogFull, MsgInvFull, MsgCrafted, MsgNeedItems, MsgNeedFurnace, MsgNoRoom, MsgSaved,
                       MsgSaveFailed, MsgNoSave, MsgLoadFailed, MsgLoaded, MsgNoStorage };
  struct Slot { uint8_t item, count; };
  struct Recipe { uint8_t in_a, n_a, in_b, n_b, out, n_out, station; const char* name; };
  static constexpr Recipe kRecipeTable[kRecipes] = {
      {pge::kGenStone, 2, pge::kGenDirt, 1, kBrick, 2, 0, "BRICK"},
      {pge::kGenStone, 8, 0, 0, kFurnace, 1, 0, "FURNACE"},
      {pge::kGenOre, 3, 0, 0, kMetal, 1, kFurnace, "METAL"}};
  static constexpr int kDefaultCache = 12, kMargin = 64;
  static constexpr int kVisW = PGE_LOGICAL_W / kTile + 2, kVisH = PGE_LOGICAL_H / kTile + 2;
  static constexpr int kFgTiles = 9, kWallTiles = 3;
  // 8.8 fixed point: 256 = 1 px (per tick for velocities)
  static constexpr int kGrav = 64, kMaxFall = 6 * 256, kJump = 5 * 256, kMaxRun = 2 * 256, kAccel = 64;
  static constexpr int kPatrol = 96, kChase = 160, kSlimeJump = 832, kBounce = 3 * 256, kRespawn = 600;
  static constexpr int kSlimeOff[kMaxSlimes] = {-26, -15, -8, 9, 17, 27};  // columns from the spawn column
  struct Slime { pge::EntityId id; int8_t dir; uint16_t wait; int col; };

  bool say(Msg m) {  // returns false so failure paths can `return say(...)`
    msg_ = m;
    msg_t_ = 120;
    return false;
  }
  static void wr32(uint8_t* o, int v) {
    for (int i = 0; i < 4; i++) o[i] = static_cast<uint8_t>(static_cast<uint32_t>(v) >> (8 * i));
  }
  static int rd32(const uint8_t* p) {
    return static_cast<int>(static_cast<uint32_t>(p[0]) | static_cast<uint32_t>(p[1]) << 8 |
                            static_cast<uint32_t>(p[2]) << 16 | static_cast<uint32_t>(p[3]) << 24);
  }

  void resetInventory() {
    memset(inv_, 0, sizeof inv_);
    inv_[0] = Slot{pge::kGenDirt, 10};
    sel_ = 0;
  }
  // First non-full stack of the item, else the first empty slot, else -1.
  int findSlotFor(uint8_t item) const {
    for (int i = 0; i < kSlots; i++)
      if (inv_[i].count > 0 && inv_[i].item == item && inv_[i].count < kStack) return i;
    for (int i = 0; i < kSlots; i++)
      if (inv_[i].count == 0) return i;
    return -1;
  }

  // Is a fg tile of this id within reach? Scans the 11x11 tile box around the player centre (only called when crafting).
  bool nearTile(uint8_t tile) {
    const pge::Body& b = *ent_.body(player_);
    const int cx = (b.x + b.w / 2) / kTile, cy = (b.y + b.h / 2) / kTile;
    for (int dy = -5; dy <= 5; dy++)
      for (int dx = -5; dx <= 5; dx++)
        if (world_.get(cx + dx, cy + dy, pge::Layer::Fg) == tile && inReach(cx + dx, cy + dy)) return true;
    return false;
  }

  void load(pge::Rect r, int tries) {
    for (int i = 0; i < tries && world_.ensureLoaded(r) > 0; i++) {}
  }
  void loadView(int tries) {
    load(pge::Rect{cam_.x - kMargin, cam_.y - kMargin, cam_.view_w + 2 * kMargin, cam_.view_h + 2 * kMargin}, tries);
  }

  // Menu rows in the current language: the TXT_MENU_ ids run in menu order.
  void applyLang() {
    static_assert(TXT_MENU_OVERLAY - TXT_MENU_BRICK + 1 == kMenuItems, "one TXT_MENU_ row per menu item");
    for (int i = 0; i < kMenuItems; i++) menu_text_[i] = text_.get(lang_, static_cast<uint16_t>(TXT_MENU_BRICK + i));
  }

  // Next language (wraps) and the stored choice "lang = xx"; a failed write is ignored (the game keeps running).
  void cycleLang() {
    lang_ = static_cast<uint8_t>((lang_ + 1) % text_.nlang);
    applyLang();
    if (!storage_ok_) return;
    char line[] = "lang = xx\n";
    line[7] = static_cast<char>(text_.codes[2 * lang_]);
    line[8] = static_cast<char>(text_.codes[2 * lang_ + 1]);
    // shortcut: rewrites settings.txt with the lang key only, other keys are lost; merge through pge::Settings when a second key exists
    storage_.writeFile(settings_path, line, sizeof line - 1);
  }

  // Menu is open: Up / Down + A / Start via MenuNav, Left / Right cycle the hotbar slot, B closes.
  void menuInput(const pge::Input& in) {
    using pge::Button;
    const int act = nav_.update(in, kMenuItems);
    if (in.pressed(Button::Left)) selectSlot(sel_ - 1);
    if (in.pressed(Button::Right)) selectSlot(sel_ + 1);
    if (in.pressed(Button::B)) {
      menu_ = false;
    } else if (act >= 0 && act < kRecipes) {
      craft(act);
    } else if (act == kRecipes) {
      saveToStorage();
    } else if (act == kRecipes + 1) {
      loadFromStorage();
    } else if (act == kRecipes + 2) {
      cycleLang();
    } else if (act == kRecipes + 3) {
      overlay = !overlay;
    }
  }

  // Scripted bot, deterministic: walks right until the world edge or a 90-tick stall, then turns; jumps at a wall in
  // front of the feet; every 40 ticks digs the tile in front, 20 ticks later places the held tile there. Returns a
  // synthetic Input so the normal worldTick path (chunk streaming, dig / place, change log) runs unchanged.
  const pge::Input& botInput() {
    using namespace pge;
    const Body& b = *ent_.body(player_);
    stuck_ = b.x == last_x_ ? stuck_ + 1 : 0;
    last_x_ = b.x;
    if (b.x + b.w >= world_.chunksW() * kChunkSize * kTile - 32) bot_dir_ = -1;
    else if (b.x <= 32) bot_dir_ = 1;
    if (stuck_ > 90) { bot_dir_ = -bot_dir_; stuck_ = 0; }
    const int fx = bot_dir_ > 0 ? b.x + b.w : b.x - 1;
    const bool wall = (world_.tileFlags(fx >> 4, (b.y + b.h - 1) >> 4) & kTileSolid) != 0;
    pge_raw_input raw = {};
    raw.buttons = (bot_dir_ > 0 ? PGE_BTN_RIGHT : PGE_BTN_LEFT) | (wall ? PGE_BTN_A : 0) |
                  (ticks_ % 40 == 0 ? PGE_BTN_B : 0) | (ticks_ % 40 == 20 ? PGE_BTN_START : 0);
    bot_in_.update(raw);
    return bot_in_;
  }

  void sample() {
    const pge::Stats& s = eng_->stats();
    bench_.n++;
    bench_.sum_tick_us += s.tick_us;
    bench_.sum_band_us += s.band_us;
    bench_.sum_flush_us += s.flush_us;
    bench_.sum_frame_us += s.frame_us;
    if (s.tick_us > bench_.max_tick_us) bench_.max_tick_us = s.tick_us;
    if (s.band_us > bench_.max_band_us) bench_.max_band_us = s.band_us;
    if (s.frame_us > bench_.max_frame_us) bench_.max_frame_us = s.frame_us;
  }

  void worldTick(const pge::Input& in) {
    using namespace pge;
    loadView(1);
    Body& pb = *ent_.body(player_);
    control(in);
    aim(in, pb);
    const int hx = in.touchX() - 2, hy = in.touchY() - (PGE_LOGICAL_H - 20);  // hotbar cells: 18 px, see onRender
    const bool hot = in.touchPressed() && hx >= 0 && hx < kSlots * 18 && hy >= 0 && hy < 18;
    if (hot) selectSlot(hx / 18);
    if (in.pressed(Button::B) || (in.touchPressed() && !hot)) dig(tx_, ty_);
    if (in.pressed(Button::Start)) place(tx_, ty_);
    for (Slime& s : slimes_) ai(s);
    ent_.update(world_, PhysicsParams{kGrav, kMaxFall});
    contacts();
    ent_.endTick();
    cam_.centerOn(pb.x + pb.w / 2, pb.y + pb.h / 2);
    const Health* h = ent_.health(player_);
    blink_ = h && h->invuln_ticks > 0 && ((ticks_ >> 2) & 1);
    walk_ = (ent_.meta(player_)->flags & kEntOnGround) && (ent_.motion(player_)->vx != 0);
    buildView();
  }

  void control(const pge::Input& in) {
    using pge::Button;
    int dir = (in.held(Button::Right) ? 1 : 0) - (in.held(Button::Left) ? 1 : 0);
    if (dir == 0 && (in.axis(0) > 8192 || in.axis(0) < -8192)) dir = in.axis(0) > 0 ? 1 : -1;
    if (dir) face_ = dir;
    pge::Motion& m = *ent_.motion(player_);
    const int tgt = dir * kMaxRun, v = m.vx;
    m.vx = static_cast<int16_t>(v < tgt ? (v + kAccel > tgt ? tgt : v + kAccel) : (v - kAccel < tgt ? tgt : v - kAccel));
    if ((ent_.meta(player_)->flags & pge::kEntOnGround) && (in.held(Button::A) || (in.held(Button::Up) && !in.held(Button::B)))) m.vy = -kJump;
  }

  static int stick(int v) { return v > 8192 ? 1 : v < -8192 ? -1 : 0; }

  // Target tile: the touch / mouse tile while touching; else, from the aim stick (raw axes 2 / 3, or the Down button on
  // a one-stick board; Up aims above the head (and jumps unless B is held)), the tile beside / above / below the body (diagonals combine both); with no aim, the tile in
  // front of the player at body mid height.
  void aim(const pge::Input& in, const pge::Body& b) {
    if (in.touching()) {
      tx_ = (in.touchX() + cam_.x) >> 4;  // kTile == 16
      ty_ = (in.touchY() + cam_.y) >> 4;
    } else {
      const int ax = stick(in.axis(2));
      int ay = stick(in.axis(3));
      if (ax == 0 && ay == 0) ay = in.held(pge::Button::Down) ? 1 : in.held(pge::Button::Up) ? -1 : 0;  // Down wins if both
      if (ax != 0) face_ = ax;
      const int side = ax != 0 ? ax : (ay == 0 ? face_ : 0);  // 0 = centre column
      tx_ = (b.x + b.w / 2 + side * (b.w / 2 + 8)) >> 4;
      // up: the row above the head's own row; down: the row below the feet's last pixel row (= the tile stood on)
      ty_ = ay < 0 ? (b.y >> 4) - 1 : ay > 0 ? ((b.y + b.h - 1) >> 4) + 1 : (b.y + b.h / 2) >> 4;
    }
    reach_ = inReach(tx_, ty_);
  }

  bool spawnSlime(Slime& s) {
    const int th = pge::terrainHeight(s.col, seed);
    if (world_.get(s.col, th, pge::Layer::Fg) == pge::kTileVoid) return false;  // chunk not loaded yet
    s.id = ent_.spawn(kKindSlime, s.col * kTile + 2, th * kTile - 10, 12, 10, pge::kEntGravity | pge::kEntCollide);
    if (!pge::isValid(s.id)) return false;
    ent_.addHealth(s.id, 3, pge::kHealthAutoReap);
    return true;
  }

  // Walk at patrol speed, turn at a wall or ledge, chase within 96 px horizontally / 32 px vertically (and hop
  // 1-tile steps while chasing). Frozen in unloaded chunks, like the engine's physics.
  void ai(Slime& s) {
    using namespace pge;
    if (!ent_.alive(s.id)) {
      if (s.wait == 0) s.wait = kRespawn;
      else if (--s.wait == 0) spawnSlime(s);
      return;
    }
    Body& b = *ent_.body(s.id);
    Motion& m = *ent_.motion(s.id);
    const uint16_t fl = ent_.meta(s.id)->flags;
    if (world_.get(b.x >> 4, b.y >> 4, Layer::Fg) == kTileVoid ||
        world_.get((b.x + b.w - 1) >> 4, (b.y + b.h - 1) >> 4, Layer::Fg) == kTileVoid) return;
    const Body& pb = *ent_.body(player_);
    const int dx = pb.x + pb.w / 2 - (b.x + b.w / 2), dy = pb.y + pb.h / 2 - (b.y + b.h / 2);
    const bool chase = dx >= -96 && dx <= 96 && dy >= -32 && dy <= 32;
    if (!chase && (fl & kEntHitX)) { s.dir = -s.dir; turns_++; }
    int want = chase ? (dx < 0 ? -1 : dx > 0 ? 1 : s.dir) : s.dir;
    if (fl & kEntOnGround) {
      const int fx = want > 0 ? b.x + b.w : b.x - 1;  // column just in front of the leading edge
      if (!(world_.tileFlags(fx >> 4, (b.y + b.h) >> 4) & kTileSolid)) {  // no floor ahead
        if (chase) want = 0;
        else { s.dir = -s.dir; turns_++; want = s.dir; }
      } else if (chase && (fl & kEntHitX)) {
        m.vy = -kSlimeJump;
      }
    }
    m.vx = static_cast<int16_t>(want * (chase ? kChase : kPatrol));
  }

  // Slime touching the player: from above (player falling) the slime takes 1 and the player bounces, else the
  // player takes 1. Dead players respawn at the spawn point.
  void contacts() {
    using namespace pge;
    const Body pb = *ent_.body(player_);
    ent_.rebuildGrid(Rect{pb.x - 64, pb.y - 64, pb.w + 128, pb.h + 128});
    EntityId hit[8];
    const uint16_t n = ent_.queryOverlap(Rect{pb.x, pb.y, pb.w, pb.h}, hit, 8);
    Motion& pm = *ent_.motion(player_);
    for (uint16_t i = 0; i < n; i++) {
      if (hit[i].index == player_.index) continue;
      const Body* sb = ent_.body(hit[i]);
      if (!sb) continue;
      if (pm.vy > 0 && pb.y + pb.h - sb->y <= 8) {
        ent_.applyDamage(hit[i], 1, kKindPlayer, 10);
        pm.vy = -kBounce;
      } else {
        ent_.applyDamage(player_, 1, kKindSlime, 30);
      }
    }
    bool dead = false;
    Event ev;
    while (ent_.popEvent(ev))
      if (ev.type == EventType::Death && ev.a.index == player_.index && ev.a.gen == player_.gen) dead = true;
    if (dead) {
      deaths_++;
      teleportTo(kSpawnCol * kTile + 2, surfaceY(kSpawnCol) - 14);
      Health* h = ent_.health(player_);
      h->hp = h->max_hp;
      h->invuln_ticks = 60;
    }
  }

  // Copies the tiles around the camera (plus one tile of margin) into small buffers for Canvas::tilemap.
  void buildView() {
    const int x0 = cam_.x >> 4, y0 = cam_.y >> 4;  // kTile == 16; cam_ is >= 0 (the world is larger than the view)
    for (int y = 0; y < kVisH; y++)
      for (int x = 0; x < kVisW; x++) {
        const uint8_t f = world_.get(x0 + x, y0 + y, pge::Layer::Fg), w = world_.get(x0 + x, y0 + y, pge::Layer::Wall);
        vis_fg_[y * kVisW + x] = f < kFgTiles ? f : 0;
        vis_wall_[y * kVisW + x] = w < kWallTiles ? w : 0;
      }
  }

  void makeArt() {
    using pge::rgb;
    const pge::Color fp[] = {0, rgb(70, 190, 60), rgb(40, 130, 40), rgb(150, 100, 50), rgb(110, 70, 30),
                             rgb(130, 130, 140), rgb(100, 100, 112), rgb(240, 200, 40), rgb(80, 230, 230),
                             rgb(170, 60, 45), rgb(200, 190, 170), rgb(70, 70, 78), rgb(255, 140, 20),
                             rgb(200, 205, 215), rgb(120, 125, 140)};
    for (int i = 0; i < 15; i++) fg_pal_[i] = fp[i];
    const pge::Color wp[] = {0, rgb(80, 55, 30), rgb(60, 40, 22), rgb(70, 70, 80), rgb(55, 55, 65)};
    for (int i = 0; i < 5; i++) wall_pal_[i] = wp[i];
    const pge::Color pp[] = {0, rgb(255, 205, 150), rgb(210, 50, 50), rgb(40, 50, 130), rgb(0, 0, 0)};
    for (int i = 0; i < 5; i++) player_pal_[i] = pp[i];
    const pge::Color sp[] = {0, rgb(80, 220, 90), rgb(40, 150, 60), rgb(255, 255, 255), rgb(0, 0, 0)};
    for (int i = 0; i < 5; i++) slime_pal_[i] = sp[i];
    for (int y = 0; y < kTile; y++)
      for (int x = 0; x < kTile; x++) {
        const int o = y * kTile + x;
        for (int t = 1; t < kFgTiles; t++) {
          const int h = (x * 5 + y * 3 + t * 7) % 11;
          const bool blob = (x - 4) * (x - 4) + (y - 5) * (y - 5) < 6 || (x - 11) * (x - 11) + (y - 10) * (y - 10) < 5;
          const bool edge = x == 0 || y == 0 || x == 15 || y == 15;
          fg_px_[t * 256 + o] =
              t == kBrick   ? (y % 4 == 3 || x % 8 == ((y / 4) % 2 ? 3 : 7) ? 10 : 9)  // mortar lines, offset rows
            : t == kFurnace ? (x >= 4 && x <= 11 && y >= 6 && y <= 13 ? 12 : edge ? 6 : 11)  // dark body, fire opening
            : t == kMetal   ? (x >= 14 || y >= 14 || ((x == 3 || x == 11) && (y == 3 || y == 11)) ? 14 : 13)  // rivets
            : t == pge::kGenGrass ? (y < 3 ? (h % 3 ? 1 : 2) : (h ? 3 : 4))
            : t == pge::kGenDirt  ? (h ? 3 : 4)
            : (t >= pge::kGenOre && blob) ? (t == pge::kGenOre ? 7 : 8) : (h < 2 ? 6 : 5);
        }
        const int h = (x * 3 + y * 5) % 11;
        wall_px_[256 + o] = h ? 1 : 2;
        wall_px_[512 + o] = h < 2 ? 4 : 3;
      }
    for (int f = 0; f < 2; f++) {  // 12x14 player: head, hair, shirt, legs (frame 1 = legs apart)
      uint8_t* p = player_px_ + f * 168;
      auto fill = [p](int x0, int y0, int x1, int y1, uint8_t c) {
        for (int y = y0; y <= y1; y++)
          for (int x = x0; x <= x1; x++) p[y * 12 + x] = c;
      };
      fill(3, 0, 8, 4, 1);
      fill(3, 0, 8, 1, 4);
      fill(7, 2, 8, 3, 4);
      fill(2, 5, 9, 9, 2);
      fill(f ? 2 : 3, 10, f ? 4 : 5, 13, 3);
      fill(f ? 7 : 6, 10, f ? 9 : 8, 13, 3);
    }
    for (int f = 0; f < 2; f++) {  // 12x10 slime, frame 1 squashed
      uint8_t* p = slime_px_ + f * 120;
      const int hh = f ? 8 : 10, off = 10 - hh;
      for (int y = off; y < 10; y++)
        for (int x = 0; x < 12; x++) {
          const int u = 2 * x - 11, v = 2 * (y - off) + 1 - hh;
          if (u * u * hh * hh + v * v * 144 <= 144 * hh * hh) p[y * 12 + x] = y == 9 ? 2 : 1;
        }
      p[(3 + off) * 12 + 3] = p[(3 + off) * 12 + 4] = p[(3 + off) * 12 + 7] = p[(3 + off) * 12 + 8] = 3;
      p[(4 + off) * 12 + 4] = p[(4 + off) * 12 + 8] = 4;
    }
  }

  static char* put(char* o, const char* s) {
    while (*s) *o++ = *s++;
    return o;
  }
  static char* putU(char* o, uint32_t v) { return o + pge::ui::formatU32(o, 11, v); }
  void format() {
    // shortcut: HUD tile names, "HP", "FPS", recipe names stay English; add them to game_text.csv if the HUD needs it.
    static const char* const kNames[kFgTiles] = {"-",     "GRASS", "DIRT",   "STONE",  "ORE",
                                                 "DEEP ORE", "BRICK", "FURNACE", "METAL"};
    char* o = put(hud_, "FPS ");
    o = putU(o, eng_ ? eng_->stats().fps : 0);
    o = put(o, "  DEATHS ");
    o = putU(o, deaths_);
    o = put(o, "\nHELD ");
    o = put(o, kNames[held() < kFgTiles ? held() : 0]);
    o = put(o, "  EDITS ");
    o = putU(o, world_.changeCount());
    o = put(o, "/");
    o = putU(o, world_.changeCapacity());
    *o = 0;
  }

  pge::Engine* eng_ = nullptr;
  pge::TileProps props_[256] = {};
  pge::World world_;
  pge::Entities ent_;
  pge::Storage storage_;
  pge::StringTable text_;
  const char* menu_text_[kMenuItems] = {};
  pge::EntityId player_ = pge::kNoEntity;
  Slime slimes_[kMaxSlimes] = {};
  pge::Camera cam_{0, 0, PGE_LOGICAL_W, PGE_LOGICAL_H, 32 * 32 * kTile, 4 * 32 * kTile};
  uint8_t fg_px_[kFgTiles * 256] = {}, wall_px_[kWallTiles * 256] = {};
  uint8_t player_px_[2 * 168] = {}, slime_px_[2 * 120] = {};
  pge::Color fg_pal_[15] = {}, wall_pal_[5] = {}, player_pal_[5] = {}, slime_pal_[5] = {};
  pge::Tileset fg_set_{fg_px_, fg_pal_, kTile, kTile, true};
  pge::Tileset wall_set_{wall_px_, wall_pal_, kTile, kTile, true};
  pge::SpriteSheet fg_icons_{fg_px_, fg_pal_, kTile, kTile, kFgTiles, true};  // hotbar icons: frame = tile id
  pge::SpriteSheet player_sheet_{player_px_, player_pal_, 12, 14, 2, true};
  pge::SpriteSheet slime_sheet_{slime_px_, slime_pal_, 12, 10, 2, true};
  pge::ui::Style style_{&pge::kFont5x7, 0xFFFF, 0x0000, 0xFFE0, 1};
  pge::ui::MenuNav nav_;
  pge::Input bot_in_;
  Bench bench_ = {};
  int bot_dir_ = 1, stuck_ = 0, last_x_ = -1;
  uint8_t vis_fg_[kVisW * kVisH] = {}, vis_wall_[kVisW * kVisH] = {};
  Slot inv_[kSlots] = {};
  int8_t sel_ = 0;
  uint8_t lang_ = 0;
  int tx_ = 0, ty_ = 0, face_ = 1;
  bool reach_ = false, blink_ = false, walk_ = false, menu_ = false, storage_ok_ = false;
  Msg msg_ = MsgNone;
  uint16_t msg_t_ = 0;
  uint32_t ticks_ = 0, turns_ = 0, deaths_ = 0;
  char hud_[96] = {};
};
