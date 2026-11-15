// pge_f429_input: input HAL test screen. 8 boxes (one per PGE_BTN_* bit, green = pressed), two axis bars and touch:
// while touch_down a crosshair + 8x8 dot at touch_x/touch_y and a "touch x=.. y=.." line (S4.3, orientation to verify).
#include "pge/gfx.h"
#include "pge/hal/display.h"
#include "pge/hal/input.h"
#include "pge/hal/time.h"
#include "board.h"

namespace {

pge::Color g_band[320 * 16];
const pge::Color kGreen = 0x07E0, kGrey = 0x2104, kBar = 0xFFE0, kFrame = 0x7BEF, kCross = 0xF81F;

struct State { uint32_t buttons; int16_t ax, ay; uint8_t td; int16_t tx, ty; };

char* put_int(char* p, int v) {   // tiny itoa, v >= 0
  char t[6];
  int n = 0;
  do { t[n++] = char('0' + v % 10); v /= 10; } while (v);
  while (n) *p++ = t[--n];
  return p;
}

void bar(pge::Canvas& c, int y, int16_t v) {   // 240 px track, centre at x=160, +-32767 -> +-100 px
  c.fillRect({40, y, 240, 20}, kGrey);
  int len = v * 100 / 32767;
  c.fillRect({len >= 0 ? 160 : 160 + len, y + 2, len >= 0 ? len : -len, 16}, kBar);
  c.fillRect({159, y, 2, 20}, kFrame);
}

void render(pge::Canvas& c, void* ctx) {
  const State& s = *static_cast<const State*>(ctx);
  for (int i = 0; i < 8; i++)   // bit i of PGE_BTN_*: UP DOWN LEFT RIGHT A B START SELECT
    c.fillRect({20 + (i % 4) * 75, 20 + (i / 4) * 60, 60, 44}, (s.buttons >> i) & 1u ? kGreen : kGrey);
  bar(c, 160, s.ax);
  bar(c, 190, s.ay);
  if (s.td) {
    char line[32], *p = line;
    for (const char* q = "touch x="; *q;) *p++ = *q++;
    p = put_int(p, s.tx);
    for (const char* q = " y="; *q;) *p++ = *q++;
    p = put_int(p, s.ty);
    *p = 0;
    c.text(pge::kFont5x7, 20, 136, line, kBar);
    c.fillRect({0, s.ty, 320, 1}, kCross);
    c.fillRect({s.tx, 0, 1, 240}, kCross);
    c.fillRect({s.tx - 4, s.ty - 4, 8, 8}, kCross);
  }
}

}  // namespace

int main() {
  pge_board_init();
  pge_display_info info;
  if (pge_hal_display_init(320, 240, &info) != 0 || pge_hal_input_init() != 0) {
    pge_dbg_puts("# pge_f429_input init FAILED\n");
    pge_board_led(3, 1);
    for (;;) {}
  }
  pge_dbg_puts("# pge_f429_input boxes: UP DOWN LEFT RIGHT / A B START SELECT; bars: axis0 axis1\n");

  const pge::RenderConfig cfg = {320, 240, 16, 0};
  uint32_t last_mask = 0xFFFFFFFFu, last_log = 0, last_touch = 0xFFFFFFFFu, last_tlog = 0;
  for (;;) {
    pge_raw_input in;
    if (pge_hal_input_poll(&in) != 0) in = pge_raw_input{};
    State s = {in.buttons, in.axis[0], in.axis[1], in.touch_down, in.touch_x, in.touch_y};
    pge_board_led(4, in.buttons != 0);
    uint32_t now = pge_hal_time_us();
    if (in.buttons != last_mask && now - last_log >= 50000u) {   // <= 20 lines/s
      pge_dbg_printf("buttons=0x%02lx axis0=%d axis1=%d\n", (unsigned long)in.buttons, (int)in.axis[0],
                     (int)in.axis[1]);
      last_mask = in.buttons;
      last_log = now;
    }
    uint32_t tkey = in.touch_down ? 0x80000000u | (uint32_t)in.touch_x << 16 | (uint16_t)in.touch_y : 0;
    if (tkey != last_touch && now - last_tlog >= 50000u) {   // <= 20 lines/s
      pge_dbg_printf("touch=%d x=%d y=%d\n", (int)in.touch_down, (int)in.touch_x, (int)in.touch_y);
      last_touch = tkey;
      last_tlog = now;
    }
    if (pge::renderFrame(cfg, g_band, 320 * 16, 0, render, &s) != 0) pge_board_led(3, 1);
  }
}
