// Planet example (S8.6a): a round 4 x 4 chunk planet and one character that walks all the way around it under 4-way
// gravity, with a jetpack, digging and a camera that turns 90 degrees at the diagonals behind a short iris transition.
// The character is drawn along the radial axis ("tilt"): copies of its frames pre-rotated by integer maths in 9 steps of
// 11.25 degrees; collision, gravity and the camera turns stay 4-way. With "rotate" the whole world turns smoothly with
// the planet instead (Canvas::tilemapRotated, screen-up = away from the planet centre, upright character, no iris, no
// tilt); physics and controls stay 4-way, so walk and jump directions still kink at the diagonals. The view turns at
// most 2 degrees per tick and keeps its angle within 48 px of the planet centre, where a few px swing the radial
// direction wildly. A small compass in the top-right corner shows where the planet's world-up is on the screen (both
// view modes). Holding A is dig mode: no gravity, free 360-degree movement with the analog stick, and the rock ahead is
// dug automatically.
// Only public "pge/..." headers are used. The object must stay in place after onCreate (tilesets and the world point
// into it), so keep it static. "Screen frame" = the frame where gravity points down: one clockwise camera turn maps a
// world vector (x, y) (y down) to the screen vector (-y, x), so all walk and aim logic runs there and holding screen
// Right keeps going the same way around the planet after every turn.
#pragma once
#include <stdint.h>
#include <string.h>
#include "pge/camera.h"
#include "pge/collision.h"
#include "pge/color.h"
#include "pge/engine.h"
#include "pge/entity.h"
#include "pge/save.h"
#include "pge/storage.h"
#include "pge/ui.h"
#include "pge/world.h"
#include "pge/worldgen.h"

class PlanetGame {
 public:
  struct Bench {  // sums of Engine::stats() over the autoplay ticks after the 60-tick warm-up
    uint32_t n, sum_tick_us, sum_band_us, sum_flush_us, sum_frame_us;
    uint32_t max_tick_us, max_band_us, max_flush_us, max_frame_us;
  };
  static constexpr int kTile = 8, kWorldPx = 1024, kCentre = 512, kBlob = 11, kChangeCap = 128 * 128 * 2;
  static constexpr int kFgTiles = 7;  // fg tile ids 0..6; ids kFgTiles + t are the lit copies of tile t in the view
  // radius 52 leaves 12 tiles (96 px) of space above the rim at every cardinal point: headroom for jump / jetpack and an unclamped camera
  static constexpr pge::RoundSpec kSpec = {4, 52};
  // Core radius in tiles (kGenCore: solid, not breakable). The body centre stays about a core radius plus half the body
  // from the planet centre, outside the 48 px hold radius of the rotating view. 5 tiles leave only 2 px of room and trap the
  // 16 x 32 character in a gravity flip-flop at the core corner (measured, see docs/user/examples.md). kCoreMax keeps a rim.
  static constexpr int kCoreDefault = 6, kCoreMax = kSpec.radius / 2;
  static_assert(kSpec.chunks * 32 * kTile == kWorldPx, "the world is kSpec.chunks chunks of 32 tiles per side");
  static constexpr int kTurnTicks = 10, kHyst = 6;
  // Screen row of the character's centre: a third from the top (not the middle), so more ground shows below it. The view
  // centre is the point (H / 2 - kCharY) px below the character in the screen frame, in both view modes.
  static constexpr int kCharY = PGE_LOGICAL_H / 3;
  // Tilt index -4..+4 = 9 steps of 11.25 degrees, positive = the head leans to the screen right. Frames per tilt: both walk frames.
  static constexpr int kTiltMax = 4, kTiltN = 2 * kTiltMax + 1, kTiltSideMax = 36;  // 36 = side of the 16 x 32 frame's diagonal
  static constexpr int kTiltBytes = kTiltN * 2 * kTiltSideMax * kTiltSideMax;      // the tilted frame buffer
  static constexpr int tiltSide(int h) {  // canvas side for a 16 x h frame: its diagonal, rounded up to even
    int s = 2;
    while (s * s < 16 * 16 + h * h) s += 2;
    return s;
  }
  static constexpr pge::Color kHeadColor = pge::rgb(250, 90, 20), kBootColor = pge::rgb(200, 30, 200);  // only the character uses them
  // HUD compass: dial centre and radius (px); colours of the arrow's bright tip end (with its "U") and dim tail end
  // ("D")
  static constexpr int kCompassX = PGE_LOGICAL_W - 24, kCompassY = 24, kCompassR = 12;
  static constexpr pge::Color kCompassTip = pge::rgb(255, 250, 110), kCompassTail = pge::rgb(120, 110, 40);

  uint32_t quit_after = 0;    // request quit after this many ticks; 0 = run until the platform quits
  bool autoplay = false;      // ignore the Input and run the scripted bot (benchmarks and tests)
  bool tilt = true;           // draw the character along the radial axis; false = upright in the 4 gravity directions
  bool rotate = false;        // the world turns smoothly with the planet (set before onCreate); no iris, tilt is ignored
  uint32_t seed = 12345;      // world seed (set before onCreate)
  int core_tiles = kCoreDefault;  // radius in tiles of the unbreakable planet core (set before onCreate), 0 = none
  uint8_t char_h = 32;     // configured character height in px, 24 or 32 (width is always 16), read by onCreate; Select changes only charH()
  uint32_t change_cap = kChangeCap;  // change log entries (5 B each); the default is the planet worst case
  pge::Arena* log_arena = nullptr;   // injected because on the F429 the log must live in external SDRAM (PGE_RAM_EXT); S8.6b
  const char* save_path = "planet.sav";
  bool load_on_start = false;  // onCreate ends with loadFromStorage()

  pge::Error onCreate(pge::Engine& e) {
    using namespace pge;
    eng_ = &e;
    ch_ = char_h == 32 ? 32 : 24;
    for (int t = kGenGrass; t <= kGenDeepOre; t++) props_[t] = TileProps{kTileSolid | kTileBreakable, 4, 0};
    props_[kGenCore] = TileProps{kTileSolid, 255, 0};  // solid, not breakable: dig and touch skip it
    core_gen_ = core_tiles < 0 ? 0 : core_tiles > kCoreMax ? kCoreMax : core_tiles;
    props_[kTileVoid] = TileProps{kTileSolid, 255, 0};
    makeArt();
    buildTilt();
    WorldConfig wc = {};
    wc.w_chunks = wc.h_chunks = kSpec.chunks;
    wc.seed = seed;
    wc.gen = gen;
    wc.props = props_;
    wc.cache_slots = 16;  // the whole planet stays resident
    wc.change_capacity = change_cap;
    wc.tile_px = kTile;
    wc.log_arena = log_arena;
    Error err = world_.init(wc, e.initArena());
    if (err != Ok) return err;
    err = ent_.init(e.initArena(), 4);
    if (err != Ok) return err;
    e.trackPool(0, &ent_.counters());
    err = storage_.init(e.initArena(), 512);
    if (err != Ok) return err;
    storage_ok_ = storage_.mount() == Ok;
    loadAll();
    player_ = ent_.spawn(kKindPlayer, 0, 0, 16, ch_, kEntGravity | kEntCollide);
    if (!isValid(player_)) return OutOfMemory;
    ticks_ = switches_ = 0;
    bench_ = Bench{};
    bot_in_ = Input{};
    stuck_ = 0;
    msg_ = MsgNone;
    msg_t_ = 0;
    walk_ = false;
    dig_ = false;
    dir_ = 0;
    teleportToSurface(GravityDir::Down);
    format();
    if (load_on_start) loadFromStorage();
    return Ok;
  }

  void onTick(const pge::Input& real) {
    using namespace pge;
    const Input& want = autoplay ? botInput() : real;
    if (turn_ > 0) turn_--;
    world_.ensureLoaded(Rect{0, 0, kWorldPx, kWorldPx}, 1);
    updateGravity();
    const uint8_t o = orientForGravity(gd_);
    if (o != cam_.orient) {
      cam_.orient = o;  // with rotate it only keeps the 4-way control frame: no iris, no input freeze
      if (!rotate) turn_ = kTurnTicks;
    }
    const Input& in = turn_ > 0 ? idle_ : want;  // input is ignored during the turn; physics keeps running
    if (in.pressed(Button::Select)) toggleChar();
    if (in.pressed(Button::Start)) saveToStorage();
    dig_ = want.held(Button::A);  // from want, not in: the iris turn (in = idle) must not drop dig mode
    pge::EntityMeta& em = *ent_.meta(player_);
    if (dig_) em.flags &= (uint16_t)~kEntGravity;
    else em.flags |= kEntGravity;
    if (dig_) digMove(in);
    else control(in);
    aim(in);
    if (in.pressed(Button::B)) digZone();
    if (in.touchPressed()) touchDig(in.touchX(), in.touchY());  // after B, so B still digs the aimed zone
    ent_.update(world_, PhysicsParams{kGrav, kMaxFall});
    stepUp();
    ent_.endTick();
    const Body& b = *ent_.body(player_);
    follow(b.x + b.w / 2, b.y + b.h / 2, false);
    walk_ = dir_ != 0 && (ent_.meta(player_)->flags & kEntOnGround);
    updateTilt();
    buildView();
    if (msg_t_ > 0) msg_t_--;
    ticks_++;
    if (autoplay && eng_ && ticks_ > 60) sample();
    format();
    if (quit_after != 0 && ticks_ >= quit_after && eng_) eng_->requestQuit();
  }

  // Runs once per band: everything drawn is a pure function of the state set in onCreate / onTick.
  void onRender(pge::Canvas& c) {
    using namespace pge;
    c.clear(rgb(6, 6, 20));
    if (rotate) drawRotated(c);
    else drawTurned(c);
    c.text(kFont5x7, 3, 3, hud_, rgb(0, 0, 0));
    c.text(kFont5x7, 2, 2, hud_, rgb(255, 255, 255));
    drawCompass(c);
  }

  // The 4-way camera: tile maps turned by orient, the character tilted or upright, the iris at a turn.
  void drawTurned(pge::Canvas& c) {
    using namespace pge;
    const int ox = cam_.x - win_x_ * kTile, oy = cam_.y - win_y_ * kTile;
    c.tilemap(TileLayer{vis_wall_, vis_n_, vis_n_, &wall_set_}, ox, oy, cam_.orient);
    c.tilemap(TileLayer{vis_fg_, vis_n_, vis_n_, &fg_set_}, ox, oy, cam_.orient);
    const Rect r = playerScreenRect();
    const SpriteFlags own = static_cast<SpriteFlags>(static_cast<uint8_t>(gd_) * 4);  // rotation field == gravity value
    const int fr = walk_ ? (ticks_ >> 3) & 1 : 0;
    const SpriteFlags fl = cam_.spriteFlags(face_ < 0 ? own | FlipX : own);
    if (!tilt) {
      c.sprite(charSheet(), fr, r.x, r.y, fl);
    } else {  // a flipped tilt +k image is the tilt -k image: facing left uses the mirrored index and the mirrored pivot
      const int i = (face_ < 0 ? -tilt_i_ : tilt_i_) + kTiltMax, s = tilt_sheet_.w;
      c.sprite(tilt_sheet_, i * 2 + fr, r.x + r.w / 2 - (face_ < 0 ? s - tilt_ox_[i] : tilt_ox_[i]), r.y + r.h - tilt_oy_[i], fl);
    }
    if (turn_ > 0) {  // iris: the bars shrink from half the screen each to nothing
      const int h = PGE_LOGICAL_H / 2 * turn_ / kTurnTicks;
      c.fillRect(Rect{0, 0, PGE_LOGICAL_W, h}, 0);
      c.fillRect(Rect{0, PGE_LOGICAL_H - h, PGE_LOGICAL_W, h}, 0);
    }
  }

  // The rotating camera: both layers through tilemapRotated about the view centre (always at the screen centre), the
  // character upright at the screen position of its reference point (row kCharY); the aim zone shows as lit tiles in the fg layer.
  void drawRotated(pge::Canvas& c) {
    using namespace pge;
    const int ax = view_x_ - win_x_ * kTile, ay = view_y_ - win_y_ * kTile;
    const int cx = PGE_LOGICAL_W / 2, cy = PGE_LOGICAL_H / 2;
    c.tilemapRotated(TileLayer{vis_wall_, vis_n_, vis_n_, &wall_set_}, ax, ay, cx, cy, view_cos_, view_sin_);
    c.tilemapRotated(TileLayer{vis_fg_, vis_n_, vis_n_, &fg_set_}, ax, ay, cx, cy, view_cos_, view_sin_);
    const Rect r = charScreenRect();
    c.sprite(charSheet(), walk_ ? (ticks_ >> 3) & 1 : 0, r.x, r.y, face_ < 0 ? FlipX : None);
  }

  // A dial with an arrow through its centre: the bright tip (and "U") points to the planet's world-up as this view
  // shows it, the dim tail (and "D") to world-down. The 4-way camera turns it in quarter turns, the rotating view
  // smoothly.
  void drawCompass(pge::Canvas& c) const {
    using namespace pge;
    int ux = 0, uy = -16384;  // world-up as a Q14 screen vector
    if (rotate) {
      ux = view_sin_;
      uy = -view_cos_;
    } else {
      toScreen(cam_.orient, ux, uy);
    }
    int x, y, x2, y2;
    // the point "along" the arrow and "side" of it, in px from the dial centre
    auto pt = [&](int along, int side, int& px, int& py) {
      px = kCompassX + ((ux * along - uy * side + 8192) >> 14);
      py = kCompassY + ((uy * along + ux * side + 8192) >> 14);
    };
    c.fillCircle(kCompassX, kCompassY, kCompassR, rgb(10, 12, 30));
    c.circle(kCompassX, kCompassY, kCompassR, rgb(200, 200, 215));
    pt(-10, 0, x, y);
    c.line(kCompassX, kCompassY, x, y, kCompassTail);
    pt(10, 0, x, y);
    c.line(kCompassX, kCompassY, x, y, kCompassTip);
    for (int side = -3; side <= 3; side += 6) {  // arrow head: two barbs back from the tip
      pt(5, side, x2, y2);
      c.line(x, y, x2, y2, kCompassTip);
    }
    for (int i = 0; i < 2; i++) {  // the letters sit just outside the dial on the arrow's axis, shadowed like the HUD
      pt(i ? -18 : 18, 0, x, y);
      c.text(kFont5x7, x - 1, y - 2, i ? "D" : "U", rgb(0, 0, 0));
      c.text(kFont5x7, x - 2, y - 3, i ? "D" : "U", i ? kCompassTail : kCompassTip);
    }
  }

  // Digs the fg tile if it is breakable. False if refused or if the change log is full ("LOG FULL" on the HUD).
  bool dig(int tx, int ty) {
    using namespace pge;
    if (!(world_.props(world_.get(tx, ty, Layer::Fg)).flags & kTileBreakable)) return false;
    const Error err = world_.set(tx, ty, Layer::Fg, kTileAir);
    if (err == OutOfMemory) say(MsgLogFull);
    return err == Ok;
  }

  // Digs every breakable fg tile of the 2 x 2 aim zone (tx_, ty_ = its top-left tile); the others are skipped. True if at
  // least one tile was dug; "LOG FULL" (set by dig) if any tile hit a full change log.
  bool digZone() {
    bool any = false;
    for (int i = 0; i < 4; i++) any = dig(tx_ + (i & 1), ty_ + (i >> 1)) || any;
    return any;
  }

  // Test helper and spawn: stands the character on the rim at the middle of the quadrant whose gravity is "side"
  // (Down = top of the planet, Right = left side, Up = bottom, Left = right side); no transition.
  void teleportToSurface(pge::GravityDir side) {
    using pge::GravityDir;
    const int box = kCentre - 8;  // the 16 px side of the box centred on the planet
    switch (side) {
      case GravityDir::Down: setPose(side, box, scan(true, 0, 1) * kTile - ch_); break;
      case GravityDir::Up: setPose(side, box, (scan(true, kWorldPx / kTile - 1, -1) + 1) * kTile); break;
      case GravityDir::Left: setPose(side, (scan(false, kWorldPx / kTile - 1, -1) + 1) * kTile, box); break;
      default: setPose(side, scan(false, 0, 1) * kTile - ch_, box); break;
    }
  }

  // Test helper: the character's box at (x, y) in gravity g (no free-space check); camera and view snap to it.
  void teleportTo(pge::GravityDir g, int x, int y) { setPose(g, x, y); }

  // ---- save / load (explicit only: no autosave, flash wear) ----
  // shortcut: the save has no core size, so a save from another core radius digs through the new core; add a byte (blob
  // version 2) if saves must survive a core change.
  bool saveToStorage() {
    using namespace pge;
    if (!storage_ok_) return say(MsgNoStorage);
    uint8_t blob[kBlob];
    const Body& b = *ent_.body(player_);
    blob[0] = 1;
    wr32(blob + 1, b.x);
    wr32(blob + 5, b.y);
    blob[9] = static_cast<uint8_t>(gd_);
    blob[10] = ch_;
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
    loadAll();  // loadGame (even a failed one) frees chunks, and the checks below read tiles
    if (err != Ok) {
      // a failed load may still have cleared the change log (rock back under the body): free the character if so
      if (!isFree(*ent_.body(player_))) teleportToSurface(GravityDir::Down);
      buildView();
      return say(err == Io ? MsgNoSave : MsgLoadFailed);
    }
    // The blob is a trust boundary (a valid CRC does not make the content sane).
    bool ok = len == kBlob && blob[0] == 1 && blob[9] < 4 && (blob[10] == 24 || blob[10] == 32);
    const GravityDir g = static_cast<GravityDir>(blob[9] & 3);
    const int px = rd32(blob + 1), py = rd32(blob + 5), ch = blob[10];
    const Body nb{px, py, vertical(g) ? 16 : ch, vertical(g) ? ch : 16};
    ok = ok && px >= 0 && px <= kWorldPx - nb.w && py >= 0 && py <= kWorldPx - nb.h && isFree(nb);
    if (!ok) {
      world_.clearChanges();
      loadAll();
      teleportToSurface(GravityDir::Down);
      return say(MsgLoadFailed);
    }
    ch_ = static_cast<uint8_t>(ch);
    buildTilt();
    ticks_ = tick;
    setPose(g, px, py);
    say(MsgLoaded);
    return true;
  }

  // The spec of the world onCreate generates: kSpec with the core radius it was given.
  static pge::RoundSpec spec() { return pge::RoundSpec{kSpec.chunks, kSpec.radius, core_gen_}; }
  const Bench& bench() const { return bench_; }
  pge::World& world() { return world_; }
  pge::Entities& entities() { return ent_; }
  pge::EntityId player() const { return player_; }
  pge::GravityDir gravity() const { return gd_; }
  bool digging() const { return dig_; }
  uint8_t orient() const { return cam_.orient; }
  pge::Rect playerScreenRect() {
    const pge::Body& b = *ent_.body(player_);
    return cam_.toScreen(pge::Rect{b.x, b.y, b.w, b.h});
  }
  bool turning() const { return turn_ > 0; }
  // Rotating view (rotate = true): the unit vector (Q14, 16384 = 1.0) that turns the world clockwise on screen, the
  // world point shown at the screen centre, and the rect of the character sprite (16 x charH, upright).
  int viewCos() const { return view_cos_; }
  int viewSin() const { return view_sin_; }
  int viewX() const { return view_x_; }
  int viewY() const { return view_y_; }
  pge::Rect charScreenRect() const {
    int sx, sy;
    toViewScreen(ref_x_, ref_y_, sx, sy);
    return pge::Rect{sx - 8, sy - ch_ / 2, 16, ch_};
  }
  // World point <-> screen pixel of the picture the current view mode draws (4-way or rotating). A screen pixel maps by its
  // centre, as the drawing samples it, to the world pixel under it; the rotating view rounds to whole pixels, so a round trip
  // can be 1 px off there. Both use the camera state of the last tick, i.e. what the last frame showed.
  void worldToScreen(int wx, int wy, int& sx, int& sy) const {
    if (rotate) {
      toViewScreen(wx, wy, sx, sy);
      return;
    }
    const pge::Rect r = cam_.toScreen(pge::Rect{wx, wy, 1, 1});
    sx = r.x;
    sy = r.y;
  }
  void screenToWorld(int sx, int sy, int& wx, int& wy) const {
    int ux = 2 * sx + 1 - PGE_LOGICAL_W, uy = 2 * sy + 1 - PGE_LOGICAL_H;  // pixel centre from the screen centre, doubled
    if (rotate) {  // the transpose of toViewScreen; Q14 and the doubling make the shift 15
      wx = view_x_ + ((view_cos_ * ux + view_sin_ * uy) >> 15);
      wy = view_y_ + ((view_cos_ * uy - view_sin_ * ux) >> 15);
      return;
    }
    toWorld(cam_.orient, ux, uy);  // the camera turned the world clockwise by orient: undo it about the screen centre
    wx = (2 * cam_.x + cam_.visW() + ux) >> 1;
    wy = (2 * cam_.y + cam_.visH() + uy) >> 1;
  }
  uint32_t switches() const { return switches_; }
  uint32_t ticks() const { return ticks_; }
  uint8_t charH() const { return ch_; }
  int tiltIndex() const { return tilt_i_; }  // -4..+4, set every tick whether or not "tilt" draws it
  const pge::SpriteSheet& charSheet() const { return ch_ == 32 ? sheet32_ : sheet24_; }
  const pge::SpriteSheet& tiltSheet() const { return tilt_sheet_; }  // frame (index + 4) * 2 + walk frame, square tiltSide(charH())
  int targetX() const { return tx_; }  // top-left tile of the 2 x 2 aim zone
  int targetY() const { return ty_; }
  // The fg tile id the last tick put in the view window for world tile (tx, ty) (kFgTiles + id = lit); 0 outside the window.
  uint8_t viewTile(int tx, int ty) const {
    const int x = tx - win_x_, y = ty - win_y_;
    return x >= 0 && x < vis_n_ && y >= 0 && y < vis_n_ ? vis_fg_[y * vis_n_ + x] : 0;
  }
  const char* message() const { return msg_t_ > 0 ? kMsgs[msg_] : ""; }  // HUD message, "" when expired

 private:
  enum { kKindPlayer = 0 };
  enum Msg : uint8_t { MsgNone, MsgLogFull, MsgNoRoom, MsgSaved, MsgSaveFailed, MsgNoSave, MsgLoadFailed, MsgLoaded,
                       MsgNoStorage };
  static constexpr const char* const kMsgs[] = {"",        "LOG FULL",    "NO ROOM",   "SAVED",     "SAVE FAILED",
                                                "NO SAVE", "LOAD FAILED", "LOADED",    "NO STORAGE"};
  // 8.8 fixed point: 256 = 1 px (per tick for velocities); jetpack and run limits are in the screen frame
  static constexpr int kGrav = 40, kMaxFall = 4 * 256, kJump = 640, kMaxRun = 384, kAccel = 48, kThrust = 72, kJetMax = 384;
  static constexpr int kDig = 192;  // dig mode speed (0.75 px per tick) at full stick, in any direction
  // Tile window: 42 x 42 covers either 4-way orient; the rotating view needs the 200 px half diagonal of the screen on
  // every side of the view centre (52 tiles = 208 px, 8 px of window-origin slack leave at least 201).
  static constexpr int kVisStd = 42, kVis = 52, kWallTiles = 3;
  // Rotating view: a body jump is released at kRelease px per tick and the view centre's distance from the planet centre
  // eases by a third of the gap, at most kEaseMax px per tick (a fast fall shows the character sinking a little).
  static constexpr int kRelease = 2, kEaseMax = 3;
  // Rotating view: the view vector turns at most 2 degrees per tick towards the target angle (Q14 cos / sin of the
  // step; walking on the surface turns about 0.2), and inside kHoldPx of the planet centre the target stays where it
  // was: there a few px of motion swing the direction from the centre to the character by tens of degrees.
  static constexpr int kTurnCos = 16374, kTurnSin = 572, kHoldPx = 48;
  static constexpr int kUx[4] = {0, 1, 0, -1}, kUy[4] = {-1, 0, 1, 0};  // against gravity, by GravityDir value

  // GenFn has no context: onCreate leaves the core radius here, and every chunk of that world is made from it.
  static inline int core_gen_ = kCoreDefault;
  static void gen(uint8_t* fg, uint8_t* wall, int cx, int cy, uint32_t seed) {
    pge::roundGen(spec(), fg, wall, cx, cy, seed);
  }

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
  static int ab(int v) { return v < 0 ? -v : v; }
  static bool vertical(pge::GravityDir g) { return g == pge::GravityDir::Down || g == pge::GravityDir::Up; }
  // The two frame changes, applied orient times: a clockwise turn maps (x, y) to (-y, x); the inverse maps (x, y) to (y, -x).
  static void toScreen(int orient, int& x, int& y) {
    for (int i = 0; i < (orient & 3); i++) {
      const int t = x;
      x = -y;
      y = t;
    }
  }
  static void toWorld(int orient, int& x, int& y) {
    for (int i = 0; i < (orient & 3); i++) {
      const int t = x;
      x = y;
      y = -t;
    }
  }

  void loadAll() { world_.ensureLoaded(pge::Rect{0, 0, kWorldPx, kWorldPx}, 16); }

  // First row (along_y) or column from i, stepping by step, with a solid tile in the two centre tile lines.
  int scan(bool along_y, int i, int step) const {
    for (; i >= 0 && i < kWorldPx / kTile; i += step)
      for (int k = kCentre / kTile - 1; k <= kCentre / kTile; k++)
        if (world_.tileFlags(along_y ? k : i, along_y ? i : k) & pge::kTileSolid) return i;
    return 0;  // not reached: the planet is solid at its centre
  }

  // True if no tile under the box is solid (unloaded, void and out-of-world tiles count as solid).
  bool isFree(const pge::Body& b) const {
    for (int ty = b.y >> 3; ty <= (b.y + b.h - 1) >> 3; ty++)
      for (int tx = b.x >> 3; tx <= (b.x + b.w - 1) >> 3; tx++)
        if (world_.tileFlags(tx, ty) & pge::kTileSolid) return false;
    return true;
  }

  // Camera top-left from the world point (px, py) of the character, shown at (W / 2, kCharY): the screen centre is that point
  // moved (H / 2 - kCharY) px down the screen frame. Not clamped: at the rim the character keeps its place and space shows
  // beyond the world edge (buildView draws tiles outside the world as empty).
  void follow(int px, int py, bool snap) {
    int ox = 0, oy = PGE_LOGICAL_H / 2 - kCharY;
    toWorld(cam_.orient, ox, oy);
    cam_.x = px + ox - cam_.visW() / 2;
    cam_.y = py + oy - cam_.visH() / 2;
    if (rotate) followRot(px, py, snap);
  }

  // Rotating view. The reference point is the body centre plus off_ (a body jump not yet released). Screen-up is the
  // direction from the planet centre to it, as an exact Q14 unit vector (integer square root, no trig), so the character
  // stays on the vertical centre line. The view centre lies on that ray, (H / 2 - kCharY) px nearer the planet centre than
  // the reference point (so the character shows at row kCharY); only its distance from the planet centre eases.
  // The jump release keeps the world still when the body hops 8 px (stepUp / switch nudge) along an axis that is partly
  // tangential near the diagonals. snap (pose set, load): no easing, no release, no turn limit, no hold.
  void followRot(int px, int py, bool snap) {
    off_x_ = snap ? 0 : release(off_x_);
    off_y_ = snap ? 0 : release(off_y_);
    ref_x_ = px + off_x_;
    ref_y_ = py + off_y_;
    const int vx = ref_x_ - kCentre, vy = ref_y_ - kCentre;
    const int r = isqrtRound(static_cast<uint32_t>(vx * vx + vy * vy) << 8);  // distance in 1/16 px: unit vector good to 0.01%
    if (r > 0 && (snap || r >= kHoldPx * 16)) {  // r == 0 only exactly at the planet centre
      tgt_cos_ = -vy * 16 * 16384 / r;  // up = (vx, vy) / r, and up maps to (0, -1) on screen
      tgt_sin_ = -vx * 16 * 16384 / r;
    }
    if (snap) {
      view_cos_ = tgt_cos_;
      view_sin_ = tgt_sin_;
    } else {
      turnView();
    }
    const int rv = r - (PGE_LOGICAL_H / 2 - kCharY) * 16;  // the view centre's distance from the planet centre, 1/16 px
    const int d = rv - view_r_, st = (ab(d) + 2) / 3;  // a third of the gap, rounded up so the gap closes exactly
    view_r_ = snap ? rv : view_r_ + (d < 0 ? -1 : 1) * (st > kEaseMax * 16 ? kEaseMax * 16 : st);
    view_x_ = kCentre + ((-view_sin_ * view_r_ + (1 << 17)) >> 18);  // Q14 vector times Q4 distance = Q18
    view_y_ = kCentre + ((-view_cos_ * view_r_ + (1 << 17)) >> 18);
  }

  // The view vector goes to the target vector, by at most the 2 degree step: inside the step it takes the target
  // exactly (so quarter turns stay exact), else it turns by the step Q14 pair and is renormalised with the rounded
  // isqrt (so no component passes 16384, which tilemapRotated would drop). The angle test is tan(angle) <= kTurnSin /
  // kTurnCos, so the vector lengths do not matter.
  void turnView() {
    const int64_t dot = static_cast<int64_t>(view_cos_) * tgt_cos_ + static_cast<int64_t>(view_sin_) * tgt_sin_;
    // cross > 0: the target is clockwise of the view
    const int64_t cross = static_cast<int64_t>(view_cos_) * tgt_sin_ - static_cast<int64_t>(view_sin_) * tgt_cos_;
    if (dot > 0 && (cross < 0 ? -cross : cross) * kTurnCos <= dot * kTurnSin) {
      view_cos_ = tgt_cos_;
      view_sin_ = tgt_sin_;
      return;
    }
    const int sg = cross < 0 ? -1 : 1;  // exactly opposite (cross 0): clockwise
    const int c = (view_cos_ * kTurnCos - sg * view_sin_ * kTurnSin) >> 14;
    const int s = (view_sin_ * kTurnCos + sg * view_cos_ * kTurnSin) >> 14;
    const int n = isqrtRound(static_cast<uint32_t>(c * c + s * s));
    view_cos_ = c * 16384 / n;
    view_sin_ = s * 16384 / n;
  }

  // The body jumped by (dx, dy) px in one tick: the reference point stays and releases over a few ticks.
  void jump(int dx, int dy) {
    if (!rotate) return;
    off_x_ -= dx;
    off_y_ -= dy;
  }
  static int release(int v) { return v > kRelease ? v - kRelease : v < -kRelease ? v + kRelease : 0; }
  static int isqrtRound(uint32_t n) {  // round(sqrt(n)); never below an integer component, so |Q14 vector| <= 1.0
    uint32_t r = 0;
    for (uint32_t b = 1u << 15; b; b >>= 1)
      if ((r + b) * (r + b) <= n) r += b;
    return static_cast<int>(n - r * r > r ? r + 1 : r);
  }
  // World point to the rotating view's screen: the offset from the view centre turned clockwise by the view vector.
  void toViewScreen(int wx, int wy, int& sx, int& sy) const {
    const int dx = wx - view_x_, dy = wy - view_y_;
    sx = PGE_LOGICAL_W / 2 + ((view_cos_ * dx - view_sin_ * dy + 8192) >> 14);
    sy = PGE_LOGICAL_H / 2 + ((view_sin_ * dx + view_cos_ * dy + 8192) >> 14);
  }

  // Puts the character in gravity g with its box at (x, y); camera orient follows at once, no transition.
  void setPose(pge::GravityDir g, int x, int y) {
    using namespace pge;
    gd_ = g;
    Body& b = *ent_.body(player_);
    b = Body{x, y, vertical(g) ? 16 : ch_, vertical(g) ? ch_ : 16};
    *ent_.motion(player_) = Motion{0, 0, 0, 0};
    ent_.meta(player_)->gravity_dir = g;
    cam_.orient = orientForGravity(g);
    turn_ = 0;
    face_ = 1;
    dir_ = 0;
    dig_ = false;
    stuck_ = 0;
    last_x_ = x + b.w / 2;
    last_y_ = y + b.h / 2;
    bot_orient_ = cam_.orient;
    updateTilt();
    follow(x + b.w / 2, y + b.h / 2, true);
    aim(idle_);  // no stale lit aim tiles before the first tick; after follow: the rotating view's aim reads its vector
    buildView();
  }

  // Tilt = angle between screen-up and the vector from the planet centre to the body centre (in the screen frame), as the
  // nearest of the 9 steps: |x| * 4096 against up * tan(5.625, 16.875, 28.125, 39.375 degrees) in Q12, no atan. Beyond the last
  // boundary it stays at the end step (the gravity switch hysteresis lives in updateGravity).
  void updateTilt() {
    static constexpr int kTan[kTiltMax] = {403, 1243, 2189, 3362};
    const pge::Body& b = *ent_.body(player_);
    int x = b.x + b.w / 2 - kCentre, y = b.y + b.h / 2 - kCentre;
    toScreen(cam_.orient, x, y);
    const int ax = ab(x) * 4096;
    int k = 0;
    while (k < kTiltMax && ax >= -y * kTan[k]) k++;
    tilt_i_ = x < 0 ? -k : k;
  }

  // Rebuilds the tilted copies of both walk frames of the live character size (nearest neighbour, inverse mapping, index 0 =
  // transparent). The pivot is the feet midpoint (frame pixel edge (8, h)). A canvas is the frame's diagonal wide, which is
  // enough only if the rotated picture is centred in it, so the pivot sits at a per-tilt canvas point (tilt_ox_ / tilt_oy_, on
  // pixel edges): draw at (feet - pivot). Index +k and -k mirror each other, so ox(-k) = side - ox(k).
  void buildTilt() {
    static constexpr int kCos[kTiltMax + 1] = {4096, 4017, 3784, 3406, 2896}, kSin[kTiltMax + 1] = {0, 799, 1567, 2276, 2896};
    const int h = ch_, s = tiltSide(h);
    tilt_sheet_ = pge::SpriteSheet{tilt_px_, player_pal_, static_cast<uint16_t>(s), static_cast<uint16_t>(s), kTiltN * 2, true};
    for (int t = -kTiltMax; t <= kTiltMax; t++) {
      const int a = ab(t), c = kCos[a], sn = t < 0 ? -kSin[a] : kSin[a];
      int mnx = 1 << 30, mxx = -mnx, mny = mnx, mxy = -mnx;  // Q12 bounds of the rotated frame corners about the pivot, as for +a
      for (int q = 0; q < 4; q++) {
        const int u = (q & 1 ? 8 : -8) * 4096, v = (q & 2 ? -h : 0) * 4096;
        const int rx = static_cast<int>((static_cast<int64_t>(u) * c - static_cast<int64_t>(v) * kSin[a]) >> 12);
        const int ry = static_cast<int>((static_cast<int64_t>(u) * kSin[a] + static_cast<int64_t>(v) * c) >> 12);
        if (rx < mnx) mnx = rx;
        if (rx > mxx) mxx = rx;
        if (ry < mny) mny = ry;
        if (ry > mxy) mxy = ry;
      }
      const int oxp = (s * 4096 - (mnx + mxx) + 4096) >> 13, oy = (s * 4096 - (mny + mxy) + 4096) >> 13;
      const int ox = t < 0 ? s - oxp : oxp, idx = t + kTiltMax;
      tilt_ox_[idx] = static_cast<uint8_t>(ox);
      tilt_oy_[idx] = static_cast<uint8_t>(oy);
      for (int f = 0; f < 2; f++) {
        const uint8_t* src = charSheet().px + f * 16 * h;
        uint8_t* dst = tilt_px_ + (idx * 2 + f) * s * s;
        for (int j = 0; j < s; j++)
          for (int i = 0; i < s; i++) {
            const int X = 2 * i + 1 - 2 * ox, Y = 2 * j + 1 - 2 * oy;  // pixel centre about the pivot, in half pixels
            const int fx = (8 * 8192 + X * c + Y * sn) >> 13, fy = (h * 8192 - X * sn + Y * c) >> 13;
            dst[j * s + i] = fx >= 0 && fx < 16 && fy >= 0 && fy < h ? src[fy * 16 + fx] : 0;
          }
      }
    }
  }

  // Gravity follows the planet centre; the switch needs kHyst px of lead on the new axis so a diagonal does not flip-flop.
  void updateGravity() {
    using namespace pge;
    const Body& b = *ent_.body(player_);
    const int cx = b.x + b.w / 2, cy = b.y + b.h / 2;
    const GravityDir cand = roundGravity(cx, cy, kCentre, kCentre);
    if (cand == gd_) return;
    const int ax = ab(cx - kCentre), ay = ab(cy - kCentre);
    if ((vertical(cand) ? ay - ax : ax - ay) >= kHyst) switchGravity(cand);
  }

  // The box is world-axis aligned: changing the axis family swaps w and h around the centre (all sizes are even). If
  // the new box overlaps rock it may shift up to 8 px against the new gravity; if nothing fits, gravity stays.
  bool switchGravity(pge::GravityDir nd) {
    using namespace pge;
    Body& b = *ent_.body(player_);
    Body nb = vertical(nd) != vertical(gd_) ? Body{b.x + (b.w - b.h) / 2, b.y + (b.h - b.w) / 2, b.h, b.w} : b;
    const int ux = kUx[static_cast<int>(nd)], uy = kUy[static_cast<int>(nd)];
    int s = 0;
    while (s <= 8 && !isFree(Body{nb.x + ux * s, nb.y + uy * s, nb.w, nb.h})) s++;
    if (s > 8) return false;
    nb.x += ux * s;
    nb.y += uy * s;
    jump(ux * s, uy * s);
    Motion& m = *ent_.motion(player_);
    int vx = m.vx, vy = m.vy;
    toScreen(orientForGravity(gd_), vx, vy);  // the velocity keeps its screen-frame direction across the turn
    toWorld(orientForGravity(nd), vx, vy);
    m.vx = static_cast<int16_t>(vx);
    m.vy = static_cast<int16_t>(vy);
    m.rx = m.ry = 0;
    b = nb;
    gd_ = nd;
    ent_.meta(player_)->gravity_dir = nd;
    switches_++;
    return true;
  }

  static int stick(int v) { return v > 8192 ? 1 : v < -8192 ? -1 : 0; }

  void control(const pge::Input& in) {
    using pge::Button;
    int dir = (in.held(Button::Right) ? 1 : 0) - (in.held(Button::Left) ? 1 : 0);
    if (dir == 0) dir = stick(in.axis(0));
    dir_ = dir;
    if (dir) face_ = dir;
    pge::Motion& m = *ent_.motion(player_);
    int sx = m.vx, sy = m.vy;
    toScreen(cam_.orient, sx, sy);
    const int tgt = dir * kMaxRun;
    sx = sx < tgt ? (sx + kAccel > tgt ? tgt : sx + kAccel) : (sx - kAccel < tgt ? tgt : sx - kAccel);
    const bool ground = (ent_.meta(player_)->flags & pge::kEntOnGround) != 0;
    // stick up (Button::Up) jumps and flies; it still aims up as well (A is dig mode)
    const bool jumpKey = in.pressed(Button::Up);
    const bool jetKey = in.held(Button::Up);
    if (ground && jumpKey) sy = -kJump;
    // shortcut: jetpack has no fuel, add when the game needs it
    else if (!ground && jetKey && sy > -kJetMax) sy = sy - kThrust < -kJetMax ? -kJetMax : sy - kThrust;
    toWorld(cam_.orient, sx, sy);
    m.vx = static_cast<int16_t>(sx);
    m.vy = static_cast<int16_t>(sy);
  }

  // Dig mode: flies along the stick (screen frame, any direction, speed in proportion to the deflection, the arrow buttons
  // give 8 directions inside the dead zone) with no acceleration, and digs the breakable tiles within 4 px ahead.
  void digMove(const pge::Input& in) {
    using pge::Button;
    dir_ = 0;  // no walk animation, no step-up
    int ax = in.axis(0), ay = in.axis(1);
    int len = isqrtRound(static_cast<uint32_t>(static_cast<int64_t>(ax) * ax + static_cast<int64_t>(ay) * ay));
    if (len < 8192) {
      ax = ((in.held(Button::Right) ? 1 : 0) - (in.held(Button::Left) ? 1 : 0)) * 32767;
      ay = ((in.held(Button::Down) ? 1 : 0) - (in.held(Button::Up) ? 1 : 0)) * 32767;
      len = isqrtRound(static_cast<uint32_t>(static_cast<int64_t>(ax) * ax + static_cast<int64_t>(ay) * ay));
    }
    int sx = 0, sy = 0;
    if (len > 0) {
      const int d = len > 32767 ? len : 32767;
      sx = ax * kDig / d;
      sy = ay * kDig / d;
    }
    if (sx != 0) face_ = sx > 0 ? 1 : -1;
    int wx = sx, wy = sy;
    if (rotate) {
      wx = (view_cos_ * sx + view_sin_ * sy + 8192) >> 14;
      wy = (view_cos_ * sy - view_sin_ * sx + 8192) >> 14;
    } else {
      toWorld(cam_.orient, wx, wy);
    }
    pge::Motion& m = *ent_.motion(player_);
    m.vx = static_cast<int16_t>(wx);
    m.vy = static_cast<int16_t>(wy);
    const int gx = (wx > 0) - (wx < 0), gy = (wy > 0) - (wy < 0);
    if (gx == 0 && gy == 0) return;
    const pge::Body& b = *ent_.body(player_);
    const int x0 = b.x + gx * 4, y0 = b.y + gy * 4;
    for (int ty = y0 >> 3; ty <= (y0 + b.h - 1) >> 3; ty++)
      for (int tx = x0 >> 3; tx <= (x0 + b.w - 1) >> 3; tx++) dig(tx, ty);
  }

  // Walks up a one-tile step: the surface near each diagonal is a staircase of 1-tile steps that would block the walk.
  void stepUp() {
    using namespace pge;
    const uint16_t fl = ent_.meta(player_)->flags;
    if (dir_ == 0 || !(fl & kEntOnGround) || !(fl & (vertical(gd_) ? kEntHitX : kEntHitY))) return;
    Body& b = *ent_.body(player_);
    const int ux = kUx[static_cast<int>(gd_)], uy = kUy[static_cast<int>(gd_)];
    int wx = dir_, wy = 0;
    toWorld(cam_.orient, wx, wy);
    Body t{b.x + ux * kTile, b.y + uy * kTile, b.w, b.h};
    if (!isFree(t)) return;
    t.x += wx;
    t.y += wy;
    if (!isFree(t)) return;
    b.x += ux * kTile;
    b.y += uy * kTile;
    jump(ux * kTile, uy * kTile);
    // the hit zeroed the walk speed: restart at full speed, or gravity pulls the body back down before it gets onto the step
    int sx = dir_ * kMaxRun, sy = 0;
    toWorld(cam_.orient, sx, sy);
    pge::Motion& m = *ent_.motion(player_);
    m = pge::Motion{static_cast<int16_t>(sx), static_cast<int16_t>(sy), 0, 0};
  }

  // Aim in the screen frame: stick axes 2 / 3, else the Down / Up buttons (Down wins), else in front. The aim point is half the
  // body plus 8 px from its centre; the zone is the 2 x 2 tiles around the tile corner nearest to it, so it sits just outside
  // the body. tx_ / ty_ = the zone's top-left tile. The 4-way camera turns the screen offset into the world by its quarter
  // turns; the rotating view turns it by the inverse of the view vector, so the zone stays on the character's screen axes.
  void aim(const pge::Input& in) {
    using pge::Button;
    const pge::Body& b = *ent_.body(player_);
    const int ax = stick(in.axis(2));
    int ay = stick(in.axis(3));
    if (ax == 0 && ay == 0) ay = in.held(Button::Down) ? 1 : in.held(Button::Up) ? -1 : 0;
    if (ax != 0) face_ = ax;
    int ox = (ax != 0 ? ax : ay == 0 ? face_ : 0) * (8 + 8), oy = ay * (ch_ / 2 + 8);  // from the centre, screen frame
    if (rotate) {
      const int wx = (view_cos_ * ox + view_sin_ * oy + 8192) >> 14;
      const int wy = (view_cos_ * oy - view_sin_ * ox + 8192) >> 14;
      ox = wx;
      oy = wy;
    } else {
      toWorld(cam_.orient, ox, oy);
    }
    zoneAround(b.x + b.w / 2 + ox, b.y + b.h / 2 + oy);
  }

  // The 2 x 2 zone around the tile corner nearest to the world point: corner = (point + 4) >> 3, tiles corner - 1 .. corner.
  void zoneAround(int px, int py) {
    tx_ = ((px + 4) >> 3) - 1;
    ty_ = ((py + 4) >> 3) - 1;
  }

  // Touch (logical screen pixel): digs the zone around the touched world point, same rule and messages as B.
  // shortcut: no reach limit, add one when the game needs it.
  void touchDig(int sx, int sy) {
    int wx, wy;
    screenToWorld(sx, sy, wx, wy);
    zoneAround(wx, wy);
    digZone();
  }

  // Select: 24 <-> 32 px tall; the feet side stays fixed, and only if the new box is free.
  void toggleChar() {
    using namespace pge;
    const int nh = ch_ == 24 ? 32 : 24;
    Body& b = *ent_.body(player_);
    Body nb = b;
    if (vertical(gd_)) {
      nb.h = nh;
      if (gd_ == GravityDir::Down) nb.y = b.y + b.h - nh;
    } else {
      nb.w = nh;
      if (gd_ == GravityDir::Right) nb.x = b.x + b.w - nh;
    }
    if (!isFree(nb)) {
      say(MsgNoRoom);
      return;
    }
    jump(nb.x + nb.w / 2 - (b.x + b.w / 2), nb.y + nb.h / 2 - (b.y + b.h / 2));
    b = nb;
    ch_ = static_cast<uint8_t>(nh);
    buildTilt();
  }

  // Scripted bot, deterministic: holds screen Right and jumps when it made no progress for 30 ticks.
  const pge::Input& botInput() {
    using namespace pge;
    const Body& b = *ent_.body(player_);
    const int cx = b.x + b.w / 2, cy = b.y + b.h / 2;
    int dx = cx - last_x_, dy = cy - last_y_;
    toScreen(cam_.orient, dx, dy);
    stuck_ = cam_.orient != bot_orient_ || dx > 0 ? 0 : stuck_ + 1;
    bot_orient_ = cam_.orient;
    last_x_ = cx;
    last_y_ = cy;
    const bool jump = stuck_ >= 30;
    if (jump) stuck_ = 0;
    pge_raw_input raw = {};
    raw.buttons = PGE_BTN_RIGHT | (jump ? PGE_BTN_UP : 0);
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
    if (s.flush_us > bench_.max_flush_us) bench_.max_flush_us = s.flush_us;
    if (s.frame_us > bench_.max_frame_us) bench_.max_frame_us = s.frame_us;
  }

  // Copies the tiles around the camera into small buffers for Canvas::tilemap (done in onTick, never in onRender).
  // cam_ may be negative: >> 3 floors, so the offset cam_ - win_ * kTile passed to tilemap stays in 0..7.
  // Rotating view: the window is centred on the view centre and is wide enough for every screen corner.
  // The breakable tiles of the aim zone get the lit id (kFgTiles + id), so aim() must run before this in a tick.
  void buildView() {
    vis_n_ = rotate ? kVis : kVisStd;
    win_x_ = (rotate ? view_x_ - vis_n_ * kTile / 2 : cam_.x) >> 3;
    win_y_ = (rotate ? view_y_ - vis_n_ * kTile / 2 : cam_.y) >> 3;
    for (int y = 0; y < vis_n_; y++)
      for (int x = 0; x < vis_n_; x++) {
        const uint8_t f = world_.get(win_x_ + x, win_y_ + y, pge::Layer::Fg);
        const uint8_t w = world_.get(win_x_ + x, win_y_ + y, pge::Layer::Wall);
        vis_fg_[y * vis_n_ + x] = f < kFgTiles ? f : 0;
        vis_wall_[y * vis_n_ + x] = w < kWallTiles ? w : 0;
      }
    for (int i = 0; i < 4; i++) {
      const int tx = tx_ + (i & 1), ty = ty_ + (i >> 1), x = tx - win_x_, y = ty - win_y_;
      if (x >= 0 && x < vis_n_ && y >= 0 && y < vis_n_ &&
          (world_.props(world_.get(tx, ty, pge::Layer::Fg)).flags & pge::kTileBreakable))
        vis_fg_[y * vis_n_ + x] += kFgTiles;
    }
  }

  // Placeholder character, 16 x h, facing right: helmet, visor, suit, jetpack box on the back, legs, boots; 2 walk frames.
  static void drawChar(uint8_t* px, int h) {
    for (int f = 0; f < 2; f++) {
      uint8_t* p = px + f * 16 * h;
      auto fill = [p](int x0, int y0, int x1, int y1, uint8_t c) {
        for (int y = y0; y <= y1; y++)
          for (int x = x0; x <= x1; x++) p[y * 16 + x] = c;
      };
      fill(4, 0, 11, 7, 1);
      fill(8, 2, 11, 4, 2);
      fill(3, 8, 12, h - 9, 3);
      fill(0, 9, 2, h - 12, 4);
      const int l = f ? 3 : 4, r = f ? 9 : 8;  // legs apart in frame 1
      fill(l, h - 8, l + 3, h - 3, 5);
      fill(r, h - 8, r + 3, h - 3, 5);
      fill(l, h - 2, l + 3, h - 1, 6);
      fill(r, h - 2, r + 3, h - 1, 6);
    }
  }

  void makeArt() {
    using pge::rgb;
    static constexpr uint8_t fp[12][3] = {{0, 0, 0},       {70, 190, 60},   {40, 130, 40},  {150, 100, 50},
                                          {110, 70, 30},   {130, 130, 140}, {100, 100, 112}, {240, 200, 40},
                                          {80, 230, 230},  {90, 20, 20},    {170, 45, 20},   {255, 150, 40}};
    // 9..11: the core, dark red to glow. 12..23: the same colours 45 % of the way to white (entry 12, the lit copy of
    // "transparent", is never drawn).
    auto lit = [](int c) { return static_cast<uint8_t>(c + (255 - c) * 9 / 20); };
    for (int i = 0; i < 12; i++) {
      fg_pal_[i] = rgb(fp[i][0], fp[i][1], fp[i][2]);
      fg_pal_[12 + i] = i ? rgb(lit(fp[i][0]), lit(fp[i][1]), lit(fp[i][2])) : 0;
    }
    const pge::Color wp[] = {0, rgb(80, 55, 30), rgb(60, 40, 22), rgb(60, 60, 72), rgb(46, 46, 58)};
    for (int i = 0; i < 5; i++) wall_pal_[i] = wp[i];
    const pge::Color pp[] = {0, kHeadColor, rgb(120, 220, 255), rgb(225, 225, 235), rgb(90, 95, 105), rgb(40, 60, 170), kBootColor};
    for (int i = 0; i < 7; i++) player_pal_[i] = pp[i];
    for (int y = 0; y < kTile; y++)
      for (int x = 0; x < kTile; x++) {
        const int o = y * kTile + x;
        const bool speck = (x == 2 && y == 2) || (x == 5 && y == 5) || (x == 6 && y == 1) || (x == 1 && y == 6);
        const bool glow = (x == 3 || x == 4) && (y == 3 || y == 4);  // the core: a glowing 2 x 2 in a magma tile
        for (int t = 1; t < kFgTiles; t++) {
          const int h = (x * 5 + y * 3 + t * 7) % 11;
          fg_px_[t * 64 + o] = t == pge::kGenGrass ? (h % 3 ? 1 : 2)  // no blade side: the rim faces every direction
                             : t == pge::kGenDirt  ? (h % 4 ? 3 : 4)
                             : t == pge::kGenCore  ? (glow ? 11 : h < 4 ? 10 : 9)
                           : (t >= pge::kGenOre && speck) ? (t == pge::kGenOre ? 7 : 8) : (h < 3 ? 6 : 5);
        }
        const int h = (x * 3 + y * 5) % 11;
        wall_px_[64 + o] = h ? 1 : 2;
        wall_px_[128 + o] = h < 2 ? 4 : 3;
      }
    // fg tile ids kFgTiles + t are the lit copies of tile t, used for the aim zone (palette entries 12..23, 0 stays transparent)
    for (int t = 1; t < kFgTiles; t++)
      for (int o = 0; o < 64; o++) {
        const uint8_t p = fg_px_[t * 64 + o];
        fg_px_[(kFgTiles + t) * 64 + o] = p ? static_cast<uint8_t>(p + 12) : 0;
      }
    drawChar(char24_px_, 24);
    drawChar(char32_px_, 32);
  }

  static char* put(char* o, const char* s) {
    while (*s) *o++ = *s++;
    return o;
  }
  static char* putU(char* o, uint32_t v) { return o + pge::ui::formatU32(o, 11, v); }
  void format() {
    char* o = put(hud_, "FPS ");
    o = putU(o, eng_ ? eng_->stats().fps : 0);
    o = put(o, "  GRAV ");
    *o++ = "DLUR"[static_cast<int>(gd_) & 3];
    o = put(o, "  CHAR ");
    o = putU(o, ch_);
    o = put(o, "\nEDITS ");
    o = putU(o, world_.changeCount());
    o = put(o, "/");
    o = putU(o, world_.changeCapacity());
    o = put(o, "\n");
    o = put(o, message());
    *o = 0;
  }

  pge::Engine* eng_ = nullptr;
  pge::TileProps props_[256] = {};
  pge::World world_;
  pge::Entities ent_;
  pge::Storage storage_;
  pge::EntityId player_ = pge::kNoEntity;
  pge::GravityDir gd_ = pge::GravityDir::Down;
  pge::Camera cam_{0, 0, PGE_LOGICAL_W, PGE_LOGICAL_H, kWorldPx, kWorldPx};
  uint8_t fg_px_[2 * kFgTiles * 64] = {}, wall_px_[kWallTiles * 64] = {};
  uint8_t char24_px_[2 * 16 * 24] = {}, char32_px_[2 * 16 * 32] = {};
  pge::Color fg_pal_[24] = {}, wall_pal_[5] = {}, player_pal_[7] = {};
  pge::Tileset fg_set_{fg_px_, fg_pal_, kTile, kTile, true};
  pge::Tileset wall_set_{wall_px_, wall_pal_, kTile, kTile, true};
  pge::SpriteSheet sheet24_{char24_px_, player_pal_, 16, 24, 2, true};
  pge::SpriteSheet sheet32_{char32_px_, player_pal_, 16, 32, 2, true};
  uint8_t tilt_px_[kTiltBytes] = {};  // 23 KB: the one RAM cost of the tilt
  pge::SpriteSheet tilt_sheet_{tilt_px_, player_pal_, 0, 0, 0, true};
  uint8_t tilt_ox_[kTiltN] = {}, tilt_oy_[kTiltN] = {};  // pivot (feet midpoint) in the canvas of each tilt
  int tilt_i_ = 0;
  pge::Input idle_, bot_in_;
  Bench bench_ = {};
  uint8_t vis_fg_[kVis * kVis] = {}, vis_wall_[kVis * kVis] = {};
  uint16_t vis_n_ = kVisStd;  // tiles per side of the live window
  int win_x_ = 0, win_y_ = 0, tx_ = 0, ty_ = 0, face_ = 1, dir_ = 0, turn_ = 0;
  // rotating view: Q14 vector and its target vector, view centre (world px), its eased distance from the planet centre
  // (1/16 px), reference point (body centre + jump offset)
  int view_cos_ = 16384, view_sin_ = 0, view_x_ = 0, view_y_ = 0, view_r_ = 0, tgt_cos_ = 16384, tgt_sin_ = 0;
  int ref_x_ = 0, ref_y_ = 0, off_x_ = 0, off_y_ = 0;
  int stuck_ = 0, last_x_ = 0, last_y_ = 0;
  uint8_t bot_orient_ = 0;
  bool walk_ = false, dig_ = false, storage_ok_ = false;  // dig_: A held, dig mode
  uint8_t ch_ = 32;  // live character height: char_h at onCreate, then Select / load
  Msg msg_ = MsgNone;
  uint16_t msg_t_ = 0;
  uint32_t ticks_ = 0, switches_ = 0;
  char hud_[96] = {};
};
static_assert(PlanetGame::tiltSide(32) == PlanetGame::kTiltSideMax && PlanetGame::tiltSide(24) <= PlanetGame::kTiltSideMax,
              "kTiltSideMax is the canvas side of the largest character");

