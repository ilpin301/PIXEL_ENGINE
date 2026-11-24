// pge_planet [--seed N] [--char 24|32] [--core N] [--autoplay] [--upright] [--rotate] [--ticks N] [--shot out.bmp] [--load]
// [--save PATH]: windowed host planet through Engine + run<G>. Arrows walk (Left / Right) and aim (Up / Down), Up jumps
// and, held in the air, is the jetpack (Up still aims up too; WASD aims without jumping); hold Z for dig mode (no gravity,
// arrows move in 8 directions, auto-digs the rock ahead), X digs the aimed 2 x 2 tiles,
// a left click (touch) digs the 2 x 2 tiles around the clicked point, WASD aims (none = the zone in front), Enter saves,
// Backspace (Select) switches the character between 24 and 32 px tall, Esc quits. --char picks the character height
// (default 32), --core N sets the radius in tiles of the unbreakable core at the planet centre (default 6, 0 = none),
// --autoplay runs the scripted bot (it walks around the planet), --upright draws the character upright in
// the 4 gravity directions (default: it leans along the radial axis, drawn from pre-rotated frames), --rotate turns the
// whole world smoothly with the planet
// (upright character, no iris, --upright is ignored; physics and controls stay 4-way), --load loads the save at startup,
// --save PATH sets the save file (default planet.sav), --ticks N quits after N ticks, --shot saves the window contents to a
// BMP after the loop.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "game.h"

extern "C" int pge_host_save_bmp(const char* path);
extern "C" void pge_host_input_wasd_aim(int);

static PlanetGame g_game;
alignas(8) static uint8_t g_log_buf[PlanetGame::kChangeCap * 5 + 64];  // the planet-sized change log does not fit the init arena
static pge::Arena g_log_arena;

int main(int argc, char** argv) {
  const char* shot = nullptr;
  for (int i = 1; i < argc; i++) {
    if (!std::strcmp(argv[i], "--autoplay")) g_game.autoplay = true;
    else if (!std::strcmp(argv[i], "--upright")) g_game.tilt = false;
    else if (!std::strcmp(argv[i], "--rotate")) g_game.rotate = true;
    else if (!std::strcmp(argv[i], "--load")) g_game.load_on_start = true;
    else if (i + 1 < argc && !std::strcmp(argv[i], "--save")) g_game.save_path = argv[++i];
    else if (i + 1 < argc && !std::strcmp(argv[i], "--char")) g_game.char_h = std::atoi(argv[++i]) == 32 ? 32 : 24;
    else if (i + 1 < argc && !std::strcmp(argv[i], "--core")) g_game.core_tiles = std::atoi(argv[++i]);
    else if (i + 1 < argc && !std::strcmp(argv[i], "--seed")) g_game.seed = (uint32_t)std::strtoul(argv[++i], nullptr, 10);
    else if (i + 1 < argc && !std::strcmp(argv[i], "--ticks")) g_game.quit_after = (uint32_t)std::strtoul(argv[++i], nullptr, 10);
    else if (i + 1 < argc && !std::strcmp(argv[i], "--shot")) shot = argv[++i];
  }
  g_log_arena.init(g_log_buf, sizeof g_log_buf);
  g_game.log_arena = &g_log_arena;
  pge_host_input_wasd_aim(1);
  pge::Error err = pge::init();
  if (err != pge::Ok) { std::printf("init failed: %d\n", err); return 1; }
  err = pge::run(g_game);
  if (err != pge::Ok) { std::printf("run failed: %d\n", err); return 1; }
  if (shot && pge_host_save_bmp(shot) != 0) { std::printf("save_bmp failed\n"); return 1; }
  return 0;
}
