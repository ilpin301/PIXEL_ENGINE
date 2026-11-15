#pragma once
#include <stdint.h>

namespace pge {

// A frame sequence of a SpriteSheet: frames first .. first + count - 1, each shown for ticks_per_frame ticks.
struct Animation { uint16_t first, count, ticks_per_frame; bool loop; };

// Frame to draw `ticks` ticks after the animation started. A pure function of the tick count, so every band of
// one frame draws the same frame. Not looping: holds the last frame. count == 0 gives `first`;
// ticks_per_frame == 0 counts as 1.
inline uint16_t animFrame(const Animation& a, uint32_t ticks) {
  if (a.count == 0) return a.first;
  uint32_t step = ticks / (a.ticks_per_frame ? a.ticks_per_frame : 1u);
  if (a.loop) step %= a.count;
  else if (step >= a.count) step = a.count - 1u;
  return static_cast<uint16_t>(a.first + step);
}

}  // namespace pge
