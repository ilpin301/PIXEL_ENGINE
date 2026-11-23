#pragma once
// Entity pool with tile-sweep physics and a uniform-grid overlap index (docs/architecture/03 section 8, K9).
// Per-slot data lives in parallel arrays allocated from an Arena in init; nothing else allocates.
#include <stdint.h>
#include "pge/collision.h"
#include "pge/config.h"
#include "pge/error.h"
#include "pge/gfx.h"
#include "pge/memory.h"
#include "pge/sprite.h"
#include "pge/world.h"

namespace pge {

// A handle goes stale when its slot is freed (the slot's gen increments on free only).
struct EntityId {
  uint16_t index, gen;
};
constexpr EntityId kNoEntity{0xFFFF, 0};
inline bool isValid(EntityId id) { return id.index != 0xFFFF; }

// vx, vy in 8.8 px/tick; rx, ry are the sub-pixel remainder 0..255 carried between ticks.
struct Motion {
  int16_t vx, vy;
  uint8_t rx, ry;
};

struct EntityMeta {
  uint8_t kind;
  uint8_t anim;
  uint16_t flags;  // EntityFlag bits
  uint16_t anim_t;
  GravityDir gravity_dir;  // set by the game each tick; Down = 0 after spawn
};

// Low byte: engine opt-ins (0x04..0x80 free for games). 0x0100..0x0400: results written by update().
enum EntityFlag : uint16_t {
  kEntGravity = 0x01,
  kEntCollide = 0x02,
  kEntOnGround = 0x0100,
  kEntHitX = 0x0200,
  kEntHitY = 0x0400,
  kEntHealth = 0x1000,   // internal: slot has a valid Health (addHealth)
  kEntIndexed = 0x2000,  // internal: in the grid built by the last rebuildGrid
  kEntFresh = 0x4000,    // internal: spawned this tick, update() skips it
  kEntDying = 0x8000,    // internal: despawn() called, slot freed in endTick
};

struct PhysicsParams {
  int gravity;   // 8.8 per tick^2 along EntityMeta::gravity_dir, >= 0
  int max_fall;  // 8.8 per tick, >= 0, caps the speed along gravity_dir only
};

struct Health {
  int16_t hp, max_hp;
  uint8_t invuln_ticks;  // counted down by update(), also for bodies frozen in unloaded chunks
  uint8_t flags;         // HealthFlag
};
enum HealthFlag : uint8_t { kHealthAutoReap = 0x01 };  // endTick despawns the entity once hp <= 0

enum class EventType : uint8_t { Damage, Death };
// Notification only: Health::hp is authoritative. aux is the `kind` passed to applyDamage.
struct Event {
  EventType type;
  EntityId a;
  int16_t amount;
  uint8_t aux;
};

// Frame to draw for m: animFrame(table[m.anim], m.anim_t), or 0 for a null table or an anim id >= count.
inline uint16_t entityFrame(const EntityMeta& m, const Animation* table, uint8_t count) {
  return (table && m.anim < count) ? animFrame(table[m.anim], m.anim_t) : (uint16_t)0;
}

class Entities {
 public:
  // capacity 0 = PGE_ENTITY_COUNT. InvalidArg if capacity > 65533; OutOfMemory if the arena runs out.
  Error init(Arena& a, uint16_t capacity = 0);

  // kNoEntity (and counters().full + 1) when the pool is full. Game flags only: internal bits are masked off.
  EntityId spawn(uint8_t kind, int x, int y, uint8_t w, uint8_t h, uint16_t flags = 0);
  void despawn(EntityId id);  // deferred to endTick; alive() is false at once; stale or repeated ids are ignored
  bool alive(EntityId id) const;
  Body* body(EntityId id);
  Motion* motion(EntityId id);
  EntityMeta* meta(EntityId id);
  void setAnim(EntityId id, uint8_t anim);  // restarts anim_t only when the animation id changes

  // false if not alive or max_hp <= 0. Sets hp = max_hp, invuln 0.
  bool addHealth(EntityId id, int16_t max_hp, uint8_t flags = 0);
  Health* health(EntityId id);  // nullptr if not alive or no health

  // false (no change, no event) if the target is not alive, has no Health, hp <= 0, is invulnerable or amount <= 0.
  // Else hp = max(0, hp - amount), invuln_ticks = the argument, pushes Damage (+ Death when hp reaches 0), `kind` goes to aux.
  bool applyDamage(EntityId target, int16_t amount, uint8_t kind, uint8_t invuln_ticks = 0);
  bool popEvent(Event& out);                                   // FIFO; false when empty
  uint16_t eventsDropped() const { return ev_dropped_; }       // oldest events lost to a full queue (saturating)
  uint16_t eventCount() const { return ev_n_; }

  // fn(EntityId, Body&) for every live, non-dying entity (fresh ones included), ascending slot order.
  template <class F>
  void forEach(F fn) {
    for (uint16_t i = 0; i < cap_; i++)
      if (link_[i] == kLive && !(meta_[i].flags & kEntDying)) fn(EntityId{i, gen_[i]}, body_[i]);
  }

  // Gravity, sub-pixel motion and tile collision for live, non-dying, non-fresh slots in ascending order; gravity,
  // on-ground and the tile sweep follow each body's gravity_dir. A body overlapping an in-bounds chunk that is not
  // loaded is frozen (velocity kept).
  void update(const World& w, const PhysicsParams& p);
  void endTick();  // despawns hp <= 0 entities with kHealthAutoReap, frees dying slots (gen + 1), clears kEntFresh

  // Index every live entity by the cell of its top-left corner, once per tick. Entities whose cell lies outside
  // active_px (or outside the PGE_ENTITY_GRID_CELLS budget) are not indexed.
  void rebuildGrid(Rect active_px);
  // Live entities indexed by the last rebuildGrid whose Body strictly overlaps r (touching edges do not count).
  // Writes at most cap ids; order is deterministic (cell scan order).
  uint16_t queryOverlap(Rect r, EntityId* out, uint16_t cap) const;

  uint16_t size() const { return size_; }  // slots in use, dying ones included until endTick
  uint16_t capacity() const { return cap_; }
  const PoolCounters& counters() const { return counters_; }
  uint32_t gridClamped() const { return grid_clamped_; }  // rebuildGrid calls that had to shrink the grid

 private:
  static constexpr uint16_t kLive = 0xFFFE;  // link_ value of a live slot; free slots hold the next free index
  static constexpr uint16_t kNone = 0xFFFF;

  Body* body_ = nullptr;
  Motion* mot_ = nullptr;
  EntityMeta* meta_ = nullptr;
  Health* health_ = nullptr;
  Event* ev_ = nullptr;  // ring of PGE_EVENT_QUEUE
  uint16_t ev_head_ = 0, ev_n_ = 0, ev_dropped_ = 0;
  void pushEvent(EventType t, EntityId a, int16_t amount, uint8_t aux);
  uint16_t* gen_ = nullptr;
  uint16_t* link_ = nullptr;   // free list / live marker
  uint16_t* gnext_ = nullptr;  // per-slot next entity in the same grid cell
  uint16_t* ghead_ = nullptr;  // PGE_ENTITY_GRID_CELLS list heads
  uint16_t cap_ = 0, size_ = 0, free_head_ = kNone;
  PoolCounters counters_;
  int gx0_ = 0, gy0_ = 0, gw_ = 0, gh_ = 0;  // grid origin and size in cells
  int max_w_ = 0, max_h_ = 0;                // largest indexed body extent
  uint32_t grid_clamped_ = 0;
};

}  // namespace pge
