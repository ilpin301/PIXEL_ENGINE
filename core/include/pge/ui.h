#pragma once
#include <stdint.h>
#include "pge/engine.h"
#include "pge/gfx.h"

namespace pge {
namespace ui {

// Immediate-mode widgets: pure functions of their arguments, drawn from onRender (every band redraws them), no state.
struct Style { const Font* font; Color fg, bg, accent; uint8_t pad; };

// Decimal, NUL-terminated; returns the digit count, or 0 (and buf[0] = 0 if cap > 0) when buf / cap is too small.
int formatU32(char* buf, uint8_t cap, uint32_t v);
void label(Canvas& c, const Style& s, int x, int y, const char* text);  // fg text, transparent background
// 1 px fg outline, bg interior, `fill` for value/max of the interior width (value clamped to max; max 0 = empty).
void bar(Canvas& c, const Style& s, Rect r, uint16_t value, uint16_t max, Color fill);
// cols x rows cells of (icon + 2*pad) size; frames[i] == 0xFFFF is an empty slot, counts[i] > 1 (counts may be null)
// is printed bottom-right; selected < 0 selects nothing.
void iconGrid(Canvas& c, const Style& s, Rect area, const SpriteSheet& icons, const uint16_t* frames,
              const uint8_t* counts, uint8_t cols, uint8_t rows, int8_t selected);
// Rows of font->h + 2*pad; the list scrolls so that `selected` is the last visible row when it does not fit.
void listMenu(Canvas& c, const Style& s, Rect r, const char* const* items, uint8_t n, uint8_t selected);

struct MenuNav {
  uint8_t selected = 0;
  int update(const Input& in, uint8_t n);  // Up / Down wrap; returns the activated index (A or Start), else -1
};

constexpr uint16_t kNoIcon = 0xFFFF;  // Toast::icon: draw no icon
// text = id the game maps to a string at draw time, icon = SpriteSheet frame or kNoIcon, higher priority shows first,
// ticks = ticks left once visible.
struct Toast { uint16_t text; uint16_t icon; uint8_t priority; uint16_t ticks; };
// Maps a text id to a string (string table, fixed array, ...); may return nullptr for "no text".
using ToastText = const char* (*)(const void* ctx, uint16_t text);

// Notification queue, fixed storage, no heap. It never reads or consumes Input, so toasts never block input: tick() belongs
// in onTick, toasts() in onRender. Entries [0, visibleCount()) are visible, in the order they became visible; the rest wait,
// sorted by priority (highest first), first come first served within a priority.
class ToastQueue {
 public:
  static constexpr uint8_t kCap = 8;
  // Set before the first push; changing it with toasts queued may leave the waiting part unsorted.
  uint8_t max_visible = 3;
  // False (nothing changes, not counted as dropped) for ticks == 0. A toast with the same text id that is already queued is
  // refreshed instead (icon, timer restarted, priority = the higher one): a message raised every tick must not flood the
  // queue. A waiting one moves only when its priority rises (behind the waiting ones of that priority); otherwise it keeps
  // its place in line. A merge needs no room, so it works on a full queue too. A full queue drops the new toast, or the last
  // waiting one if that has a lower priority; visible toasts are never displaced. Both count in dropped().
  bool push(uint16_t text, uint16_t icon, uint8_t priority, uint16_t ticks);
  void tick();  // visible toasts count down and leave at 0, waiting ones move up; waiting ones do not count down
  uint8_t count() const { return n_; }
  uint8_t visibleCount() const { return n_ < max_visible ? n_ : max_visible; }
  const Toast& at(uint8_t i) const { return t_[i]; }  // i < count(); 0 = the oldest visible
  const Toast* find(uint16_t text) const;             // nullptr if not queued
  uint16_t dropped() const { return dropped_; }       // saturating
  void clear() { n_ = 0; }                            // dropped() is kept

 private:
  void insert(const Toast& t);
  Toast t_[kCap] = {};
  uint8_t n_ = 0;
  uint16_t dropped_ = 0;
};

// Draws the visible toasts as a stack growing upward: row height = max(font h, icon h) + 2*pad, the oldest is the bottom row
// and ends at y_bottom. Each row: bg, 1 px accent outline, icon (if icons and the toast has one), then fg text. Text and
// icon are not clipped to w; pad >= 1 keeps them off the outline.
void toasts(Canvas& c, const Style& s, int x, int y_bottom, int w, const ToastQueue& q, const SpriteSheet* icons,
            ToastText text, const void* ctx);

}  // namespace ui
}  // namespace pge
