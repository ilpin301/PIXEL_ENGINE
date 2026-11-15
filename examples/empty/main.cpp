// pge_empty [--seconds N]: engine loop with an empty game; prints timing and Stats (headless, no SDL).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "pge/engine.h"

static void printStats(const pge::Stats& s) {
  std::printf("tick_us=%u tick_us_max=%u frame_us=%u band_us=%u band_us_max=%u flush_us=%u ticks=%u dropped=%u overruns=%u fps=%u\n",
              s.tick_us, s.tick_us_max, s.frame_us, s.band_us, s.band_us_max, s.flush_us, s.ticks, s.ticks_dropped,
              s.tick_overruns, (unsigned)s.fps);
  std::printf("pool_high[0]=%u pool_full[0]=%u\n", (unsigned)s.pool_high[0], (unsigned)s.pool_full[0]);
}

class EmptyGame {
 public:
  explicit EmptyGame(uint32_t target_ticks) : target_(target_ticks) {}

  pge::Error onCreate(pge::Engine& e) {
    eng_ = &e;
    e.trackPool(0, &pool_.counters());
    return pge::Ok;
  }

  void onTick(const pge::Input&) {
    ticks_++;
    pge::Pool<uint32_t, 16>::Handle h[8];
    uint32_t n = 1 + ticks_ % 8;  // a few live handles per tick so the high-water mark moves
    for (uint32_t i = 0; i < n; i++) h[i] = pool_.alloc();
    for (uint32_t i = 0; i < n; i++) pool_.free(h[i]);
    if (ticks_ % PGE_TICK_HZ == 0) printStats(eng_->stats());
    if (ticks_ >= target_) eng_->requestQuit();
  }

  void onRender(pge::Canvas&) {}

  uint32_t ticks() const { return ticks_; }

 private:
  pge::Engine* eng_ = nullptr;
  pge::Pool<uint32_t, 16> pool_;
  uint32_t target_, ticks_ = 0;
};

int main(int argc, char** argv) {
  uint32_t seconds = 3;
  for (int i = 1; i + 1 < argc; i += 2)
    if (!std::strcmp(argv[i], "--seconds")) seconds = (uint32_t)std::strtoul(argv[i + 1], nullptr, 10);

  pge::Error err = pge::init();
  if (err != pge::Ok) { std::printf("init failed: %d\n", err); return 1; }
  EmptyGame game(seconds * PGE_TICK_HZ);
  uint32_t t0 = pge_hal_time_us();
  err = pge::run(game);
  uint32_t wall_us = pge_hal_time_us() - t0;
  if (err != pge::Ok) { std::printf("run failed: %d\n", err); return 1; }

  std::printf("tick_hz=%d ticks_run=%u ticks_expected=%u wall_ms=%u\n", PGE_TICK_HZ, game.ticks(),
              (unsigned)((uint64_t)wall_us * PGE_TICK_HZ / 1000000u), wall_us / 1000u);
  printStats(pge::engine().stats());
  return 0;
}
