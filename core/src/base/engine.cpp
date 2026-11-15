#include "pge/engine.h"
#include "pge/hal/display.h"

namespace pge {

namespace {
alignas(8) PGE_RAM_DMA uint8_t g_init_mem[PGE_INIT_ARENA_BYTES];
alignas(8) PGE_RAM_FAST uint8_t g_frame_mem[PGE_FRAME_ARENA_BYTES];
Engine g_engine;
}  // namespace

Engine& engine() { return g_engine; }

Error init() { return g_engine.setup(); }

Error Engine::setup() {
  if (ready_) return Busy;
  pge_display_info info;
  const int disp_w = PGE_LOGICAL_W * PGE_SCALE, disp_h = PGE_LOGICAL_H * PGE_SCALE;
  int rc = pge_hal_display_init(disp_w, disp_h, &info);
  if (rc != 0) return static_cast<Error>(rc);
  bool fits = (info.panel_w >= disp_w && info.panel_h >= disp_h) ||
              ((info.flags & PGE_DISP_TRANSPOSED) && info.panel_w >= disp_h && info.panel_h >= disp_w);
  if (!fits) return InvalidArg;
  rc = pge_hal_input_init();
  if (rc != 0) return static_cast<Error>(rc);
  init_.init(g_init_mem, sizeof g_init_mem);
  frame_.init(g_frame_mem, sizeof g_frame_mem);
  band_lines_ = PGE_BAND_LINES > PGE_LOGICAL_H ? PGE_LOGICAL_H : PGE_BAND_LINES;
  band_pixels_ = static_cast<uint32_t>(PGE_LOGICAL_W) * band_lines_;
  band_ = static_cast<Color*>(init_.alloc(band_pixels_ * sizeof(Color), 32));
  if (!band_) return OutOfMemory;
  if (PGE_SCALE > 1) {
    stage_pixels_ = band_lines_ * PGE_SCALE * PGE_LOGICAL_W * PGE_SCALE;
    stage_ = static_cast<Color*>(init_.alloc(stage_pixels_ * sizeof(Color), 32));
    if (!stage_) return OutOfMemory;
  }
  ready_ = true;
  return Ok;
}

void Engine::beginRun() {
  quit_ = false;
  stats_ = Stats{};
  ticks_ = 0;
  band_acc_ = 0;
  sec_frames_ = 0;
  init_.lock();
  step_.init(PGE_TICK_HZ, PGE_MAX_CATCHUP);
  last_us_ = sec_t0_ = pge_hal_time_us();
}

uint32_t Engine::advanceClock() {
  uint32_t now = pge_hal_time_us();
  uint32_t n = step_.advance(now - last_us_);  // unsigned: survives the 2^32 us wrap
  last_us_ = now;
  stats_.ticks_dropped = step_.dropped();
  stats_.tick_overruns = step_.overruns();
  return n;
}

void Engine::pace() {
  uint32_t us = step_.usUntilNext();
  pge_hal_time_delay_us(us > 100000u ? 100000u : us);  // overshoot is fine, the accumulator absorbs it
}

void Engine::pollInput() {
  pge_raw_input raw = {};
  if (pge_hal_input_poll(&raw) != 0) raw.buttons = 0;
  if (raw.buttons & PGE_INPUT_QUIT) quit_ = true;
  input_.update(raw.buttons);
}

void Engine::recordTick(uint32_t us) {
  stats_.tick_us = us;
  if (us > stats_.tick_us_max) stats_.tick_us_max = us;
  stats_.ticks = ++ticks_;
}

uint32_t Engine::seed() const {
  uint32_t h = ticks_ ^ static_cast<uint32_t>(PGE_GAME_SEED);
  h ^= h >> 16; h *= 0x85EBCA6Bu;
  h ^= h >> 13; h *= 0xC2B2AE35u;
  h ^= h >> 16;
  return h;
}

void Engine::bandTrampoline(Canvas& c, void* p) {
  BandCtx& b = *static_cast<BandCtx*>(p);
  uint32_t t0 = pge_hal_time_us();
  b.fn(c, b.ctx);
  b.e->drops_ += c.dropped();
  uint32_t us = pge_hal_time_us() - t0;
  b.e->band_acc_ += us;
  if (us > b.e->stats_.band_us_max) b.e->stats_.band_us_max = us;
}

int Engine::render(RenderFn fn, void* ctx) {
  frame_t0_ = pge_hal_time_us();
  band_acc_ = 0;
  drops_ = 0;
  BandCtx b = {fn, ctx, this};
  RenderConfig cfg = {PGE_LOGICAL_W, PGE_LOGICAL_H, band_lines_, PGE_CLEAR_COLOR,
                      PGE_SCALE, stage_, stage_pixels_};
  return renderFrame(cfg, band_, band_pixels_, seed(), bandTrampoline, &b);
}

void Engine::endFrame() {
  uint32_t now = pge_hal_time_us();
  stats_.frame_us = now - frame_t0_;
  stats_.band_us = band_acc_;
  stats_.draw_dropped = drops_ > 0xFFFF ? 0xFFFF : static_cast<uint16_t>(drops_);
  stats_.flush_us = stats_.frame_us > band_acc_ ? stats_.frame_us - band_acc_ : 0;
  sec_frames_++;
  if (now - sec_t0_ >= 1000000u) {
    stats_.fps = static_cast<uint16_t>(sec_frames_);
    sec_frames_ = 0;
    sec_t0_ = now;
  }
}

const Stats& Engine::stats() {
  for (unsigned i = 0; i < PGE_POOL_KINDS; i++) {
    stats_.pool_high[i] = pools_[i] ? pools_[i]->high : 0;
    stats_.pool_full[i] = pools_[i] ? pools_[i]->full : 0;
  }
  return stats_;
}

}  // namespace pge
