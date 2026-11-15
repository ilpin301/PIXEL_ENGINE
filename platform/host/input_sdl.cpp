// Host input HAL on SDL2: keyboard -> buttons, left mouse button -> touch.
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include "pge/hal/input.h"

extern "C" int pge_host_quit_requested(void);
extern "C" int pge_host_window_to_display(int wx, int wy, int* dx, int* dy);

// mapping table per platform (S4.3); several rows may share a bit
static const struct { SDL_Scancode sc; uint32_t bit; } kKeys[] = {
  {SDL_SCANCODE_UP, PGE_BTN_UP},       {SDL_SCANCODE_W, PGE_BTN_UP},
  {SDL_SCANCODE_DOWN, PGE_BTN_DOWN},   {SDL_SCANCODE_S, PGE_BTN_DOWN},
  {SDL_SCANCODE_LEFT, PGE_BTN_LEFT},   {SDL_SCANCODE_A, PGE_BTN_LEFT},
  {SDL_SCANCODE_RIGHT, PGE_BTN_RIGHT}, {SDL_SCANCODE_D, PGE_BTN_RIGHT},
  {SDL_SCANCODE_Z, PGE_BTN_A},         {SDL_SCANCODE_J, PGE_BTN_A},
  {SDL_SCANCODE_X, PGE_BTN_B},         {SDL_SCANCODE_K, PGE_BTN_B},
  {SDL_SCANCODE_RETURN, PGE_BTN_START},
  {SDL_SCANCODE_BACKSPACE, PGE_BTN_SELECT}, {SDL_SCANCODE_RSHIFT, PGE_BTN_SELECT},
};

extern "C" {

int pge_hal_input_init(void) { return 0; }

int pge_hal_input_poll(pge_raw_input* out) {
  if (!out) return -1;
  SDL_PumpEvents();
  const Uint8* k = SDL_GetKeyboardState(nullptr);
  uint32_t b = 0;
  for (const auto& m : kKeys)
    if (k[m.sc]) b |= m.bit;
  if (k[SDL_SCANCODE_ESCAPE] || pge_host_quit_requested()) b |= PGE_INPUT_QUIT;
  out->buttons = b;
  out->axis[0] = (int16_t)(((b & PGE_BTN_RIGHT) ? 32767 : 0) - ((b & PGE_BTN_LEFT) ? 32767 : 0));
  out->axis[1] = (int16_t)(((b & PGE_BTN_DOWN) ? 32767 : 0) - ((b & PGE_BTN_UP) ? 32767 : 0));
  out->axis[2] = out->axis[3] = 0;
  out->touch_down = 0;
  out->touch_x = out->touch_y = 0;
  int mx, my;
  int dx, dy;
  if ((SDL_GetMouseState(&mx, &my) & SDL_BUTTON(SDL_BUTTON_LEFT)) && pge_host_window_to_display(mx, my, &dx, &dy) == 0) {
    out->touch_down = 1;
    out->touch_x = (int16_t)dx;
    out->touch_y = (int16_t)dy;
  }
  return 0;
}

}  // extern "C"
