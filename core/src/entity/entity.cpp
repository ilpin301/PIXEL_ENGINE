#include "pge/entity.h"
#include <string.h>

namespace pge {

namespace {

int fdiv(int a, int b) {  // floor division, b > 0
  int q = a / b;
  return (a % b != 0 && a < 0) ? q - 1 : q;
}

constexpr uint16_t kInternalFlags = kEntHealth | kEntIndexed | kEntFresh | kEntDying;
constexpr uint16_t kResultFlags = kEntOnGround | kEntHitX | kEntHitY;

}  // namespace

Error Entities::init(Arena& a, uint16_t capacity) {
  if (capacity > 65533) return InvalidArg;
  const uint16_t n = capacity ? capacity : (uint16_t)PGE_ENTITY_COUNT;
  Body* body = a.allocArray<Body>(n);
  Motion* mot = a.allocArray<Motion>(n);
  EntityMeta* meta = a.allocArray<EntityMeta>(n);
  uint16_t* gen = a.allocArray<uint16_t>(n);
  uint16_t* link = a.allocArray<uint16_t>(n);
  uint16_t* gnext = a.allocArray<uint16_t>(n);
  uint16_t* ghead = a.allocArray<uint16_t>(PGE_ENTITY_GRID_CELLS);
  Health* health = a.allocArray<Health>(n);
  Event* ev = a.allocArray<Event>(PGE_EVENT_QUEUE);
  if (!body || !mot || !meta || !gen || !link || !gnext || !ghead || !health || !ev) return OutOfMemory;
  memset(health, 0, sizeof(Health) * n);
  memset(body, 0, sizeof(Body) * n);
  memset(mot, 0, sizeof(Motion) * n);
  memset(meta, 0, sizeof(EntityMeta) * n);
  for (uint16_t i = 0; i < n; i++) {
    gen[i] = 1;
    link[i] = (uint16_t)(i + 1 < n ? i + 1 : kNone);
    gnext[i] = kNone;
  }
  for (int i = 0; i < PGE_ENTITY_GRID_CELLS; i++) ghead[i] = kNone;
  body_ = body; mot_ = mot; meta_ = meta; gen_ = gen; link_ = link; gnext_ = gnext; ghead_ = ghead;
  health_ = health; ev_ = ev;
  ev_head_ = ev_n_ = ev_dropped_ = 0;
  cap_ = n; size_ = 0; free_head_ = 0;
  counters_ = PoolCounters();
  gx0_ = gy0_ = gw_ = gh_ = 0;
  max_w_ = max_h_ = 0;
  grid_clamped_ = 0;
  return Ok;
}

EntityId Entities::spawn(uint8_t kind, int x, int y, uint8_t w, uint8_t h, uint16_t flags) {
  if (free_head_ == kNone) {
    if (counters_.full != 0xFFFF) counters_.full++;
    return kNoEntity;
  }
  const uint16_t i = free_head_;
  free_head_ = link_[i];
  link_[i] = kLive;
  body_[i] = Body{x, y, w, h};
  mot_[i] = Motion{0, 0, 0, 0};
  meta_[i] = EntityMeta{kind, 0, (uint16_t)((flags & ~kInternalFlags) | kEntFresh), 0};
  if (++size_ > counters_.high) counters_.high = size_;
  return EntityId{i, gen_[i]};
}

bool Entities::alive(EntityId id) const {
  return id.index < cap_ && link_[id.index] == kLive && gen_[id.index] == id.gen &&
         !(meta_[id.index].flags & kEntDying);
}

void Entities::despawn(EntityId id) {
  if (alive(id)) meta_[id.index].flags |= kEntDying;
}

Body* Entities::body(EntityId id) { return alive(id) ? &body_[id.index] : nullptr; }
Motion* Entities::motion(EntityId id) { return alive(id) ? &mot_[id.index] : nullptr; }
EntityMeta* Entities::meta(EntityId id) { return alive(id) ? &meta_[id.index] : nullptr; }

void Entities::setAnim(EntityId id, uint8_t anim) {
  if (!alive(id)) return;
  EntityMeta& m = meta_[id.index];
  if (m.anim == anim) return;
  m.anim = anim;
  m.anim_t = 0;
}

bool Entities::addHealth(EntityId id, int16_t max_hp, uint8_t flags) {
  if (!alive(id) || max_hp <= 0) return false;
  health_[id.index] = Health{max_hp, max_hp, 0, flags};
  meta_[id.index].flags |= kEntHealth;
  return true;
}

Health* Entities::health(EntityId id) {
  return (alive(id) && (meta_[id.index].flags & kEntHealth)) ? &health_[id.index] : nullptr;
}

void Entities::pushEvent(EventType t, EntityId a, int16_t amount, uint8_t aux) {
  if (ev_n_ == PGE_EVENT_QUEUE) {  // full: drop the oldest
    ev_head_ = (uint16_t)((ev_head_ + 1) % PGE_EVENT_QUEUE);
    ev_n_--;
    if (ev_dropped_ != 0xFFFF) ev_dropped_++;
  }
  ev_[(ev_head_ + ev_n_) % PGE_EVENT_QUEUE] = Event{t, a, amount, aux};
  ev_n_++;
}

bool Entities::popEvent(Event& out) {
  if (ev_n_ == 0) return false;
  out = ev_[ev_head_];
  ev_head_ = (uint16_t)((ev_head_ + 1) % PGE_EVENT_QUEUE);
  ev_n_--;
  return true;
}

bool Entities::applyDamage(EntityId target, int16_t amount, uint8_t kind, uint8_t invuln_ticks) {
  Health* h = health(target);
  if (!h || h->hp <= 0 || h->invuln_ticks > 0 || amount <= 0) return false;
  const int left = h->hp - amount;
  h->hp = (int16_t)(left > 0 ? left : 0);
  h->invuln_ticks = invuln_ticks;
  pushEvent(EventType::Damage, target, amount, kind);
  if (h->hp == 0) pushEvent(EventType::Death, target, amount, kind);
  return true;
}

void Entities::update(const World& w, const PhysicsParams& p) {
  const int tp = w.tilePx();
  for (uint16_t i = 0; i < cap_; i++) {
    if (link_[i] != kLive) continue;
    EntityMeta& m = meta_[i];
    if (m.flags & (kEntDying | kEntFresh)) continue;
    if (m.anim_t != 0xFFFF) m.anim_t++;
    if ((m.flags & kEntHealth) && health_[i].invuln_ticks > 0) health_[i].invuln_ticks--;
    Body& b = body_[i];
    Motion& v = mot_[i];

    // Frozen while any corner tile sits in an in-bounds chunk that is not loaded (out of bounds is not frozen:
    // tileFlags already reports solid there).
    const int tx0 = fdiv(b.x, tp), tx1 = fdiv(b.x + b.w - 1, tp);
    const int ty0 = fdiv(b.y, tp), ty1 = fdiv(b.y + b.h - 1, tp);
    const int cx[4] = {tx0, tx1, tx0, tx1}, cy[4] = {ty0, ty0, ty1, ty1};
    bool frozen = false;
    for (int k = 0; k < 4; k++)
      if (w.inBounds(cx[k], cy[k]) && !w.isLoaded(cx[k] >> 5, cy[k] >> 5)) frozen = true;
    if (frozen) continue;

    if (m.flags & kEntGravity) {
      int vy = v.vy + p.gravity;
      if (vy > p.max_fall) vy = p.max_fall;
      if (vy > INT16_MAX) vy = INT16_MAX;
      v.vy = (int16_t)vy;
    }
    const int tot_x = v.rx + v.vx, tot_y = v.ry + v.vy;  // >> on negatives is arithmetic (floor) on the targets
    const int dx = tot_x >> 8, dy = tot_y >> 8;
    v.rx = (uint8_t)(tot_x & 255);
    v.ry = (uint8_t)(tot_y & 255);

    m.flags &= (uint16_t)~kResultFlags;
    if (m.flags & kEntCollide) {
      const MoveResult r = moveAndCollide(w, b, dx, dy);
      if (r.hit_x) { v.vx = 0; v.rx = 0; m.flags |= kEntHitX; }
      if (r.hit_y) { v.vy = 0; v.ry = 0; m.flags |= kEntHitY; }
      if (r.on_ground) m.flags |= kEntOnGround;
    } else {
      b.x += dx;
      b.y += dy;
    }
  }
}

void Entities::endTick() {
  for (uint16_t i = 0; i < cap_; i++) {
    if (link_[i] != kLive) continue;
    // Reap first so the slot is freed in this same endTick.
    if ((meta_[i].flags & (kEntHealth | kEntDying)) == kEntHealth && (health_[i].flags & kHealthAutoReap) &&
        health_[i].hp <= 0)
      meta_[i].flags |= kEntDying;
    if (meta_[i].flags & kEntDying) {
      if (++gen_[i] == 0) gen_[i] = 1;
      link_[i] = free_head_;
      free_head_ = i;
      size_--;
    } else {
      meta_[i].flags &= (uint16_t)~kEntFresh;
    }
  }
}

void Entities::rebuildGrid(Rect a) {
  int gw = 0, gh = 0;
  const int cx0 = fdiv(a.x, PGE_ENTITY_CELL_PX), cy0 = fdiv(a.y, PGE_ENTITY_CELL_PX);
  if (a.w > 0 && a.h > 0) {
    gw = fdiv(a.x + a.w - 1, PGE_ENTITY_CELL_PX) - cx0 + 1;
    gh = fdiv(a.y + a.h - 1, PGE_ENTITY_CELL_PX) - cy0 + 1;
  }
  if ((int64_t)gw * gh > PGE_ENTITY_GRID_CELLS) {
    if (gw > PGE_ENTITY_GRID_CELLS) gw = PGE_ENTITY_GRID_CELLS;
    if (gh > PGE_ENTITY_GRID_CELLS / gw) gh = PGE_ENTITY_GRID_CELLS / gw;
    grid_clamped_++;
  }
  gx0_ = cx0; gy0_ = cy0; gw_ = gw; gh_ = gh;
  max_w_ = max_h_ = 0;
  for (int i = 0; i < gw * gh; i++) ghead_[i] = kNone;
  // Descending so that each cell's list ends up in ascending slot order.
  for (int i = cap_ - 1; i >= 0; i--) {
    if (link_[i] != kLive) continue;
    EntityMeta& m = meta_[i];
    m.flags &= (uint16_t)~kEntIndexed;
    if (m.flags & kEntDying) continue;
    const Body& b = body_[i];
    const int cx = fdiv(b.x, PGE_ENTITY_CELL_PX) - gx0_, cy = fdiv(b.y, PGE_ENTITY_CELL_PX) - gy0_;
    if (cx < 0 || cy < 0 || cx >= gw_ || cy >= gh_) continue;
    uint16_t& head = ghead_[cy * gw_ + cx];
    gnext_[i] = head;
    head = (uint16_t)i;
    m.flags |= kEntIndexed;
    if (b.w > max_w_) max_w_ = b.w;
    if (b.h > max_h_) max_h_ = b.h;
  }
}

uint16_t Entities::queryOverlap(Rect r, EntityId* out, uint16_t cap) const {
  if (!out || cap == 0 || r.w <= 0 || r.h <= 0 || gw_ == 0 || gh_ == 0) return 0;
  // shortcut: one cell per entity plus global max extent, so one huge body widens every query; upgrade to multi-cell membership if that shows in profiles.
  // An overlapping body has its top-left in (r.x - max_w, r.x + r.w), so scan those cells, clamped to the grid.
  int c0 = fdiv(r.x - max_w_ + 1, PGE_ENTITY_CELL_PX) - gx0_, c1 = fdiv(r.x + r.w - 1, PGE_ENTITY_CELL_PX) - gx0_;
  int r0 = fdiv(r.y - max_h_ + 1, PGE_ENTITY_CELL_PX) - gy0_, r1 = fdiv(r.y + r.h - 1, PGE_ENTITY_CELL_PX) - gy0_;
  if (c0 < 0) c0 = 0;
  if (r0 < 0) r0 = 0;
  if (c1 >= gw_) c1 = gw_ - 1;
  if (r1 >= gh_) r1 = gh_ - 1;
  uint16_t n = 0;
  for (int cy = r0; cy <= r1; cy++) {
    for (int cx = c0; cx <= c1; cx++) {
      for (uint16_t i = ghead_[cy * gw_ + cx]; i != kNone; i = gnext_[i]) {
        // A slot freed or respawned after rebuildGrid still sits in the chain; kEntIndexed filters it out.
        if (link_[i] != kLive || (meta_[i].flags & (kEntDying | kEntIndexed)) != kEntIndexed) continue;
        const Body& b = body_[i];
        if (b.x < r.x + r.w && r.x < b.x + b.w && b.y < r.y + r.h && r.y < b.y + b.h) {
          out[n++] = EntityId{i, gen_[i]};
          if (n == cap) return n;
        }
      }
    }
  }
  return n;
}

}  // namespace pge
