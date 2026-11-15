// Side-scroller demo game: two parallax tile layers (far clouds at 1/2 x, 1/4 y speed; near ground and platforms
// at 1:1) over a sky, a camera clamped to the world, and a HUD. No platform includes, so a firmware main can
// reuse it. The object must stay in place after onCreate (the layers point into it), so keep it static.
#pragma once
#include <stdint.h>
#include "pge/camera.h"
#include "pge/color.h"
#include "pge/engine.h"

class ScrollerGame {
 public:
  uint32_t quit_after = 0;  // request quit after this many ticks; 0 = run until the platform quits

  pge::Error onCreate(pge::Engine& e) {
    eng_ = &e;
    makeTiles();
    makeMaps();
    layers_[0] = pge::MapLayer{{far_map_, kMapW, kMapH, &tiles_}, 128, 64};
    layers_[1] = pge::MapLayer{{near_map_, kMapW, kMapH, &tiles_}};
    cam_.moveTo(0, 0);
    format();
    return pge::Ok;
  }

  void onTick(const pge::Input& in) {
    using pge::Button;
    const int step = in.held(Button::A) ? 4 : 2;
    int dx = (in.held(Button::Right) ? step : 0) - (in.held(Button::Left) ? step : 0);
    int dy = (in.held(Button::Down) ? step : 0) - (in.held(Button::Up) ? step : 0);
    if (!(in.held(Button::Up) || in.held(Button::Down) || in.held(Button::Left) || in.held(Button::Right))) {
      dx += in.axis(0) * 3 / 32768;  // stick: up to 2 px per tick, only when no d-pad button is held (the host fills both)
      dy += in.axis(1) * 3 / 32768;
    }
    cam_.moveTo(cam_.x + dx, cam_.y + dy);
    touching_ = in.touching();
    if (touching_) {
      tx_ = in.touchX();
      ty_ = in.touchY();
      cam_.centerOn(cam_.x + tx_, cam_.y + ty_);  // screen point -> world point, then pull it to the centre
    }
    ticks_++;
    format();
    if (quit_after != 0 && ticks_ >= quit_after) eng_->requestQuit();
  }

  // Runs once per band: everything drawn is a pure function of the state set in onCreate / onTick.
  void onRender(pge::Canvas& c) {
    using namespace pge;
    c.clear(rgb(92, 148, 252));
    drawLayers(c, cam_, layers_, 2);
    c.text(kFont5x7, 3, 3, hud_[0], rgb(0, 0, 0));
    c.text(kFont5x7, 2, 2, hud_[0], rgb(255, 255, 255));
    if (touching_) {
      c.text(kFont5x7, 3, 12, hud_[1], rgb(0, 0, 0));
      c.text(kFont5x7, 2, 11, hud_[1], rgb(255, 255, 255));
      c.line(tx_ - 5, ty_, tx_ + 5, ty_, rgb(255, 255, 0));
      c.line(tx_, ty_ - 5, tx_, ty_ + 5, rgb(255, 255, 0));
    }
  }

 private:
  static constexpr int kMapW = 64, kMapH = 32, kTile = 8;
  enum { kSky = 0, kGround = 1, kBrick = 2, kCloud = 3 };  // tile ids; 0 is transparent in both layers

  // 4 tiles of 8x8 pixels; palette index 0 is the transparent one.
  void makeTiles() {
    pal_[1] = pge::rgb(60, 180, 60);    // grass
    pal_[2] = pge::rgb(150, 100, 50);   // dirt
    pal_[3] = pge::rgb(110, 70, 30);    // speckle
    pal_[4] = pge::rgb(200, 200, 190);  // mortar
    pal_[5] = pge::rgb(190, 70, 50);    // brick
    pal_[6] = pge::rgb(255, 255, 255);  // cloud
    pal_[7] = pge::rgb(200, 215, 240);  // cloud rim
    for (int y = 0; y < 8; y++)
      for (int x = 0; x < 8; x++) {
        px_[kGround * 64 + y * 8 + x] = y < 2 ? 1 : y == 2 ? (x & 1 ? 2 : 1) : ((x * 3 + y * 5) % 7 == 0 ? 3 : 2);
        const bool mortar = y == 0 || y == 4 || (y < 4 ? x == 0 : x == 4);
        px_[kBrick * 64 + y * 8 + x] = mortar ? 4 : 5;
        const int u = 2 * x - 7, v = 2 * y - 9, d = u * u * 81 + v * v * 49;  // ellipse puff
        px_[kCloud * 64 + y * 8 + x] = d > 49 * 81 ? 0 : d > 49 * 81 * 7 / 10 ? 7 : 6;
      }
  }

  void makeMaps() {
    for (int x = 0; x < kMapW; x++) {
      near_map_[26 * kMapW + x] = kGround;
      for (int y = 27; y < kMapH; y++) near_map_[y * kMapW + x] = kBrick;
    }
    for (int k = 0; k < 12; k++) {  // floating brick platforms
      const int x0 = 3 + k * 5 + (k * 3) % 4, row = 12 + (k * 5) % 11, len = 3 + k % 3;
      for (int i = 0; i < len && x0 + i < kMapW; i++) near_map_[row * kMapW + x0 + i] = kBrick;
    }
    for (int k = 0; k < 10; k++) {  // clouds
      const int x0 = k * 7 + (k * 5) % 3, row = 2 + (k * 3) % 9, len = 2 + k % 3;
      for (int i = 0; i < len && x0 + i < kMapW; i++) far_map_[row * kMapW + x0 + i] = kCloud;
    }
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
    char* o = putStr(hud_[0], "CAM ");
    o = putInt(o, cam_.x);
    o = putStr(o, ",");
    o = putInt(o, cam_.y);
    o = putStr(o, "  TICK ");
    o = putInt(o, static_cast<int>(ticks_));
    *o = 0;
    o = putStr(hud_[1], "TOUCH ");
    o = putInt(o, tx_);
    o = putStr(o, ",");
    o = putInt(o, ty_);
    *o = 0;
  }

  pge::Engine* eng_ = nullptr;
  uint8_t px_[4 * 64] = {};
  pge::Color pal_[8] = {};
  pge::Tileset tiles_{px_, pal_, kTile, kTile, true};
  uint8_t far_map_[kMapW * kMapH] = {};
  uint8_t near_map_[kMapW * kMapH] = {};
  pge::MapLayer layers_[2] = {};
  pge::Camera cam_{0, 0, PGE_LOGICAL_W, PGE_LOGICAL_H, kMapW * kTile, kMapH * kTile};
  char hud_[2][40] = {};
  uint32_t ticks_ = 0;
  int tx_ = 0, ty_ = 0;
  bool touching_ = false;
};
