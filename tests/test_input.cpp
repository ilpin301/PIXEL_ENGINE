// Input tests: button edges, axes and touch fed straight into Input::update (no HAL involved).
#include <cstdio>
#include <initializer_list>
#include "pge/engine.h"

#define CHECK(c)                                                          \
  do {                                                                    \
    if (!(c)) {                                                           \
      std::printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); \
      return 1;                                                           \
    }                                                                     \
  } while (0)

using namespace pge;

static pge_raw_input mk(uint32_t buttons, bool down = false, int x = 0, int y = 0) {
  pge_raw_input r = {};
  r.buttons = buttons;
  r.touch_down = down;
  r.touch_x = (int16_t)x;
  r.touch_y = (int16_t)y;
  return r;
}

static int testButtons() {
  Input in;
  CHECK(!in.held(Button::A) && !in.pressed(Button::A) && !in.released(Button::A));
  // A is down for ticks 1..3: pressed only in tick 1, released only in tick 4
  const uint32_t seq[] = {0, PGE_BTN_A, PGE_BTN_A, PGE_BTN_A, 0, 0};
  const bool held[] = {0, 1, 1, 1, 0, 0}, pressed[] = {0, 1, 0, 0, 0, 0}, released[] = {0, 0, 0, 0, 1, 0};
  for (int i = 0; i < 6; i++) {
    in.update(mk(seq[i]));
    if (in.held(Button::A) != held[i] || in.pressed(Button::A) != pressed[i] || in.released(Button::A) != released[i]) {
      std::printf("button step %d wrong\n", i);
      return 1;
    }
  }
  // two buttons are independent: A held across B's press / release
  in.update(mk(PGE_BTN_A));
  in.update(mk(PGE_BTN_A | PGE_BTN_B));
  CHECK(in.pressed(Button::B) && !in.pressed(Button::A) && in.held(Button::A) && in.held(Button::B));
  in.update(mk(PGE_BTN_A));
  CHECK(in.released(Button::B) && !in.released(Button::A) && in.held(Button::A) && !in.held(Button::B));
  in.update(mk(0));
  CHECK(in.released(Button::A) && !in.released(Button::B));

  // the QUIT bit and bits above 0xFF are never visible
  Input q;
  q.update(mk(PGE_INPUT_QUIT));
  for (uint32_t b : {PGE_BTN_UP, PGE_BTN_DOWN, PGE_BTN_LEFT, PGE_BTN_RIGHT, PGE_BTN_A, PGE_BTN_B, PGE_BTN_START,
                     PGE_BTN_SELECT}) {
    CHECK(!q.held(static_cast<Button>(b)) && !q.pressed(static_cast<Button>(b)));
  }
  q.update(mk(0xFFFFFF00u));
  CHECK(!q.held(Button::Up) && !q.held(Button::Select) && !q.pressed(Button::A));
  q.update(mk(0xFFFFFF00u | PGE_BTN_SELECT));
  CHECK(q.held(Button::Select) && q.pressed(Button::Select) && !q.held(Button::Start));
  return 0;
}

static int testAxes() {
  Input in;
  for (unsigned i = 0; i < 8; i++) CHECK(in.axis(i) == 0);
  pge_raw_input r = mk(0);
  r.axis[0] = -32768;
  r.axis[1] = 32767;
  r.axis[2] = -1;
  r.axis[3] = 300;
  in.update(r);
  CHECK(in.axis(0) == -32768 && in.axis(1) == 32767 && in.axis(2) == -1 && in.axis(3) == 300);
  for (unsigned i = 4; i < 8; i++) CHECK(in.axis(i) == 0);
  CHECK(in.axis(0xFFFFFFFFu) == 0);
  in.update(mk(0));  // axes are a snapshot of the latest poll
  CHECK(in.axis(0) == 0 && in.axis(1) == 0);
  return 0;
}

static int testTouch() {
  const int scales[] = {1, 2, 3};
  for (int s : scales) {
    Input in;
    in.update(mk(0), s);
    CHECK(!in.touching() && !in.touchPressed() && !in.touchReleased() && in.touchX() == 0 && in.touchY() == 0);
    in.update(mk(0, true, 100, 61), s);
    CHECK(in.touching() && in.touchPressed() && !in.touchReleased());
    CHECK(in.touchX() == 100 / s && in.touchY() == 61 / s);
    in.update(mk(0, true, 31, 90), s);  // moved while held
    CHECK(in.touching() && !in.touchPressed() && !in.touchReleased());
    CHECK(in.touchX() == 31 / s && in.touchY() == 90 / s);
    in.update(mk(0, false, 0, 0), s);  // release: the position reported while down stays
    CHECK(!in.touching() && !in.touchPressed() && in.touchReleased());
    CHECK(in.touchX() == 31 / s && in.touchY() == 90 / s);
    in.update(mk(0), s);
    CHECK(!in.touching() && !in.touchPressed() && !in.touchReleased());
    CHECK(in.touchX() == 31 / s && in.touchY() == 90 / s);
  }
  Input in;  // scale < 1 behaves as 1
  in.update(mk(0, true, 40, 20), 0);
  CHECK(in.touchX() == 40 && in.touchY() == 20);
  in.update(mk(0, true, 40, 20), -2);
  CHECK(in.touchX() == 40 && in.touchY() == 20);
  return 0;
}

int main() {
  if (testButtons() || testAxes() || testTouch()) return 1;
  std::printf("input OK\n");
  return 0;
}
