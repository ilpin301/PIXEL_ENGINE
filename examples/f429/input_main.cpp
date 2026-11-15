// pge_f429_input: input HAL test screen. 8 boxes (one per PGE_BTN_* bit, green = pressed) and two axis bars.
#include "pge/gfx.h"
#include "pge/hal/display.h"
#include "pge/hal/input.h"
#include "pge/hal/time.h"
#include "board.h"

namespace {

pge::Color g_band[320 * 16];
const pge::Color kGreen = 0x07E0, kGrey = 0x2104, kBar = 0xFFE0, kFrame = 0x7BEF;

struct State { uint32_t buttons; int16_t ax, ay; };

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
  uint32_t last_mask = 0xFFFFFFFFu, last_log = 0;
  for (;;) {
    pge_raw_input in;
    if (pge_hal_input_poll(&in) != 0) in = pge_raw_input{};
    State s = {in.buttons, in.axis[0], in.axis[1]};
    pge_board_led(4, in.buttons != 0);
    uint32_t now = pge_hal_time_us();
    if (in.buttons != last_mask && now - last_log >= 50000u) {   // <= 20 lines/s
      pge_dbg_printf("buttons=0x%02lx axis0=%d axis1=%d\n", (unsigned long)in.buttons, (int)in.axis[0],
                     (int)in.axis[1]);
      last_mask = in.buttons;
      last_log = now;
    }
    if (pge::renderFrame(cfg, g_band, 320 * 16, 0, render, &s) != 0) pge_board_led(3, 1);
  }
}
