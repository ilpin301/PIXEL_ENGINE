// pge_platformer [--auto] [--ticks N] [--shot out.bmp]: windowed host platformer through Engine + run<G>.
// Arrows / WASD move, Z or Up jumps. --auto runs the scripted bot, --ticks N quits after N ticks, --shot saves the
// window contents to a BMP after the loop.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "game.h"

extern "C" int pge_host_save_bmp(const char* path);

static PlatformerGame g_game;

int main(int argc, char** argv) {
  const char* shot = nullptr;
  for (int i = 1; i < argc; i++) {
    if (!std::strcmp(argv[i], "--auto")) g_game.autoplay = true;
    else if (i + 1 < argc && !std::strcmp(argv[i], "--ticks")) g_game.quit_after = (uint32_t)std::strtoul(argv[++i], nullptr, 10);
    else if (i + 1 < argc && !std::strcmp(argv[i], "--shot")) shot = argv[++i];
  }
  pge::Error err = pge::init();
  if (err != pge::Ok) { std::printf("init failed: %d\n", err); return 1; }
  err = pge::run(g_game);
  if (err != pge::Ok) { std::printf("run failed: %d\n", err); return 1; }
  if (shot && pge_host_save_bmp(shot) != 0) { std::printf("save_bmp failed\n"); return 1; }
  return 0;
}
