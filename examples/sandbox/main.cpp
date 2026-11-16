// pge_sandbox [--overlay] [--seed N] [--ticks N] [--shot out.bmp] [--load] [--save PATH]: windowed host sandbox through
// Engine + run<G>. Arrows move, Z or Up jumps, WASD aims (none = the tile in front), X digs the aimed tile (it goes into
// the inventory; Down aims below, Up above the head, and Up without X also jumps), Enter places the selected hotbar tile there, Esc quits. The left mouse button digs the tile under the
// pointer (reach limit 64 px) or, on a hotbar cell, selects that slot. Select (Backspace) opens the menu (craft BRICK /
// FURNACE / METAL, SAVE, LOAD, OVERLAY): Up / Down choose, A or Start (Z / Enter) confirm, Left / Right pick the hotbar
// slot, B (X) or Select closes. --load loads the save at startup, --save PATH sets the save file (default sandbox.sav),
// --ticks N quits after N ticks, --shot saves the window contents to a BMP after the loop.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "game.h"

extern "C" int pge_host_save_bmp(const char* path);
extern "C" void pge_host_input_wasd_aim(int);

static SandboxGame g_game;

int main(int argc, char** argv) {
  const char* shot = nullptr;
  for (int i = 1; i < argc; i++) {
    if (!std::strcmp(argv[i], "--overlay")) g_game.overlay = true;
    else if (!std::strcmp(argv[i], "--load")) g_game.load_on_start = true;
    else if (i + 1 < argc && !std::strcmp(argv[i], "--save")) g_game.save_path = argv[++i];
    else if (i + 1 < argc && !std::strcmp(argv[i], "--seed")) g_game.seed = (uint32_t)std::strtoul(argv[++i], nullptr, 10);
    else if (i + 1 < argc && !std::strcmp(argv[i], "--ticks")) g_game.quit_after = (uint32_t)std::strtoul(argv[++i], nullptr, 10);
    else if (i + 1 < argc && !std::strcmp(argv[i], "--shot")) shot = argv[++i];
  }
  pge_host_input_wasd_aim(1);
  pge::Error err = pge::init();
  if (err != pge::Ok) { std::printf("init failed: %d\n", err); return 1; }
  err = pge::run(g_game);
  if (err != pge::Ok) { std::printf("run failed: %d\n", err); return 1; }
  if (shot && pge_host_save_bmp(shot) != 0) { std::printf("save_bmp failed\n"); return 1; }
  return 0;
}
