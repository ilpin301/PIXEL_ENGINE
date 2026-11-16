#pragma once
// Pooled particles: compact 8 B records, no collision, no handles. Deviates from the S5.2 plan wording
// 'pooled entity kind': the 8 B record of 03 section 3.2 keeps them cheap.
#include <stdint.h>
#include "pge/config.h"
#include "pge/gfx.h"
#include "pge/memory.h"

namespace pge {

// Positions in world pixels, velocities in integer px/tick.
struct Particle {
  int16_t x, y;
  int8_t vx, vy;
  uint8_t life;   // update() calls left
  uint8_t color;  // palette index
};
static_assert(sizeof(Particle) == 8, "Particle must stay 8 B");

class Particles {
 public:
  // Clamps x, y to int16 and vx, vy to int8. False when the pool is full (counters().full + 1) or life == 0.
  bool spawn(int x, int y, int vx, int vy, uint8_t life, uint8_t color);
  // Gravity acts before the position step: every update with life % gravity_period == 0 adds 1 to vy (max 127).
  // Positions saturate at the int16 range. A particle is freed by the update that takes its life to 0.
  void update(uint8_t gravity_period = 0);
  // One pixel per live particle; color >= palette_size falls back to palette[0]. Does not change state.
  void draw(Canvas& c, int cam_x, int cam_y, const Color* palette, uint8_t palette_size) const;

  uint16_t size() const { return pool_.size(); }
  const PoolCounters& counters() const { return pool_.counters(); }  // for Engine::trackPool

 private:
  Pool<Particle, PGE_PARTICLE_COUNT> pool_;
};

}  // namespace pge
