#pragma once
#include <stdint.h>
#include "pge/config.h"
#include "pge/error.h"
#include "pge/gfx.h"
#include "pge/memory.h"
#include "pge/timestep.h"
#include "pge/hal/input.h"
#include "pge/hal/time.h"

namespace pge {

struct Stats {
  uint32_t tick_us;       // last onTick duration
  uint32_t tick_us_max;   // longest onTick seen
  uint32_t frame_us;      // last render pass (renderFrame, all bands)
  uint32_t band_us;       // sum of onRender time over the bands of the last frame
  uint32_t band_us_max;   // largest single-band onRender time seen
  uint32_t flush_us;      // frame_us - band_us: pre-clear + flush + overhead (renderFrame is not instrumented)
  uint32_t ticks;         // ticks run
  uint32_t ticks_dropped; // ticks skipped by the overrun policy
  uint32_t tick_overruns; // loop passes that hit PGE_MAX_CATCHUP
  uint16_t fps;           // frames rendered in the last full second
  uint16_t draw_dropped;  // invalid-argument draw calls in the last frame (saturating)
  uint16_t pool_high[PGE_POOL_KINDS];
  uint16_t pool_full[PGE_POOL_KINDS];
};

enum class Button : uint32_t {
  Up = PGE_BTN_UP, Down = PGE_BTN_DOWN, Left = PGE_BTN_LEFT, Right = PGE_BTN_RIGHT,
  A = PGE_BTN_A, B = PGE_BTN_B, Start = PGE_BTN_START, Select = PGE_BTN_SELECT
};

// Buttons only; axes, touch and mapping come in S4.3. Edges are per tick.
class Input {
 public:
  bool held(Button b) const { return (cur_ & static_cast<uint32_t>(b)) != 0; }
  bool pressed(Button b) const { return (cur_ & ~prev_ & static_cast<uint32_t>(b)) != 0; }
  bool released(Button b) const { return (prev_ & ~cur_ & static_cast<uint32_t>(b)) != 0; }

 private:
  friend class Engine;
  void update(uint32_t raw) { prev_ = cur_; cur_ = raw & 0xFFu; }
  uint32_t cur_ = 0, prev_ = 0;
};

class Engine {
 public:
  void requestQuit() { quit_ = true; }
  bool quitRequested() const { return quit_; }
  uint32_t tickCount() const { return ticks_; }
  Arena& initArena() { return init_; }
  Arena& frameArena() { return frame_; }
  const Stats& stats();  // refreshes the pool fields from the tracked pools
  void trackPool(unsigned kind, const PoolCounters* c) { if (kind < PGE_POOL_KINDS) pools_[kind] = c; }
  uint32_t seed() const;  // per frame (not per band), a function of the tick count

  // Internal: used by init() and run<G>().
  Error setup();
  bool ready() const { return ready_; }
  const Input& input() const { return input_; }
  void beginRun();       // clears the quit flag and stats, locks the init arena (K5), starts the clock
  uint32_t advanceClock();  // ticks due now
  void pace();           // sleeps until the next tick is due
  void pollInput();
  void recordTick(uint32_t us);
  int render(RenderFn fn, void* ctx);
  void endFrame();

 private:
  Arena init_, frame_;
  Input input_;
  Stats stats_ = {};
  Timestep step_;
  const PoolCounters* pools_[PGE_POOL_KINDS] = {};
  Color* band_ = nullptr;
  Color* stage_ = nullptr;
  uint32_t stage_pixels_ = 0, drops_ = 0;
  uint32_t band_pixels_ = 0, ticks_ = 0, last_us_ = 0, frame_t0_ = 0, band_acc_ = 0, sec_t0_ = 0, sec_frames_ = 0;
  uint16_t band_lines_ = 0;
  bool ready_ = false, quit_ = false;
  struct BandCtx { RenderFn fn; void* ctx; Engine* e; };
  static void bandTrampoline(Canvas& c, void* p);  // times each onRender call
};

Engine& engine();
Error init();  // HAL display + input, arenas, band buffer; Busy if already done

namespace detail {
template <class G> auto destroy(G& g, int) -> decltype(g.onDestroy(), void()) { g.onDestroy(); }
template <class G> void destroy(G&, long) {}
template <class G> void renderTrampoline(Canvas& c, void* g) { static_cast<G*>(g)->onRender(c); }
}  // namespace detail

// G: Error onCreate(Engine&); void onTick(const Input&); void onRender(Canvas&); void onDestroy() (optional).
template <class G>
Error run(G& game) {
  Engine& e = engine();
  if (!e.ready()) return NotInitialized;
  Error err = game.onCreate(e);
  if (err != Ok) return err;
  e.beginRun();
  Error result = Ok;
  while (!e.quitRequested()) {
    uint32_t n = e.advanceClock();
    if (n == 0) { e.pace(); continue; }  // nothing due: sleep instead of rendering the same state again
    for (; n != 0 && !e.quitRequested(); n--) {
      e.frameArena().reset();
      e.pollInput();
      uint32_t t0 = pge_hal_time_us();
      game.onTick(e.input());
      e.recordTick(pge_hal_time_us() - t0);
    }
    if (e.quitRequested()) break;
    // Render at most once per loop pass, decoupled from the tick count (K4).
    int rc = e.render(detail::renderTrampoline<G>, &game);
    e.endFrame();
    if (rc != 0) { result = static_cast<Error>(rc); break; }
  }
  detail::destroy(game, 0);
  return result;
}

}  // namespace pge
