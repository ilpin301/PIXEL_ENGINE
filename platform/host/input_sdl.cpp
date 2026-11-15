// Host input HAL on SDL2: keyboard -> buttons.
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include "pge/hal/input.h"

extern "C" int pge_host_quit_requested(void);

extern "C" {

int pge_hal_input_init(void) { return 0; }

int pge_hal_input_poll(pge_raw_input* out) {
  if (!out) return -1;
  SDL_PumpEvents();
  const Uint8* k = SDL_GetKeyboardState(nullptr);
  bool up = k[SDL_SCANCODE_UP] || k[SDL_SCANCODE_W];
  bool down = k[SDL_SCANCODE_DOWN] || k[SDL_SCANCODE_S];
  bool left = k[SDL_SCANCODE_LEFT] || k[SDL_SCANCODE_A];
  bool right = k[SDL_SCANCODE_RIGHT] || k[SDL_SCANCODE_D];
  uint32_t b = 0;
  if (up) b |= PGE_BTN_UP;
  if (down) b |= PGE_BTN_DOWN;
  if (left) b |= PGE_BTN_LEFT;
  if (right) b |= PGE_BTN_RIGHT;
  if (k[SDL_SCANCODE_Z] || k[SDL_SCANCODE_J]) b |= PGE_BTN_A;
  if (k[SDL_SCANCODE_X] || k[SDL_SCANCODE_K]) b |= PGE_BTN_B;
  if (k[SDL_SCANCODE_RETURN]) b |= PGE_BTN_START;
  if (k[SDL_SCANCODE_BACKSPACE] || k[SDL_SCANCODE_RSHIFT]) b |= PGE_BTN_SELECT;
  if (k[SDL_SCANCODE_ESCAPE] || pge_host_quit_requested()) b |= PGE_INPUT_QUIT;
  out->buttons = b;
  out->axis[0] = (int16_t)((right ? 32767 : 0) - (left ? 32767 : 0));
  out->axis[1] = (int16_t)((down ? 32767 : 0) - (up ? 32767 : 0));
  out->axis[2] = out->axis[3] = 0;
  out->touch_down = 0;
  out->touch_x = out->touch_y = 0;
  return 0;
}

}  // extern "C"
