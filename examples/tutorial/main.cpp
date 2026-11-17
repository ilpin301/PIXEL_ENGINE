// pge_tutorial_stepN [--auto] [--ticks N] [--shot out.bmp]: shared host main of the Coin Catcher tutorial.
// Every step target compiles this file with its own stepN/ include directory, so stepN/game.h holds game code only.
// Arrows / WASD move, Enter = Start, Esc quits. --auto replaces the keyboard with a fixed input script (used for
// the screenshots), --ticks N quits after N ticks, --shot saves the window contents to a BMP after the loop.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "game.h"

extern "C" int pge_host_save_bmp(const char* path);

// A rectangular sweep over the screen: each entry holds its buttons until tick `until`, then the script repeats.
struct ScriptStep { uint32_t until, buttons; };
static const ScriptStep kScript[] = {
    {60, PGE_BTN_RIGHT}, {100, PGE_BTN_UP},   {220, PGE_BTN_LEFT}, {260, PGE_BTN_DOWN},
    {380, PGE_BTN_RIGHT}, {420, PGE_BTN_DOWN}, {540, PGE_BTN_LEFT}, {580, PGE_BTN_UP},
};

static uint32_t scripted(uint32_t tick) {
  tick %= kScript[sizeof kScript / sizeof kScript[0] - 1].until;
  for (const ScriptStep& s : kScript)
    if (tick < s.until) return s.buttons;
  return 0;
}

// Wraps the step's Game: counts ticks for --ticks and, with --auto, feeds it a scripted Input instead of the real one.
class Runner {
 public:
  uint32_t quit_after = 0;
  bool autoplay = false;

  pge::Error onCreate(pge::Engine& e) {
    eng_ = &e;
    return game_.onCreate(e);
  }
  void onTick(const pge::Input& in) {
    if (autoplay) {
      pge_raw_input raw = {};
      raw.buttons = scripted(ticks_);
      fake_.update(raw);
    }
    game_.onTick(autoplay ? fake_ : in);
    if (++ticks_ == quit_after) eng_->requestQuit();
  }
  void onRender(pge::Canvas& c) { game_.onRender(c); }

 private:
  Game game_;
  pge::Input fake_;
  pge::Engine* eng_ = nullptr;
  uint32_t ticks_ = 0;
};

static Runner g_runner;  // static: games may keep pointers into themselves after onCreate

int main(int argc, char** argv) {
  const char* shot = nullptr;
  for (int i = 1; i < argc; i++) {
    if (!std::strcmp(argv[i], "--auto")) g_runner.autoplay = true;
    else if (i + 1 < argc && !std::strcmp(argv[i], "--ticks")) g_runner.quit_after = (uint32_t)std::strtoul(argv[++i], nullptr, 10);
    else if (i + 1 < argc && !std::strcmp(argv[i], "--shot")) shot = argv[++i];
  }
  pge::Error err = pge::init();
  if (err != pge::Ok) { std::printf("init failed: %d\n", err); return 1; }
  err = pge::run(g_runner);
  if (err != pge::Ok) { std::printf("run failed: %d\n", err); return 1; }
  if (shot && pge_host_save_bmp(shot) != 0) { std::printf("save_bmp failed\n"); return 1; }
  return 0;
}
