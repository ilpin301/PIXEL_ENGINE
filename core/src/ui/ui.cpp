#include "pge/ui.h"

namespace pge {
namespace ui {

int formatU32(char* buf, uint8_t cap, uint32_t v) {
  char t[10];
  int n = 0;
  do { t[n++] = static_cast<char>('0' + v % 10); v /= 10; } while (v);
  if (!buf) return 0;
  if (cap < n + 1) {
    if (cap > 0) buf[0] = 0;
    return 0;
  }
  for (int i = 0; i < n; i++) buf[i] = t[n - 1 - i];
  buf[n] = 0;
  return n;
}

void label(Canvas& c, const Style& s, int x, int y, const char* text) {
  if (!s.font || !text) return;
  c.text(*s.font, x, y, text, s.fg);
}

void bar(Canvas& c, const Style& s, Rect r, uint16_t value, uint16_t max, Color fill) {
  if (r.w < 3 || r.h < 3 || !c.visible(r)) return;
  c.fillRect(r, s.bg);
  c.rect(r, s.fg);
  if (max == 0) return;
  const uint32_t v = value < max ? value : max;
  const int w = static_cast<int>(static_cast<uint32_t>(r.w - 2) * v / max);
  if (w > 0) c.fillRect(Rect{r.x + 1, r.y + 1, w, r.h - 2}, fill);
}

void iconGrid(Canvas& c, const Style& s, Rect area, const SpriteSheet& icons, const uint16_t* frames,
              const uint8_t* counts, uint8_t cols, uint8_t rows, int8_t selected) {
  if (!icons.px || !frames || cols == 0 || rows == 0) return;
  const int cw = icons.w + 2 * s.pad, ch = icons.h + 2 * s.pad;
  for (int row = 0; row < rows; row++)
    for (int col = 0; col < cols; col++) {
      const int i = row * cols + col;
      const Rect cell{area.x + col * cw, area.y + row * ch, cw, ch};
      if (!c.visible(cell)) continue;
      c.fillRect(cell, s.bg);
      c.rect(cell, s.fg);
      if (frames[i] != 0xFFFF) c.sprite(icons, frames[i], cell.x + s.pad, cell.y + s.pad, SpriteFlags::None);
      if (counts && counts[i] > 1 && s.font) {
        char b[4];
        formatU32(b, sizeof b, counts[i]);
        c.text(*s.font, cell.x + cw - 1 - textWidth(*s.font, b), cell.y + ch - s.font->h - 1, b, s.fg);
      }
      if (i == selected) c.rect(Rect{cell.x + 1, cell.y + 1, cw - 2, ch - 2}, s.accent);
    }
}

void listMenu(Canvas& c, const Style& s, Rect r, const char* const* items, uint8_t n, uint8_t selected) {
  if (!c.visible(r)) return;
  c.fillRect(r, s.bg);
  if (!items || n == 0 || !s.font) return;
  const int rowh = s.font->h + 2 * s.pad, fit = r.h / rowh;
  if (fit == 0) return;
  const int first = selected >= fit ? selected - fit + 1 : 0, end = first + fit < n ? first + fit : n;
  for (int i = first; i < end; i++) {
    const Rect row{r.x, r.y + (i - first) * rowh, r.w, rowh};
    const bool sel = i == selected;
    if (sel) c.fillRect(row, s.accent);
    c.text(*s.font, r.x + s.pad + 1, row.y + s.pad, items[i], sel ? s.bg : s.fg);
  }
}

int MenuNav::update(const Input& in, uint8_t n) {
  if (n == 0) { selected = 0; return -1; }
  if (selected >= n) selected = n - 1;
  if (in.pressed(Button::Up)) selected = selected == 0 ? n - 1 : selected - 1;
  if (in.pressed(Button::Down)) selected = (selected + 1) % n;
  return in.pressed(Button::A) || in.pressed(Button::Start) ? selected : -1;
}

}  // namespace ui
}  // namespace pge
