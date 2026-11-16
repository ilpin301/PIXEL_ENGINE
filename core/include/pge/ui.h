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

}  // namespace ui
}  // namespace pge
