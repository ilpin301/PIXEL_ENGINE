// Health, damage events, animation helper (S5.2): handle lifecycle, damage/death events, invulnerability timing,
// event ring overflow, AUTO_REAP, unloaded-chunk countdown, setAnim / entityFrame.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include "pge/entity.h"
#include "pge/world.h"

#define CHECK(c)                                                          \
  do {                                                                    \
    if (!(c)) {                                                           \
      std::printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); \
      return 1;                                                           \
    }                                                                     \
  } while (0)

using namespace pge;

static TileProps g_props[256];
alignas(8) static uint8_t g_wbuf[1 << 18];
alignas(8) static uint8_t g_ebuf[1 << 16];

// 4x4 chunks of 16 px tiles; only chunk 0 is loaded, so a body at x >= 512 is frozen.
struct Env {
  Arena arena;
  World w;
  Error init() {
    g_props[1].flags = kTileSolid;
    g_props[2].flags = kTileSolid;
    arena.init(g_wbuf, sizeof(g_wbuf));
    WorldConfig c{};
    c.w_chunks = 4;
    c.h_chunks = 4;
    c.seed = 1;
    c.gen = flatGen;
    c.props = g_props;
    c.cache_slots = 4;
    c.tile_px = 16;
    return w.init(c, arena);
  }
};

static bool popIs(Entities& en, EventType t, EntityId a, int amount, int aux) {
  Event e;
  return en.popEvent(e) && e.type == t && e.a.index == a.index && e.a.gen == a.gen && e.amount == amount && e.aux == aux;
}

static int testLifecycle() {
  Arena a;
  a.init(g_ebuf, sizeof(g_ebuf));
  Entities en;
  CHECK(en.init(a, 4) == Ok);
  EntityId id = en.spawn(1, 0, 0, 4, 4);
  CHECK(en.health(id) == nullptr);
  CHECK(!en.addHealth(id, 0) && !en.addHealth(id, -5) && !en.addHealth(kNoEntity, 10) && !en.addHealth(EntityId{99, 1}, 10));
  CHECK(en.health(id) == nullptr);
  CHECK(en.addHealth(id, 10, kHealthAutoReap));
  Health* h = en.health(id);
  CHECK(h && h->hp == 10 && h->max_hp == 10 && h->invuln_ticks == 0 && h->flags == kHealthAutoReap);
  h->hp = 3;
  h->invuln_ticks = 9;
  CHECK(en.addHealth(id, 20) && en.health(id)->hp == 20 && en.health(id)->invuln_ticks == 0 && en.health(id)->flags == 0);

  en.despawn(id);
  CHECK(en.health(id) == nullptr && !en.addHealth(id, 5));  // dying counts as not alive
  en.endTick();
  CHECK(en.health(id) == nullptr);
  EntityId n = en.spawn(2, 0, 0, 4, 4, 0x1000 | kEntGravity);  // an internal bit from the caller must not fake a Health
  CHECK(n.index == id.index && n.gen != id.gen);
  CHECK(en.health(n) == nullptr && !(en.meta(n)->flags & kEntHealth) && !en.applyDamage(n, 1, 0));
  CHECK(en.health(id) == nullptr);  // stale handle
  CHECK(en.addHealth(n, 7) && en.health(n)->hp == 7);
  CHECK(en.health(id) == nullptr);
  return 0;
}

static int testDamage() {
  Arena a;
  a.init(g_ebuf, sizeof(g_ebuf));
  Entities en;
  CHECK(en.init(a, 8) == Ok);
  EntityId id = en.spawn(0, 0, 0, 4, 4);
  CHECK(en.addHealth(id, 10));
  CHECK(en.applyDamage(id, 3, 7));
  CHECK(en.health(id)->hp == 7 && en.health(id)->invuln_ticks == 0 && en.eventCount() == 1);
  CHECK(popIs(en, EventType::Damage, id, 3, 7));
  Event e;
  CHECK(!en.popEvent(e) && en.eventCount() == 0);

  // Exact lethal: Damage then Death, hp 0.
  CHECK(en.applyDamage(id, 7, 9));
  CHECK(en.health(id)->hp == 0 && en.eventCount() == 2);
  CHECK(popIs(en, EventType::Damage, id, 7, 9) && popIs(en, EventType::Death, id, 7, 9) && !en.popEvent(e));
  CHECK(!en.applyDamage(id, 1, 0) && en.eventCount() == 0 && en.health(id)->hp == 0);  // dead target

  // Over-lethal: hp clamps to 0, the event reports the amount asked for.
  EntityId o = en.spawn(0, 0, 0, 4, 4);
  CHECK(en.addHealth(o, 5));
  CHECK(en.applyDamage(o, 100, 2) && en.health(o)->hp == 0);
  CHECK(popIs(en, EventType::Damage, o, 100, 2) && popIs(en, EventType::Death, o, 100, 2) && !en.popEvent(e));
  EntityId big = en.spawn(0, 0, 0, 4, 4);
  CHECK(en.addHealth(big, 32767));
  CHECK(en.applyDamage(big, 32767, 0) && en.health(big)->hp == 0);
  en.popEvent(e);
  en.popEvent(e);

  // Rejected calls: no change, no event.
  EntityId f = en.spawn(0, 0, 0, 4, 4);
  EntityId nh = en.spawn(0, 0, 0, 4, 4);
  CHECK(en.addHealth(f, 10));
  CHECK(!en.applyDamage(f, 0, 0) && !en.applyDamage(f, -4, 0) && !en.applyDamage(f, INT16_MIN, 0));
  CHECK(!en.applyDamage(nh, 3, 0) && !en.applyDamage(kNoEntity, 3, 0) && !en.applyDamage(EntityId{99, 1}, 3, 0));
  EntityId stale = EntityId{f.index, (uint16_t)(f.gen + 1)};
  CHECK(!en.applyDamage(stale, 3, 0));
  en.despawn(f);
  CHECK(!en.applyDamage(f, 3, 0));
  CHECK(en.eventCount() == 0 && en.eventsDropped() == 0);
  return 0;
}

static int testInvuln() {
  Env e;
  CHECK(e.init() == Ok);
  CHECK(e.w.ensureLoaded(Rect{0, 0, 16, 16}, 1) == 0);
  Arena a;
  a.init(g_ebuf, sizeof(g_ebuf));
  Entities en;
  CHECK(en.init(a, 4) == Ok);
  const PhysicsParams pp{0, 0};
  EntityId id = en.spawn(0, 100, 100, 4, 4);
  CHECK(en.addHealth(id, 100));
  CHECK(en.applyDamage(id, 1, 0, 3));  // spawned this tick: update skips it, so the countdown starts after endTick
  en.update(e.w, pp);
  CHECK(en.health(id)->invuln_ticks == 3);
  en.endTick();
  Event ev;
  CHECK(en.popEvent(ev) && !en.popEvent(ev));

  CHECK(!en.applyDamage(id, 1, 0, 3) && en.health(id)->hp == 99 && en.eventCount() == 0);
  en.update(e.w, pp);
  CHECK(en.health(id)->invuln_ticks == 2 && !en.applyDamage(id, 1, 0));
  en.update(e.w, pp);
  CHECK(en.health(id)->invuln_ticks == 1 && !en.applyDamage(id, 1, 0));
  CHECK(en.eventCount() == 0 && en.health(id)->hp == 99);
  en.update(e.w, pp);  // third update since the hit: vulnerable again
  CHECK(en.health(id)->invuln_ticks == 0);
  CHECK(en.applyDamage(id, 1, 0, 3) && en.health(id)->hp == 98 && en.health(id)->invuln_ticks == 3);
  en.popEvent(ev);

  // invuln_ticks 0 = no protection; update never underflows.
  CHECK(en.applyDamage(id, 1, 0, 0) == false);  // still invulnerable from the previous hit
  for (int i = 0; i < 3; i++) en.update(e.w, pp);
  CHECK(en.applyDamage(id, 1, 0, 0) && en.applyDamage(id, 1, 0, 0) && en.health(id)->hp == 96);
  en.update(e.w, pp);
  CHECK(en.health(id)->invuln_ticks == 0);
  while (en.popEvent(ev)) {}

  // Frozen body (in-bounds chunk that is not loaded) still counts down.
  EntityId fz = en.spawn(0, 600, 100, 4, 4, kEntGravity | kEntCollide);
  CHECK(en.addHealth(fz, 10));
  en.endTick();
  en.motion(fz)->vx = 300;
  CHECK(en.applyDamage(fz, 1, 0, 2));
  en.update(e.w, PhysicsParams{64, 1000});
  CHECK(en.body(fz)->x == 600 && en.body(fz)->y == 100 && en.health(fz)->invuln_ticks == 1);
  CHECK(!en.applyDamage(fz, 1, 0));
  en.update(e.w, PhysicsParams{64, 1000});
  CHECK(en.body(fz)->x == 600 && en.health(fz)->invuln_ticks == 0 && en.applyDamage(fz, 1, 0));
  return 0;
}

static int testQueue() {
  Arena a;
  a.init(g_ebuf, sizeof(g_ebuf));
  Entities en;
  CHECK(en.init(a, 2) == Ok);
  EntityId id = en.spawn(0, 0, 0, 4, 4);
  CHECK(en.addHealth(id, 32000));
  const int N = PGE_EVENT_QUEUE;
  for (int i = 0; i < N + 3; i++) CHECK(en.applyDamage(id, (int16_t)(i + 1), (uint8_t)i));
  CHECK(en.eventsDropped() == 3 && en.eventCount() == N);
  for (int i = 3; i < N + 3; i++) CHECK(popIs(en, EventType::Damage, id, i + 1, i));  // oldest 3 gone, order kept
  Event ev;
  CHECK(!en.popEvent(ev) && en.eventCount() == 0 && en.eventsDropped() == 3);

  // FIFO across the wrap point of the ring.
  for (int round = 0; round < 3; round++) {
    for (int i = 0; i < N - 1; i++) CHECK(en.applyDamage(id, (int16_t)(i + 1), (uint8_t)round));
    for (int i = 0; i < N - 1; i++) CHECK(popIs(en, EventType::Damage, id, i + 1, round));
  }
  CHECK(en.eventsDropped() == 3);

  // A Death pair that overflows the ring drops the oldest event first.
  const uint16_t before = en.eventsDropped();
  CHECK(en.addHealth(id, 32000));
  for (int i = 0; i < N - 1; i++) CHECK(en.applyDamage(id, 1, 1));
  EntityId v = en.spawn(0, 0, 0, 4, 4);
  CHECK(en.addHealth(v, 1) && en.eventCount() == N - 1);
  CHECK(en.applyDamage(v, 1, 2));  // Damage fits, Death evicts the oldest
  CHECK(en.eventsDropped() == before + 1 && en.eventCount() == N);
  Event last = {};
  int seen = 0;
  while (en.popEvent(ev)) { last = ev; seen++; }
  CHECK(seen == N && last.type == EventType::Death && last.aux == 2);

  // The drop counter saturates; init resets queue and counter.
  for (int i = 0; i < 70000; i++) {
    en.addHealth(id, 100);
    en.applyDamage(id, 1, 0);
  }
  CHECK(en.eventsDropped() == 0xFFFF);
  CHECK(en.init(a, 2) == Ok && en.eventsDropped() == 0 && en.eventCount() == 0);
  return 0;
}

static int testAutoReap() {
  Arena a;
  a.init(g_ebuf, sizeof(g_ebuf));
  Entities en;
  CHECK(en.init(a, 8) == Ok);
  EntityId r = en.spawn(0, 0, 0, 4, 4), keep = en.spawn(0, 0, 0, 4, 4), full = en.spawn(0, 0, 0, 4, 4),
           both = en.spawn(0, 0, 0, 4, 4);
  CHECK(en.addHealth(r, 5, kHealthAutoReap) && en.addHealth(keep, 5) && en.addHealth(full, 5, kHealthAutoReap) &&
        en.addHealth(both, 5, kHealthAutoReap));
  en.endTick();
  CHECK(en.size() == 4);
  CHECK(en.applyDamage(r, 5, 0) && en.applyDamage(keep, 5, 0) && en.applyDamage(both, 9, 0));
  CHECK(en.alive(r) && en.size() == 4);  // nothing happens before endTick
  en.despawn(both);                      // reaped and despawned in the same tick: freed once
  en.endTick();
  CHECK(!en.alive(r) && !en.alive(both) && en.size() == 2);
  CHECK(en.alive(keep) && en.alive(full) && en.health(keep)->hp == 0 && en.health(full)->hp == 5);
  int zero = 0, scanned = 0;
  uint16_t found = 0xFFFF;
  en.forEach([&](EntityId i, Body&) {
    scanned++;
    if (en.health(i) && en.health(i)->hp <= 0) { zero++; found = i.index; }
  });
  CHECK(scanned == 2 && zero == 1 && found == keep.index);
  CHECK(!en.applyDamage(keep, 1, 0));
  en.endTick();
  CHECK(en.alive(keep) && en.size() == 2);

  EntityId n = en.spawn(0, 0, 0, 4, 4);  // freed slots are reusable at once
  CHECK(en.health(n) == nullptr && (n.index == r.index || n.index == both.index));
  CHECK(!en.alive(r) && en.health(r) == nullptr);
  return 0;
}

static int testAnim() {
  Arena a;
  a.init(g_ebuf, sizeof(g_ebuf));
  Entities en;
  CHECK(en.init(a, 4) == Ok);
  EntityId id = en.spawn(0, 0, 0, 4, 4);
  EntityMeta* m = en.meta(id);
  m->anim_t = 7;
  en.setAnim(id, 0);  // same id: clock kept
  CHECK(m->anim == 0 && m->anim_t == 7);
  en.setAnim(id, 1);
  CHECK(m->anim == 1 && m->anim_t == 0);
  m->anim_t = 3;
  en.setAnim(id, 1);
  CHECK(m->anim == 1 && m->anim_t == 3);
  en.setAnim(id, 0);
  CHECK(m->anim == 0 && m->anim_t == 0);
  en.setAnim(kNoEntity, 2);
  en.setAnim(EntityId{99, 1}, 2);  // ignored

  const Animation tab[2] = {{10, 4, 2, true}, {20, 3, 5, false}};
  EntityMeta t = {};
  for (int anim = 0; anim < 2; anim++) {
    t.anim = (uint8_t)anim;
    for (int ticks = 0; ticks < 100; ticks++) {
      t.anim_t = (uint16_t)ticks;
      const int want = anim == 0 ? 10 + (ticks / 2) % 4 : 20 + std::min(ticks / 5, 2);
      CHECK(entityFrame(t, tab, 2) == want);
      CHECK(entityFrame(t, tab, 2) == animFrame(tab[anim], (uint32_t)ticks));
    }
  }
  t.anim = 2;
  CHECK(entityFrame(t, tab, 2) == 0 && entityFrame(t, nullptr, 2) == 0);
  t.anim = 1;
  CHECK(entityFrame(t, tab, 1) == 0);
  return 0;
}

int main() {
  int rc = 0;
  rc |= testLifecycle();
  rc |= testDamage();
  rc |= testInvuln();
  rc |= testQueue();
  rc |= testAutoReap();
  rc |= testAnim();
  if (rc) return 1;
  std::printf("health: all tests passed\n");
  return 0;
}
