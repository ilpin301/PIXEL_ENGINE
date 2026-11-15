// Core tests: Pool, Arena, Timestep, Engine lifecycle (null host backend).
#include <cstdio>
#include "pge/engine.h"

#define CHECK(c)                                                  \
  do {                                                            \
    if (!(c)) {                                                   \
      std::printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); \
      return 1;                                                   \
    }                                                             \
  } while (0)

static int testPool() {
  typedef pge::Pool<uint32_t, 4> P;
  P p;
  P::Handle h[4];
  for (int i = 0; i < 4; i++) {
    h[i] = p.alloc();
    CHECK(h[i] != P::kInvalid);
    CHECK(*p.get(h[i]) == 0);
  }
  CHECK(p.size() == 4 && p.capacity() == 4);
  CHECK(p.alloc() == P::kInvalid);
  CHECK(p.counters().full == 1 && p.counters().high == 4);
  *p.get(h[1]) = 77;
  CHECK(p.free(h[1]));
  CHECK(p.size() == 3);
  P::Handle r = p.alloc();  // reuses the freed slot, zeroed again
  CHECK(r == h[1] && *p.get(r) == 0);
  CHECK(p.free(h[2]));
  CHECK(!p.free(h[2]));  // double free
  CHECK(p.size() == 3);
  CHECK(!p.free(4) && !p.free(P::kInvalid) && !p.free(0xFFFE));  // out of range, kInvalid, internal sentinel
  CHECK(p.get(P::kInvalid) == nullptr && p.get(h[2]) == nullptr && p.get(4) == nullptr);
  CHECK(p.size() == 3 && p.counters().high == 4);
  return 0;
}

static int testArena() {
  alignas(16) uint8_t buf[64];
  pge::Arena a;
  a.init(buf, sizeof buf);
  void* p1 = a.alloc(1);
  CHECK(p1 == buf);
  void* p2 = a.alloc(4, 16);
  CHECK(p2 && (reinterpret_cast<uintptr_t>(p2) & 15) == 0 && p2 == buf + 16);
  CHECK(a.alloc(8, 3) == nullptr && a.failed() == 1);  // align not a power of two
  CHECK(a.alloc(0) == nullptr && a.failed() == 2);
  CHECK(a.alloc(SIZE_MAX) == nullptr && a.failed() == 3);  // overflow-safe
  CHECK(a.alloc(100) == nullptr && a.failed() == 4);       // exhausted
  CHECK(a.allocArray<uint32_t>(SIZE_MAX / 2) == nullptr && a.failed() == 5);
  size_t hw = a.highWater();
  CHECK(hw == 20 && a.used() == 20 && a.capacity() == 64);
  a.reset();
  CHECK(a.used() == 0 && a.highWater() == hw && a.failed() == 5);
  CHECK(a.allocArray<uint32_t>(16) == reinterpret_cast<uint32_t*>(buf));  // fills it exactly
  CHECK(a.alloc(1) == nullptr);
  a.reset();
  a.lock();
  CHECK(a.alloc(1) == nullptr && a.failed() == 7);
  return 0;
}

static int testTimestep() {
  pge::Timestep t;
  t.init(60, 5);
  CHECK(t.periodUs() == 16666);
  uint32_t total = 0;
  for (int i = 0; i < 1000; i++) total += t.advance(1000);
  CHECK(total == 60);
  t.reset();
  CHECK(t.advance(16665) == 0 && t.usUntilNext() == 1);
  CHECK(t.advance(1) == 1 && t.usUntilNext() == 16666);
  t.reset();
  CHECK(t.advance(10 * 16666 + 100) == 5);
  CHECK(t.dropped() == 5 && t.overruns() == 1 && t.usUntilNext() == 16666 - 100);
  uint32_t n = t.advance(0xFFFFFFFFu);  // clamped, no wrap
  CHECK(n == 5 && t.overruns() == 2 && t.dropped() == 5 + (0x7FFFFFFFu + 100) / 16666 - 5);
  return 0;
}

struct TestGame {
  pge::Engine* e = nullptr;
  pge::Error create_result = pge::Ok;
  int created = 0, destroyed = 0, ticks = 0, renders = 0;
  bool frame_dirty = false;
  pge::Error onCreate(pge::Engine& en) { created++; e = &en; return create_result; }
  void onTick(const pge::Input&) {
    ticks++;
    if (e->frameArena().used() != 0) frame_dirty = true;
    e->frameArena().alloc(16);
    if (ticks == 10) e->requestQuit();
  }
  void onRender(pge::Canvas&) { renders++; }
  void onDestroy() { destroyed++; }
};

static int testEngine() {
  TestGame g;
  CHECK(pge::run(g) == pge::NotInitialized && g.created == 0);
  CHECK(pge::init() == pge::Ok);
  CHECK(pge::init() == pge::Busy);

  CHECK(pge::run(g) == pge::Ok);
  CHECK(g.created == 1 && g.destroyed == 1 && g.ticks == 10 && !g.frame_dirty);
  CHECK(pge::engine().stats().ticks == 10 && pge::engine().tickCount() == 10);
  CHECK(pge::engine().initArena().alloc(8) == nullptr);  // locked after onCreate (K5)

  TestGame bad;
  bad.create_result = pge::Io;
  CHECK(pge::run(bad) == pge::Io && bad.created == 1 && bad.destroyed == 0 && bad.ticks == 0);
  return 0;
}

int main() {
  if (testPool() || testArena() || testTimestep() || testEngine()) return 1;
  std::printf("test_core OK\n");
  return 0;
}
