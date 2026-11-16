#include "pge/debug.h"

namespace pge {

#if PGE_DEBUG_OVERLAY
namespace {

constexpr int kMaxLines = 16, kLineLen = 40;

struct Line {  // appends text / numbers, always NUL-terminated, silently truncated
  char* o;
  char* e;
  Line& s(const char* t) {
    while (*t && o < e) *o++ = *t++;
    *o = 0;
    return *this;
  }
  Line& u(uint32_t v) {
    char b[11];
    ui::formatU32(b, sizeof b, v);
    return s(b);
  }
};

struct Lines {
  char t[kMaxLines][kLineLen];
  int n = 0;
  Line add() {  // a full table keeps overwriting the last line
    char* p = t[n < kMaxLines ? n : kMaxLines - 1];
    if (n < kMaxLines) n++;
    p[0] = 0;
    return Line{p, p + kLineLen - 1};
  }
};

}  // namespace
#endif

void drawOverlay(Canvas& c, const ui::Style& s, const Stats& st, uint8_t flags) {
#if PGE_DEBUG_OVERLAY
  if (!s.font) return;
  Lines L;
  if (flags & kOverlayTiming) {
    L.add().s("FPS ").u(st.fps);
    L.add().s("TICK ").u(st.tick_us).s("/").u(st.tick_us_max).s(" US");
    L.add().s("FRAME ").u(st.frame_us).s(" US");
    L.add().s("BAND ").u(st.band_us).s("/").u(st.band_us_max).s(" US");
    L.add().s("FLUSH ").u(st.flush_us).s(" US");
    L.add().s("DROP ").u(st.draw_dropped).s(" ").u(st.ticks_dropped);
  }
  if (flags & kOverlayPools)
    for (unsigned k = 0; k < PGE_POOL_KINDS; k++)
      if (st.pool_high[k] > 0 || st.pool_full[k] > 0)
        L.add().s("P").u(k).s(" HI ").u(st.pool_high[k]).s(" FULL ").u(st.pool_full[k]);
  if (L.n == 0) return;
  const int pitch = s.font->h + 1;
  int wmax = 0;
  for (int i = 0; i < L.n; i++) {
    const int w = textWidth(*s.font, L.t[i]);
    if (w > wmax) wmax = w;
  }
  const Rect panel{0, 0, wmax + 2 * s.pad, L.n * pitch + 2 * s.pad};
  if (!c.visible(panel)) return;
  c.fillRect(panel, s.bg);
  for (int i = 0; i < L.n; i++) c.text(*s.font, s.pad, s.pad + i * pitch, L.t[i], s.fg);
#else
  (void)c; (void)s; (void)st; (void)flags;
#endif
}

}  // namespace pge
