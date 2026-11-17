// Coin Catcher, step 2: a player sprite that the d-pad moves around, kept inside the play field.
#pragma once
#include <stdint.h>
#include <algorithm>
#include "pge/color.h"
#include "pge/engine.h"

// 12x12 player, one palette index per pixel; index 0 is transparent. The eyes look right.
constexpr uint8_t kPlayerPx[] = {
    0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0,
    0, 0, 1, 2, 2, 2, 2, 2, 2, 1, 0, 0,
    0, 1, 2, 2, 2, 2, 2, 2, 2, 2, 1, 0,
    0, 1, 2, 3, 3, 2, 2, 3, 3, 2, 1, 0,
    0, 1, 2, 3, 1, 2, 2, 3, 1, 2, 1, 0,
    0, 1, 2, 2, 2, 2, 2, 2, 2, 2, 1, 0,
    0, 1, 2, 2, 1, 1, 1, 1, 2, 2, 1, 0,
    0, 0, 1, 2, 2, 2, 2, 2, 2, 1, 0, 0,
    0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0,
    0, 0, 1, 4, 4, 1, 1, 4, 4, 1, 0, 0,
    0, 1, 4, 4, 4, 1, 1, 4, 4, 4, 1, 0,
    0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0,
};
constexpr pge::Color kPlayerPal[] = {0, pge::rgb(20, 20, 40), pge::rgb(240, 120, 40), pge::rgb(255, 255, 255),
                                     pge::rgb(60, 60, 200)};
constexpr pge::SpriteSheet kPlayer{kPlayerPx, kPlayerPal, 12, 12, 1, true};

class Game {
 public:
  pge::Error onCreate(pge::Engine&) {
    x_ = (PGE_LOGICAL_W - kSize) / 2;
    y_ = (kGroundY - kSize) / 2;
    return pge::Ok;
  }

  void onTick(const pge::Input& in) {
    using pge::Button;
    const int dx = in.held(Button::Right) - in.held(Button::Left);
    const int dy = in.held(Button::Down) - in.held(Button::Up);
    if (dx != 0) left_ = dx < 0;
    x_ = std::clamp(x_ + dx * kSpeed, 0, PGE_LOGICAL_W - kSize);
    y_ = std::clamp(y_ + dy * kSpeed, 0, kGroundY - kSize);
  }

  void onRender(pge::Canvas& c) {
    c.clear(pge::rgb(92, 148, 252));
    c.fillRect({0, kGroundY, c.width(), c.height() - kGroundY}, pge::rgb(60, 160, 60));
    c.sprite(kPlayer, 0, x_, y_, left_ ? pge::FlipX : pge::None);
  }

 private:
  static constexpr int kGroundY = 220, kSize = 12, kSpeed = 2;  // kSpeed: pixels per tick
  int x_ = 0, y_ = 0;
  bool left_ = false;  // facing left: draw the sprite mirrored
};
