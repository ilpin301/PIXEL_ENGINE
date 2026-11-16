// Platformer demo game: tile collision (pge::moveAndCollide) with game-side 8.8 fixed-point physics, coyote time,
// jump buffer, variable jump height, one-way platforms, spikes, a scripted autoplay bot for benchmarks and tests.
// No platform includes, so a firmware main can reuse it. The object must stay in place after onCreate (the
// layers and the grid point into it), so keep it static.
#pragma once
#include <stdint.h>
#include "pge/camera.h"
#include "pge/collision.h"
#include "pge/color.h"
#include "pge/debug.h"
#include "pge/engine.h"
#include "pge/sprite.h"
#include "pge/ui.h"

class PlatformerGame {
 public:
  struct Bench {  // sums of Engine::stats() over the autoplay ticks after the 60-tick warm-up
    uint32_t n, sum_tick_us, sum_band_us, sum_flush_us, sum_frame_us, max_tick_us, max_band_us, max_frame_us;
  };

  uint32_t quit_after = 0;  // request quit after this many ticks; 0 = run until the platform quits
  bool autoplay = false;    // ignore the Input and run the scripted bot
  bool overlay = false;     // debug overlay; Select toggles it (state changes in onTick only)

  pge::Error onCreate(pge::Engine& e) {
    eng_ = &e;
    props_[kGrass].flags = props_[kDirt].flags = pge::kTileSolid;
    props_[kWood].flags = pge::kTileOneWay;
    props_[kSpike].flags = pge::kTileHazard;
    makeArt();
    makeLevel();
    layers_[0] = pge::MapLayer{{far_map_, kFarW, kFarH, &far_set_}, 128, 64};
    layers_[1] = pge::MapLayer{{map_, kW, kH, &near_set_}};
    bench_ = Bench{};
    deaths_ = ticks_ = 0;
    respawn();
    cam_.centerOn(body_.x, body_.y);
    format();
    return pge::Ok;
  }

  void onTick(const pge::Input& in) {
    if (in.pressed(pge::Button::Select)) overlay = !overlay;
    step(autoplay ? bot() : manual(in));
    cam_.centerOn(body_.x + body_.w / 2, body_.y + body_.h / 2);
    frame_ = !ground_ ? 3 : (vx_ > 32 || vx_ < -32) ? pge::animFrame(kWalk, ticks_) : 0;
    ticks_++;
    if (autoplay && eng_ && ticks_ > 60) sample();
    format();
    if (quit_after != 0 && ticks_ >= quit_after && eng_) eng_->requestQuit();
  }

  // Runs once per band: everything drawn is a pure function of the state set in onCreate / onTick.
  void onRender(pge::Canvas& c) {
    using namespace pge;
    c.clear(rgb(92, 148, 252));
    drawLayers(c, cam_, layers_, 2);
    c.sprite(sheet_, frame_, body_.x - cam_.x, body_.y - cam_.y, face_ < 0 ? FlipX : pge::None);
    c.text(kFont5x7, 3, 3, hud_, rgb(0, 0, 0));
    c.text(kFont5x7, 2, 2, hud_, rgb(255, 255, 255));
    const ui::Style st{&kFont5x7, 0xFFFF, 0x0000, 0xFFE0, 1};
    ui::label(c, st, 2, c.height() - 10, tick_);
    if (overlay && eng_) drawOverlay(c, st, eng_->stats(), kOverlayAll);
  }

  const Bench& bench() const { return bench_; }
  const pge::Body& body() const { return body_; }
  const pge::TileGrid& grid() const { return grid_; }
  uint32_t deaths() const { return deaths_; }
  uint32_t ticks() const { return ticks_; }
  int cameraX() const { return cam_.x; }

 private:
  static constexpr int kTile = 16, kW = 80, kH = 30, kGround = 24;  // kGround: row of the flat grass line
  static constexpr int kFarW = 56, kFarH = 20, kWorldW = kW * kTile, kWorldH = kH * kTile;
  static constexpr int kSpawnX = 32, kSpawnY = kGround * kTile - 14;
  enum { kAir = 0, kGrass = 1, kDirt = 2, kWood = 3, kSpike = 4 };  // tile ids
  // 8.8 fixed point: 256 = 1 px (per tick for velocities)
  static constexpr int kGrav = 64, kMaxFall = 6 * 256, kJump = 5 * 256, kMaxRun = 2 * 256, kAccel = 64;
  static constexpr int kCoyote = 5, kJumpBuf = 5;
  static constexpr pge::Animation kWalk = {1, 2, 6, true};
  struct Ctl { int dir; bool press, held, release; };  // what the player (or bot) asks for this tick
  struct Hill { uint8_t x0, x1, top; };                // columns x0..x1 start at tile row `top`
  struct Span { uint8_t x0, len, row; };               // spikes / one-way planks
  static constexpr Hill kHills[] = {{10, 12, 23}, {13, 15, 22}, {16, 18, 23}, {44, 47, 21}, {64, 66, 23}, {67, 68, 22}, {69, 71, 23}};
  static constexpr Span kSpikes[] = {{26, 2, 23}, {52, 1, 23}, {76, 2, 23}};
  static constexpr Span kPlanks[] = {{2, 3, 21}, {32, 5, 22}, {57, 4, 21}};

  void makeArt() {
    using pge::rgb;
    const pge::Color np[] = {0, rgb(70, 190, 60), rgb(40, 130, 40), rgb(150, 100, 50), rgb(110, 70, 30),
                             rgb(190, 140, 70), rgb(120, 80, 30), rgb(170, 170, 180), rgb(110, 110, 130)};
    for (int i = 0; i < 9; i++) near_pal_[i] = np[i];
    far_pal_[1] = rgb(255, 255, 255);
    far_pal_[2] = rgb(205, 220, 245);
    const pge::Color pp[] = {0, rgb(255, 205, 150), rgb(210, 50, 50), rgb(40, 50, 130), rgb(0, 0, 0)};
    for (int i = 0; i < 5; i++) pl_pal_[i] = pp[i];
    for (int y = 0; y < kTile; y++)
      for (int x = 0; x < kTile; x++) {
        const int o = y * kTile + x, dirt = (x * 5 + y * 3) % 11 == 0 ? 4 : 3;
        near_px_[kGrass * 256 + o] = y < 2 ? 1 : y < 4 ? ((x * 7 + y) % 3 ? 2 : 1) : dirt;
        near_px_[kDirt * 256 + o] = dirt;
        near_px_[kWood * 256 + o] = y > 5 ? 0 : (y == 0 || y == 5 || x % 8 == 0) ? 6 : 5;
        const int d = 2 * (x % 8) - 7;
        near_px_[kSpike * 256 + o] = (d < 0 ? -d : d) <= 1 + y * 6 / 15 ? (x % 8 < 4 ? 7 : 8) : 0;
        const int u = 2 * x - 15, v = 2 * y - 17, e = u * u * 196 + v * v * 225;  // cloud puff: ellipse
        far_px_[256 + o] = e > 44100 ? 0 : e > 30870 ? 2 : 1;
      }
    // 12x14 player: head, shirt, legs per frame (0 idle, 1 walk A, 2 walk B, 3 jump with raised arms)
    const int legs[4][2][2] = {{{3, 5}, {6, 8}}, {{3, 5}, {7, 9}}, {{4, 7}, {4, 7}}, {{2, 5}, {6, 9}}};
    for (int f = 0; f < 4; f++) {
      uint8_t* p = pl_px_ + f * 168;
      auto fill = [p](int x0, int y0, int x1, int y1, uint8_t c) {
        for (int y = y0; y <= y1; y++)
          for (int x = x0; x <= x1; x++) p[y * 12 + x] = c;
      };
      fill(3, 0, 8, 4, 1);
      fill(7, 2, 8, 3, 4);
      fill(2, 5, 9, 9, 2);
      for (const auto& l : legs[f]) fill(l[0], 10, l[1], f == 3 ? 12 : 13, 3);
      if (f == 3) { fill(0, 3, 1, 6, 2); fill(10, 3, 11, 6, 2); }
    }
  }

  void makeLevel() {
    uint8_t top[kW];
    for (int x = 0; x < kW; x++) top[x] = kGround;
    for (const Hill& h : kHills)
      for (int x = h.x0; x <= h.x1; x++) top[x] = h.top;
    for (int x = 0; x < kW; x++)
      for (int y = top[x]; y < kH; y++) map_[y * kW + x] = y == top[x] ? kGrass : kDirt;
    for (const Span& s : kSpikes)
      for (int i = 0; i < s.len; i++) map_[s.row * kW + s.x0 + i] = kSpike;
    for (const Span& s : kPlanks)
      for (int i = 0; i < s.len; i++) map_[s.row * kW + s.x0 + i] = kWood;
    for (int k = 0; k < 10; k++)  // clouds, far layer
      for (int i = 0, x0 = k * 5 + (k * 3) % 4, row = 2 + (k * 5) % 8; i < 2 + k % 3 && x0 + i < kFarW; i++)
        far_map_[row * kFarW + x0 + i] = 1;
  }

  void respawn() {
    body_ = pge::Body{kSpawnX, kSpawnY, 12, 14};
    vx_ = vy_ = rx_ = ry_ = 0;
    ground_ = false;
  }

  Ctl manual(const pge::Input& in) const {
    using pge::Button;
    int dir = (in.held(Button::Right) ? 1 : 0) - (in.held(Button::Left) ? 1 : 0);
    if (dir == 0 && (in.axis(0) > 8192 || in.axis(0) < -8192)) dir = in.axis(0) > 0 ? 1 : -1;  // ~25% deadzone
    return Ctl{dir, in.pressed(Button::A) || in.pressed(Button::Up), in.held(Button::A) || in.held(Button::Up),
               in.released(Button::A) || in.released(Button::Up)};
  }

  // Hazard tile within `dist` px in front of the body, at foot height.
  bool hazardAhead(int dist) const {
    const int fx = bot_dir_ > 0 ? body_.x + body_.w : body_.x - 1, ty = (body_.y + body_.h - 1) >> 4;  // kTile == 16
    for (int d = 0; d <= dist; d += 8)
      if (pge::tileFlags(grid_, (fx + bot_dir_ * d) >> 4, ty) & pge::kTileHazard) return true;
    return false;
  }

  // Walks to one world end and back; jumps when blocked, stalled, before spikes and on a timer. Deterministic.
  Ctl bot() {
    stuck_ = body_.x == last_x_ ? stuck_ + 1 : 0;
    last_x_ = body_.x;
    if (body_.x + body_.w >= kWorldW - 24) bot_dir_ = -1;
    else if (body_.x <= 24) bot_dir_ = 1;
    if (stuck_ > 60) { bot_dir_ = -bot_dir_; stuck_ = 0; }
    const bool jump = ground_ && (hit_x_ || stuck_ >= 8 || hazardAhead(24) || (ticks_ % 90 == 0 && !hazardAhead(96)));
    if (jump) hold_ = 22;  // full-height jump
    const Ctl c{bot_dir_, jump, hold_ > 0, hold_ == 1};
    if (hold_ > 0) hold_--;
    return c;
  }

  void step(const Ctl& c) {
    using namespace pge;
    if (c.dir) face_ = c.dir;
    const int tgt = c.dir * kMaxRun;  // accel and friction share one rate
    vx_ = vx_ < tgt ? (vx_ + kAccel > tgt ? tgt : vx_ + kAccel) : (vx_ - kAccel < tgt ? tgt : vx_ - kAccel);
    coyote_ = ground_ ? kCoyote : coyote_ > 0 ? coyote_ - 1 : 0;
    jbuf_ = c.press ? kJumpBuf : jbuf_ > 0 ? jbuf_ - 1 : 0;
    if (jbuf_ && coyote_) { vy_ = -kJump; jbuf_ = coyote_ = 0; }
    else if (c.release && vy_ < 0) vy_ /= 2;  // let go early: cut the rise
    vy_ = vy_ + kGrav > kMaxFall ? kMaxFall : vy_ + kGrav;
    const int ax = rx_ + vx_, ay = ry_ + vy_, dx = ax >> 8, dy = ay >> 8;  // floor; remainders stay in 0..255
    rx_ = ax - dx * 256;
    ry_ = ay - dy * 256;
    const MoveResult r = moveAndCollide(grid_, body_, dx, dy);
    if (r.hit_x) vx_ = rx_ = 0;
    if (r.hit_y) vy_ = ry_ = 0;
    ground_ = r.on_ground;
    hit_x_ = r.hit_x;
    if (ground_ && vy_ > 0) vy_ = ry_ = 0;
    if (r.touched_flags & kTileHazard) { respawn(); deaths_++; }
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

  static char* putStr(char* o, const char* s) {
    while (*s) *o++ = *s++;
    return o;
  }
  static char* putInt(char* o, int v) {
    char t[12];
    int n = 0;
    uint32_t u = v < 0 ? 0u - static_cast<uint32_t>(v) : static_cast<uint32_t>(v);
    do { t[n++] = static_cast<char>('0' + u % 10); u /= 10; } while (u);
    if (v < 0) *o++ = '-';
    while (n) *o++ = t[--n];
    return o;
  }
  void format() {
    char* o = putStr(hud_, "FPS ");
    o = putInt(o, eng_ ? eng_->stats().fps : 0);
    o = putStr(o, "  X ");
    o = putInt(o, body_.x);
    o = putStr(o, " px  DEATHS ");
    o = putInt(o, static_cast<int>(deaths_));
    *o = 0;
    pge::ui::formatU32(tick_ + 2, sizeof tick_ - 2, ticks_);
  }

  pge::Engine* eng_ = nullptr;
  uint8_t near_px_[5 * 256] = {};
  uint8_t far_px_[2 * 256] = {};
  uint8_t pl_px_[4 * 168] = {};
  pge::Color near_pal_[9] = {}, far_pal_[3] = {}, pl_pal_[5] = {};
  pge::Tileset near_set_{near_px_, near_pal_, kTile, kTile, true};
  pge::Tileset far_set_{far_px_, far_pal_, kTile, kTile, true};
  pge::SpriteSheet sheet_{pl_px_, pl_pal_, 12, 14, 4, true};
  uint8_t map_[kW * kH] = {};
  uint8_t far_map_[kFarW * kFarH] = {};
  pge::TileProps props_[256] = {};
  pge::TileGrid grid_{map_, kW, kH, kTile, kTile, props_};
  pge::MapLayer layers_[2] = {};
  pge::Camera cam_{0, 0, PGE_LOGICAL_W, PGE_LOGICAL_H, kWorldW, kWorldH};
  pge::Body body_ = {kSpawnX, kSpawnY, 12, 14};
  int vx_ = 0, vy_ = 0, rx_ = 0, ry_ = 0;  // 8.8 velocity and sub-pixel position remainder
  int coyote_ = 0, jbuf_ = 0, face_ = 1, frame_ = 0;
  bool ground_ = false, hit_x_ = false;
  int bot_dir_ = 1, stuck_ = 0, last_x_ = 0, hold_ = 0;
  uint32_t ticks_ = 0, deaths_ = 0;
  Bench bench_ = {};
  char hud_[48] = {};
  char tick_[16] = {'T', ' ', '0'};
};
