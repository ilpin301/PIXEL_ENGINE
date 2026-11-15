#pragma once
#include <stdint.h>

namespace pge {

// Fixed-timestep accumulator. Overrun policy: at most max_catchup ticks per advance(), the rest are dropped and counted.
class Timestep {
 public:
  void init(uint32_t hz, uint32_t max_catchup) {
    // shortcut: truncated period, 60 Hz runs 40 ppm fast, upgrade with a remainder carry if a port needs exact rate
    period_ = 1000000u / hz;
    max_ = max_catchup;
    reset();
  }

  // Adds dt_us and returns the ticks due now.
  uint32_t advance(uint32_t dt_us) {
    if (dt_us > 0x7FFFFFFFu) dt_us = 0x7FFFFFFFu;  // acc_ < period_ <= 1e6, so the sum cannot wrap
    acc_ += dt_us;
    uint32_t n = acc_ / period_;
    if (n > max_) {
      dropped_ += n - max_;
      overruns_++;
      n = max_;
      acc_ %= period_;  // keep phase
    } else {
      acc_ -= n * period_;
    }
    return n;
  }

  uint32_t usUntilNext() const { return period_ - acc_; }
  uint32_t periodUs() const { return period_; }
  uint32_t dropped() const { return dropped_; }
  uint32_t overruns() const { return overruns_; }
  void reset() { acc_ = dropped_ = overruns_ = 0; }

 private:
  uint32_t period_ = 1, max_ = 1, acc_ = 0, dropped_ = 0, overruns_ = 0;
};

}  // namespace pge
