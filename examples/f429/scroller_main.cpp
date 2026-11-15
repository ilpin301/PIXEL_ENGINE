// pge_f429_scroller: the S4.3 scroller demo (examples/scroller/game.h) on the F429 board through Engine + run<G>.
// Buttons / KY-023 stick scroll, touch pulls the camera to the touched point. LED3 on init failure.
#include "pge/engine.h"
#include "board.h"
#include "game.h"

static ScrollerGame g_game;

int main() {
  pge_board_init();
  if (pge::init() != pge::Ok || pge::run(g_game) != pge::Ok) {
    pge_dbg_puts("# pge_f429_scroller init/run FAILED\n");
    pge_board_led(3, 1);
  }
  for (;;) {}
}
