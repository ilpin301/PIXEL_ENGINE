// Entity tests (S5.2): pool/handles, deferred despawn, fresh skip, gravity + tile collision against a reference
// trajectory, unloaded-chunk freeze, grid overlap queries vs a brute-force oracle, print-only update timing.
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>
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
alignas(8) static uint8_t g_wbuf[2][1 << 18];
alignas(8) static uint8_t g_ebuf[1 << 16];

static void initProps() {
  g_props[1].flags = kTileSolid;  // grass
  g_props[2].flags = kTileSolid;  // dirt
}

// 4x4 chunks, 16 px tiles: the world is 2048 px square, the surface (kFlatSurfaceRow) is at y = 256.
struct Env {
  Arena arena;
  World w;
  Error init(int idx, int slots) {
    initProps();
    arena.init(g_wbuf[idx], sizeof(g_wbuf[idx]));
    WorldConfig c{};
    c.w_chunks = 4;
    c.h_chunks = 4;
    c.seed = 1;
    c.gen = flatGen;
    c.props = g_props;
    c.cache_slots = (uint16_t)slots;
    c.tile_px = 16;
    return w.init(c, arena);
  }
};

static uint32_t g_rng = 2463534242u;
static uint32_t rnd() {  // xorshift32
  g_rng ^= g_rng << 13;
  g_rng ^= g_rng >> 17;
  g_rng ^= g_rng << 5;
  return g_rng;
}
static int rndRange(int lo, int hi) { return lo + (int)(rnd() % (uint32_t)(hi - lo)); }  // [lo, hi)

static int testHandles() {
  Arena a;
  a.init(g_ebuf, sizeof(g_ebuf));
  Entities en;
  CHECK(en.init(a, 65534) == InvalidArg);
  CHECK(en.init(a, 8) == Ok);
  CHECK(en.capacity() == 8 && en.size() == 0);
  CHECK(!isValid(kNoEntity) && !en.alive(kNoEntity));

  EntityId e0 = en.spawn(1, 5, 6, 7, 8, kEntGravity | 0xE000);
  EntityId e1 = en.spawn(2, 0, 0, 1, 1);
  CHECK(e0.index == 0 && e1.index == 1 && e0.gen == 1);
  CHECK(en.meta(e0)->flags == (kEntGravity | kEntFresh));  // internal bits from the caller are masked
  CHECK(en.meta(e0)->kind == 1 && en.body(e0)->w == 7 && en.body(e0)->y == 6);
  CHECK(en.motion(e0)->vx == 0 && en.motion(e0)->ry == 0);

  en.despawn(e0);
  CHECK(!en.alive(e0) && en.body(e0) == nullptr && en.motion(e0) == nullptr && en.meta(e0) == nullptr);
  en.despawn(e0);  // duplicate
  en.despawn(kNoEntity);
  en.despawn(EntityId{500, 1});
  CHECK(en.alive(e1));
  en.endTick();
  CHECK(!en.alive(e0));
  EntityId n0 = en.spawn(3, 0, 0, 1, 1);
  CHECK(n0.index == e0.index && n0.gen != e0.gen && en.alive(n0) && !en.alive(e0));
  en.despawn(e0);  // stale handle must not kill the new occupant
  CHECK(en.alive(n0));
  CHECK(en.meta(n0)->kind == 3);

  Arena tiny;
  tiny.init(g_ebuf, 64);
  Entities en2;
  CHECK(en2.init(tiny, 100) == OutOfMemory);
  return 0;
}

static int testExhaustion() {
  Arena a;
  a.init(g_ebuf, sizeof(g_ebuf));
  Entities en;
  CHECK(en.init(a, 4) == Ok);
  const size_t used = a.used();
  EntityId ids[4];
  for (int i = 0; i < 4; i++) {
    ids[i] = en.spawn(0, i, 0, 1, 1);
    CHECK(isValid(ids[i]));
  }
  CHECK(!isValid(en.spawn(0, 0, 0, 1, 1)));
  CHECK(en.counters().full == 1 && en.counters().high == 4 && en.size() == 4);
  CHECK(!isValid(en.spawn(0, 0, 0, 1, 1)) && en.counters().full == 2);
  CHECK(a.used() == used && a.failed() == 0);
  en.despawn(ids[2]);
  en.endTick();
  CHECK(isValid(en.spawn(0, 0, 0, 1, 1)) && en.counters().high == 4);

  Arena d;  // default capacity
  d.init(g_ebuf, sizeof(g_ebuf));
  Entities big;
  CHECK(big.init(d) == Ok && big.capacity() == PGE_ENTITY_COUNT);
  return 0;
}

static int testOrderAndDeferral() {
  Env e;
  CHECK(e.init(0, 16) == Ok);
  CHECK(e.w.ensureLoaded(Rect{0, 0, 2048, 2048}, 16) == 0);
  Arena a;
  a.init(g_ebuf, sizeof(g_ebuf));
  Entities en;
  CHECK(en.init(a, 8) == Ok);
  EntityId id[5];
  for (int i = 0; i < 5; i++) id[i] = en.spawn((uint8_t)i, 10 * i, 10, 4, 4);
  en.despawn(id[1]);
  en.despawn(id[3]);
  en.endTick();
  CHECK(en.size() == 3);
  EntityId r1 = en.spawn(10, 0, 0, 1, 1);  // LIFO: last freed (3), then 1
  EntityId r2 = en.spawn(11, 0, 0, 1, 1);
  CHECK(r1.index == 3 && r2.index == 1);
  int seen[8], n = 0;
  en.forEach([&](EntityId i, Body&) { seen[n++] = i.index; });
  CHECK(n == 5 && seen[0] == 0 && seen[1] == 1 && seen[2] == 2 && seen[3] == 3 && seen[4] == 4);

  // Fresh entities are not moved by the update that follows their spawn, but by the next one.
  en.endTick();
  EntityId f = en.spawn(20, 100, 100, 4, 4);
  en.motion(f)->vx = 256;
  const PhysicsParams pp{0, 0};
  en.update(e.w, pp);
  CHECK(en.body(f)->x == 100 && en.meta(f)->anim_t == 0);
  en.endTick();
  en.update(e.w, pp);
  CHECK(en.body(f)->x == 101 && en.meta(f)->anim_t == 1);
  en.endTick();

  // Deferred despawn: slot not reusable and invisible to forEach until endTick.
  const uint16_t freed = f.index;
  en.despawn(f);
  CHECK(!en.alive(f));
  EntityId g = en.spawn(21, 0, 0, 1, 1);
  CHECK(!isValid(g) || g.index != freed);
  int sawFreed = 0;
  en.forEach([&](EntityId i, Body&) { if (i.index == freed) sawFreed = 1; });
  CHECK(sawFreed == 0);
  const uint16_t before = en.size();
  en.endTick();
  CHECK(en.size() == before - 1);
  return 0;
}

static int testPhysics() {
  Env e;
  CHECK(e.init(0, 16) == Ok);
  CHECK(e.w.ensureLoaded(Rect{0, 0, 2048, 2048}, 16) == 0);
  Arena a;
  a.init(g_ebuf, sizeof(g_ebuf));
  Entities en;
  CHECK(en.init(a, 8) == Ok);
  const uint16_t fl = kEntGravity | kEntCollide;

  // Hand-computed free fall, sub-pixel gravity, two max_fall settings (reference loop written independently).
  const int cfg[2][2] = {{64, 6 * 256}, {64, 400}};
  const int nticks[2] = {10, 20};
  for (int c = 0; c < 2; c++) {
    a.reset();
    CHECK(en.init(a, 8) == Ok);
    EntityId id = en.spawn(0, 50, 0, 8, 8, fl);
    const PhysicsParams pp{cfg[c][0], cfg[c][1]};
    en.update(e.w, pp);  // fresh: no motion
    en.endTick();
    CHECK(en.body(id)->y == 0 && en.motion(id)->vy == 0);
    int vy = 0, ry = 0, y = 0;
    for (int t = 0; t < nticks[c]; t++) {
      en.update(e.w, pp);
      en.endTick();
      vy += pp.gravity;
      if (vy > pp.max_fall) vy = pp.max_fall;
      const int tot = ry + vy;
      y += tot / 256;
      ry = tot % 256;
      CHECK(en.body(id)->y == y && en.motion(id)->vy == vy && en.motion(id)->ry == ry);
      CHECK(en.body(id)->x == 50);
    }
    CHECK(vy == pp.max_fall || c == 0);
  }

  // Landing flush on the surface.
  a.reset();
  CHECK(en.init(a, 8) == Ok);
  EntityId d = en.spawn(0, 50, 0, 8, 8, fl);
  const PhysicsParams pp{64, 6 * 256};
  en.update(e.w, pp);
  en.endTick();
  bool landed = false;
  for (int t = 0; t < 400 && !landed; t++) {
    en.update(e.w, pp);
    en.endTick();
    if (en.meta(d)->flags & kEntHitY) {
      landed = true;
      CHECK(en.body(d)->y + en.body(d)->h == kFlatSurfaceRow * 16);
      CHECK(en.meta(d)->flags & kEntOnGround);
      CHECK(en.motion(d)->vy == 0 && en.motion(d)->ry == 0);
    }
  }
  CHECK(landed);
  for (int t = 0; t < 50; t++) {  // stays put while resting
    en.update(e.w, pp);
    en.endTick();
    CHECK(en.body(d)->y == 248 && (en.meta(d)->flags & kEntOnGround));
  }

  // max_fall clamp: a vy above the limit is cut to it, and a long fall never exceeds it.
  a.reset();
  CHECK(en.init(a, 8) == Ok);
  EntityId m = en.spawn(0, 50, 0, 8, 8, fl);
  en.endTick();
  en.motion(m)->vy = 1000;
  en.update(e.w, PhysicsParams{64, 512});
  CHECK(en.motion(m)->vy == 512);
  en.endTick();

  // Left world edge: walking left stops flush at x == 0 and reports hit_x.
  a.reset();
  CHECK(en.init(a, 8) == Ok);
  EntityId wk = en.spawn(0, 10, 248, 8, 8, fl);
  en.endTick();
  en.motion(wk)->vx = -512;
  bool hitx = false;
  for (int t = 0; t < 10; t++) {
    en.update(e.w, PhysicsParams{0, 0});
    en.endTick();
    if (en.meta(wk)->flags & kEntHitX) { hitx = true; CHECK(en.motion(wk)->vx == 0 && en.motion(wk)->rx == 0); }
  }
  CHECK(hitx && en.body(wk)->x == 0 && en.body(wk)->y == 248);

  // Non-colliding body ignores tiles; result flags are cleared.
  a.reset();
  CHECK(en.init(a, 8) == Ok);
  EntityId gh = en.spawn(0, 50, 300, 8, 8, kEntGravity);  // inside the dirt
  en.endTick();
  en.update(e.w, PhysicsParams{256, 4096});
  CHECK(en.body(gh)->y == 301 && !(en.meta(gh)->flags & (kEntOnGround | kEntHitY)));

  // In-bounds chunk that is not loaded: frozen, velocity kept, animation clock still runs.
  Env u;
  CHECK(u.init(1, 4) == Ok);
  CHECK(u.w.ensureLoaded(Rect{0, 0, 16, 16}, 1) == 0 && u.w.chunksLoaded() == 1);
  a.reset();
  CHECK(en.init(a, 8) == Ok);
  EntityId fz = en.spawn(0, 600, 100, 8, 8, fl);  // tile x 37 -> chunk 1, not loaded
  EntityId ct = en.spawn(0, 100, 100, 8, 8, fl);  // chunk 0, loaded: control
  en.endTick();
  en.motion(fz)->vy = 300;
  en.motion(fz)->vx = 100;
  en.update(u.w, PhysicsParams{64, 6 * 256});
  CHECK(en.body(fz)->x == 600 && en.body(fz)->y == 100);
  CHECK(en.motion(fz)->vy == 300 && en.motion(fz)->vx == 100 && en.meta(fz)->anim_t == 1);
  CHECK(en.body(ct)->y == 100 && en.motion(ct)->vy == 64);  // sub-pixel: not yet a whole pixel
  for (int t = 0; t < 10; t++) en.update(u.w, PhysicsParams{64, 6 * 256});
  CHECK(en.body(ct)->y > 100 && en.body(fz)->y == 100);
  return 0;
}

static bool overlap(const Body& b, const Rect& r) {
  return b.x < r.x + r.w && r.x < b.x + b.w && b.y < r.y + r.h && r.y < b.y + b.h;
}

static int testTouching() {
  Arena a;
  a.init(g_ebuf, sizeof(g_ebuf));
  Entities en;
  CHECK(en.init(a, 8) == Ok);
  EntityId id = en.spawn(0, 0, 0, 10, 10);
  EntityId wide = en.spawn(0, 0, 40, 100, 4);  // wider than a cell
  en.rebuildGrid(Rect{-96, -96, 192, 192});
  EntityId out[8];
  CHECK(en.queryOverlap(Rect{10, 0, 5, 5}, out, 8) == 0);   // touching right edge
  CHECK(en.queryOverlap(Rect{-5, 0, 5, 5}, out, 8) == 0);   // touching left edge
  CHECK(en.queryOverlap(Rect{0, 10, 5, 5}, out, 8) == 0);   // touching bottom edge
  CHECK(en.queryOverlap(Rect{0, -5, 5, 5}, out, 8) == 0);   // touching top edge
  CHECK(en.queryOverlap(Rect{9, 9, 5, 5}, out, 8) == 1 && out[0].index == id.index);
  CHECK(en.queryOverlap(Rect{90, 41, 5, 1}, out, 8) == 1 && out[0].index == wide.index);  // top-left is 3 cells away
  CHECK(en.queryOverlap(Rect{9, 9, 0, 5}, out, 8) == 0 && en.queryOverlap(Rect{9, 9, 5, -1}, out, 8) == 0);
  CHECK(en.queryOverlap(Rect{9, 9, 5, 5}, nullptr, 8) == 0 && en.queryOverlap(Rect{9, 9, 5, 5}, out, 0) == 0);

  // Oversized active rect: the grid is shrunk to its budget, counted, and what it still covers keeps working.
  CHECK(en.gridClamped() == 0);
  en.rebuildGrid(Rect{0, 0, 100000, 100000});
  CHECK(en.gridClamped() == 1);
  CHECK(en.queryOverlap(Rect{9, 9, 5, 5}, out, 8) == 1);
  en.rebuildGrid(Rect{0, 0, 0, 5});
  CHECK(en.queryOverlap(Rect{9, 9, 5, 5}, out, 8) == 0);
  return 0;
}

static int testQueryOracle() {
  Arena a;
  const int kGx0 = -3, kGy0 = -3, kGw = 6, kGh = 7;  // 42 cells: fits every profile
  const Rect active{kGx0 * 32, kGy0 * 32, kGw * 32, kGh * 32};
  long total_hits = 0, capped = 0;
  for (int layout = 0; layout < 300; layout++) {
    a.init(g_ebuf, sizeof(g_ebuf));
    Entities en;
    CHECK(en.init(a, 64) == Ok);
    EntityId ids[40];
    for (int i = 0; i < 40; i++) {
      int x = rndRange(-128, 128), y = rndRange(-128, 160);
      if (rnd() % 4 == 0) x = 32 * rndRange(-4, 4);  // on a cell edge
      if (rnd() % 4 == 0) y = 32 * rndRange(-4, 5);
      ids[i] = en.spawn(0, x, y, (uint8_t)rndRange(1, 121), (uint8_t)rndRange(1, 121));
      CHECK(isValid(ids[i]));
    }
    en.endTick();
    en.rebuildGrid(active);
    CHECK(en.gridClamped() == 0);
    bool excluded[40] = {};
    for (int k = 0; k < 3; k++) {  // dying after the rebuild
      int i = rndRange(0, 40);
      en.despawn(ids[i]);
      excluded[i] = true;
    }
    EntityId late = en.spawn(0, 0, 0, 120, 120);  // not indexed by the last rebuild
    CHECK(isValid(late));

    for (int q = 0; q < 20; q++) {
      Rect r{rndRange(-200, 200), rndRange(-200, 200), rndRange(-3, 160), rndRange(-3, 160)};
      std::vector<int> want;
      for (int i = 0; i < 40; i++) {
        if (excluded[i]) continue;
        const Body* b = en.body(ids[i]);
        const int cx = (b->x >= 0 ? b->x / 32 : -((-b->x + 31) / 32)), cy = (b->y >= 0 ? b->y / 32 : -((-b->y + 31) / 32));
        if (cx < kGx0 || cx >= kGx0 + kGw || cy < kGy0 || cy >= kGy0 + kGh) continue;  // outside the grid
        if (r.w > 0 && r.h > 0 && overlap(*b, r)) want.push_back(ids[i].index);
      }
      std::sort(want.begin(), want.end());
      EntityId out[64];
      uint16_t n = en.queryOverlap(r, out, 64);
      std::vector<int> got;
      for (int i = 0; i < n; i++) got.push_back(out[i].index);
      std::sort(got.begin(), got.end());
      CHECK(got == want);
      total_hits += n;
      uint16_t n3 = en.queryOverlap(r, out, 3);
      CHECK(n3 == (want.size() < 3 ? want.size() : 3));
      for (int i = 0; i < n3; i++) CHECK(std::binary_search(want.begin(), want.end(), (int)out[i].index));
      if (want.size() > 3) capped++;
    }
    // Same grid, same query: identical order.
    EntityId o1[64], o2[64];
    Rect r{-50, -50, 100, 100};
    uint16_t n1 = en.queryOverlap(r, o1, 64), n2 = en.queryOverlap(r, o2, 64);
    CHECK(n1 == n2);
    for (int i = 0; i < n1; i++) CHECK(o1[i].index == o2[i].index);
  }
  CHECK(total_hits > 1000 && capped > 100);  // the layouts actually exercise the queries
  return 0;
}

static void timing() {
  Env e;
  if (e.init(0, 16) != Ok) return;
  e.w.ensureLoaded(Rect{0, 0, 2048, 2048}, 16);
  Arena a;
  a.init(g_ebuf, sizeof(g_ebuf));
  Entities en;
  if (en.init(a, 256) != Ok) return;
  for (int i = 0; i < 256; i++) {
    EntityId id = en.spawn(0, 40 + 7 * i, 248, 8, 8, kEntGravity | kEntCollide);
    if (isValid(id)) en.motion(id)->vx = (int16_t)((i & 1) ? 64 : -64);
  }
  en.endTick();
  const PhysicsParams pp{64, 6 * 256};
  const int N = 1000;
  auto t0 = std::chrono::steady_clock::now();
  for (int i = 0; i < N; i++) en.update(e.w, pp);
  auto t1 = std::chrono::steady_clock::now();
  const double us = std::chrono::duration<double, std::micro>(t1 - t0).count() / N;
#ifdef NDEBUG
  const char* bt = "NDEBUG build";
#else
  const char* bt = "no NDEBUG";
#endif
  std::printf("entities update: %.2f us/tick for 256 bodies (host, %s, not representative)\n", us, bt);
}

int main() {
  int rc = 0;
  rc |= testHandles();
  rc |= testExhaustion();
  rc |= testOrderAndDeferral();
  rc |= testPhysics();
  rc |= testTouching();
  rc |= testQueryOracle();
  if (rc) return 1;
  timing();
  std::printf("entities: all tests passed\n");
  return 0;
}
