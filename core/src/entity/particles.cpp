#include "pge/particles.h"

namespace pge {

namespace {

int clampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

}  // namespace

bool Particles::spawn(int x, int y, int vx, int vy, uint8_t life, uint8_t color) {
  if (life == 0) return false;
  const Pool<Particle, PGE_PARTICLE_COUNT>::Handle h = pool_.alloc();
  if (h == pool_.kInvalid) return false;
  Particle* p = pool_.get(h);
  *p = Particle{(int16_t)clampInt(x, INT16_MIN, INT16_MAX), (int16_t)clampInt(y, INT16_MIN, INT16_MAX),
                (int8_t)clampInt(vx, INT8_MIN, INT8_MAX), (int8_t)clampInt(vy, INT8_MIN, INT8_MAX), life, color};
  return true;
}

void Particles::update(uint8_t gravity_period) {
  for (uint16_t h = 0; h < pool_.capacity(); h++) {
    Particle* p = pool_.get(h);
    if (!p) continue;
    if (gravity_period != 0 && (p->life % gravity_period) == 0 && p->vy < 127) p->vy++;
    p->x = (int16_t)clampInt(p->x + p->vx, INT16_MIN, INT16_MAX);
    p->y = (int16_t)clampInt(p->y + p->vy, INT16_MIN, INT16_MAX);
    if (--p->life == 0) pool_.free(h);
  }
}

void Particles::draw(Canvas& c, int cam_x, int cam_y, const Color* palette, uint8_t palette_size) const {
  if (!palette || palette_size == 0) return;
  for (uint16_t h = 0; h < pool_.capacity(); h++) {
    const Particle* p = pool_.get(h);
    if (!p) continue;
    c.pixel(p->x - cam_x, p->y - cam_y, palette[p->color < palette_size ? p->color : 0]);
  }
}

}  // namespace pge
