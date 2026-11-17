// Coin Catcher, step 3: a coin. Touching it collects it, and a new one appears at a pseudo-random place.
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

// Axis-aligned boxes overlap when they overlap on both axes; touching edges do not count.
inline bool overlap(pge::Rect a, pge::Rect b) {
  return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

class Game {
 public:
  pge::Error onCreate(pge::Engine&) {
    x_ = (PGE_LOGICAL_W - kSize) / 2;
    y_ = (kGroundY - kSize) / 2;
    coin_x_ = 240;  // the first coin sits right of the player; later ones are random
    coin_y_ = y_;
    return pge::Ok;
  }

  void onTick(const pge::Input& in) {
    using pge::Button;
    const int dx = in.held(Button::Right) - in.held(Button::Left);
    const int dy = in.held(Button::Down) - in.held(Button::Up);
    if (dx != 0) left_ = dx < 0;
    x_ = std::clamp(x_ + dx * kSpeed, 0, PGE_LOGICAL_W - kSize);
    y_ = std::clamp(y_ + dy * kSpeed, 0, kGroundY - kSize);

    if (overlap({x_, y_, kSize, kSize}, {coin_x_, coin_y_, kCoin, kCoin})) {
      coin_x_ = random(PGE_LOGICAL_W - kCoin);
      coin_y_ = random(kGroundY - kCoin);
    }
  }

  void onRender(pge::Canvas& c) {
    using pge::rgb;
    c.clear(rgb(92, 148, 252));
    c.fillRect({0, kGroundY, c.width(), c.height() - kGroundY}, rgb(60, 160, 60));
    const int r = kCoin / 2, cx = coin_x_ + r, cy = coin_y_ + r;
    c.fillCircle(cx, cy, r, rgb(255, 210, 0));
    c.circle(cx, cy, r, rgb(180, 120, 0));
    c.line(cx, cy - 2, cx, cy + 2, rgb(255, 250, 200));
    c.sprite(kPlayer, 0, x_, y_, left_ ? pge::FlipX : pge::None);
  }

 private:
  static constexpr int kGroundY = 220, kSize = 12, kSpeed = 2, kCoin = 11;

  // 0 .. n-1 from a linear congruential generator: same sequence every run, which keeps --auto repeatable.
  int random(int n) {
    rng_ = rng_ * 1664525u + 1013904223u;
    return static_cast<int>((rng_ >> 16) % static_cast<uint32_t>(n));
  }

  int x_ = 0, y_ = 0, coin_x_ = 0, coin_y_ = 0;
  bool left_ = false;
  uint32_t rng_ = 12345;
};
