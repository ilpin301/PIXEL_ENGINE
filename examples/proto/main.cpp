// pge_proto_demo [--frames N] [--shot out.bmp] [--band L] [--bus-hz H]   (N = 0: until the window closes)
// Keys: arrows/WASD move the box, Z X Enter Backspace light the indicator, Esc quits.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "pge/gfx.h"
#include "pge/hal/display.h"
#include "pge/hal/input.h"
#include "pge/hal/time.h"
#include "host_bus.h"
#include "scene.h"

extern "C" int pge_host_pump(void);
extern "C" int pge_host_save_bmp(const char* path);

static pge::Color g_band[320 * 240];  // full height allowed

struct DemoCtx { scene::SceneCtx sc; uint32_t buttons; int px, py; };

// Pure function of ctx: renderFrame calls it once per band.
static void demoRender(pge::Canvas& c, void* p) {
  const DemoCtx& d = *static_cast<DemoCtx*>(p);
  scene::renderScene(c, d.sc.frame);
  static const uint32_t bits[8] = {PGE_BTN_UP, PGE_BTN_DOWN, PGE_BTN_LEFT, PGE_BTN_RIGHT,
                                   PGE_BTN_A, PGE_BTN_B, PGE_BTN_START, PGE_BTN_SELECT};
  for (int i = 0; i < 8; i++) {
    int x = 4 + i * 12;
    c.fillRect(pge::Rect{x, 4, 10, 10}, scene::rgb(25, 25, 25));
    c.fillRect(pge::Rect{x + 1, 5, 8, 8}, (d.buttons & bits[i]) ? scene::rgb(60, 255, 60) : scene::rgb(50, 50, 50));
  }
  c.sprite(scene::data().osprite, 0, d.px, d.py, pge::None);
}

int main(int argc, char** argv) {
  int frames = 0, band = 16;
  const char* shot = nullptr;
  for (int i = 1; i + 1 < argc; i += 2) {
    if (!std::strcmp(argv[i], "--frames")) frames = std::atoi(argv[i + 1]);
    else if (!std::strcmp(argv[i], "--shot")) shot = argv[i + 1];
    else if (!std::strcmp(argv[i], "--band")) band = std::atoi(argv[i + 1]);
    else if (!std::strcmp(argv[i], "--bus-hz")) pge_host_bus_set((uint32_t)std::strtoul(argv[i + 1], nullptr, 10), 1);
  }
  pge_display_info info;
  if (pge_hal_display_init(320, 240, &info) != 0) { std::printf("display init failed\n"); return 1; }
  if (pge_hal_input_init() != 0) { std::printf("input init failed\n"); return 1; }
  pge::RenderConfig cfg = {320, 240, (uint16_t)band, 0};
  uint32_t prev_buttons = 0, sec_start = pge_hal_time_us();
  int sec_frames = 0, px = 152, py = 112;
  for (int f = 0; !pge_host_pump(); f++) {
    pge_raw_input in;
    if (pge_hal_input_poll(&in) == 0) {
      if (in.buttons & PGE_INPUT_QUIT) break;
      if (in.buttons != prev_buttons) std::printf("buttons=0x%02x\n", (unsigned)in.buttons);
      prev_buttons = in.buttons;
      px += in.axis[0] / 8192;
      py += in.axis[1] / 8192;
      px = px < 0 ? 0 : px > 304 ? 304 : px;
      py = py < 0 ? 0 : py > 224 ? 224 : py;
    }
    DemoCtx ctx = {{f}, prev_buttons, px, py};
    int err = pge::renderFrame(cfg, g_band, 320 * 240, (uint32_t)f, demoRender, &ctx);
    if (err != 0) { std::printf("renderFrame failed: %d\n", err); return 1; }
    sec_frames++;
    if (pge_hal_time_us() - sec_start >= 1000000u) {
      std::printf("fps=%d\n", sec_frames);
      sec_frames = 0;
      sec_start += 1000000u;
    }
    if (frames && f + 1 >= frames) break;
  }
  if (shot && pge_host_save_bmp(shot) != 0) { std::printf("save_bmp failed\n"); return 1; }
  return 0;
}
