// Coin Catcher, step 1: the smallest game. The engine calls onCreate once, onTick 60 times a second and onRender
// to draw a frame. This one only paints the sky and the ground.
#pragma once
#include "pge/color.h"
#include "pge/engine.h"

class Game {
 public:
  pge::Error onCreate(pge::Engine&) { return pge::Ok; }

  void onTick(const pge::Input&) {}

  // The engine draws the screen in horizontal bands and calls onRender once per band, so onRender only draws;
  // all game state changes happen in onTick.
  void onRender(pge::Canvas& c) {
    c.clear(pge::rgb(92, 148, 252));
    c.fillRect({0, kGroundY, c.width(), c.height() - kGroundY}, pge::rgb(60, 160, 60));
  }

 private:
  static constexpr int kGroundY = 220;  // top row of the grass
};
