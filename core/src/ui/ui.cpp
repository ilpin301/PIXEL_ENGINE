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

bool ToastQueue::push(uint16_t text, uint16_t icon, uint8_t priority, uint16_t ticks) {
  if (ticks == 0) return false;
  Toast t{text, icon, priority, ticks};
  for (uint8_t i = 0; i < n_; i++) {
    if (t_[i].text != text) continue;
    if (t_[i].priority > priority) t.priority = t_[i].priority;
    if (i < visibleCount() || t.priority == t_[i].priority) {  // visible, or waiting with no rise: same place
      t_[i] = t;
    } else {  // waiting with a higher priority: out and back in at its new sorted place
      for (uint8_t j = i; j + 1 < n_; j++) t_[j] = t_[j + 1];
      n_--;
      insert(t);
    }
    return true;
  }
  if (n_ == kCap) {
    const bool displace = kCap - 1 >= visibleCount() && t_[kCap - 1].priority < priority;
    if (dropped_ != 0xFFFF) dropped_++;
    if (!displace) return false;
    n_--;  // the last waiting toast makes room
  }
  insert(t);
  return true;
}

// After the waiting toasts of equal priority, never before the visible ones.
void ToastQueue::insert(const Toast& t) {
  uint8_t i = visibleCount();
  while (i < n_ && t_[i].priority >= t.priority) i++;
  for (uint8_t j = n_; j > i; j--) t_[j] = t_[j - 1];
  t_[i] = t;
  n_++;
}

void ToastQueue::tick() {
  const uint8_t vc = visibleCount();  // before removals: a toast that moves up starts counting down on the next tick
  uint8_t w = 0;
  for (uint8_t i = 0; i < n_; i++)
    if (i >= vc || --t_[i].ticks != 0) t_[w++] = t_[i];
  n_ = w;
}

const Toast* ToastQueue::find(uint16_t text) const {
  for (uint8_t i = 0; i < n_; i++)
    if (t_[i].text == text) return &t_[i];
  return nullptr;
}

void toasts(Canvas& c, const Style& s, int x, int y_bottom, int w, const ToastQueue& q, const SpriteSheet* icons,
            ToastText text, const void* ctx) {
  const int fh = s.font ? s.font->h : 0, ih = icons ? icons->h : 0;
  const int rowh = (fh > ih ? fh : ih) + 2 * s.pad;
  for (uint8_t i = 0; i < q.visibleCount(); i++) {
    const Toast& t = q.at(i);
    const Rect row{x, y_bottom - (i + 1) * rowh, w, rowh};
    if (!c.visible(row)) continue;
    c.fillRect(row, s.bg);
    c.rect(row, s.accent);
    int tx = x + s.pad + 1;
    if (icons && t.icon != kNoIcon) {
      c.sprite(*icons, t.icon, tx, row.y + s.pad, SpriteFlags::None);
      tx += icons->w + s.pad;
    }
    const char* str = s.font && text ? text(ctx, t.text) : nullptr;
    if (str) c.text(*s.font, tx, row.y + s.pad, str, s.fg);
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
