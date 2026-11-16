// Host audio HAL on SDL2: one paused output device, 512-sample buffer, stereo S16.
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include "pge/hal/audio.h"

static SDL_AudioDeviceID g_dev;
static pge_audio_fill_fn g_fill;

static void SDLCALL onAudio(void*, Uint8* stream, int len) { g_fill((int16_t*)stream, (uint32_t)len / 4); }

extern "C" {

int pge_hal_audio_init(uint32_t sample_rate, pge_audio_fill_fn fill) {
  if (sample_rate == 0 || sample_rate > 0x7FFFFFFFu || !fill) return -1;
  if (g_dev) {  // closing waits for a running callback, so g_fill may change afterwards
    SDL_CloseAudioDevice(g_dev);
    g_dev = 0;
  }
  if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return -4;
  SDL_AudioSpec want = {}, have;
  want.freq = (int)sample_rate;
  want.format = AUDIO_S16SYS;
  want.channels = 2;
  want.samples = 512;
  want.callback = onAudio;
  g_fill = fill;
  g_dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);  // 0 = no change allowed: SDL converts to our spec
  return g_dev ? 0 : -4;
}

int pge_hal_audio_start(void) {
  if (!g_dev) return -4;
  SDL_PauseAudioDevice(g_dev, 0);
  return 0;
}

int pge_hal_audio_stop(void) {
  if (!g_dev) return -4;
  SDL_PauseAudioDevice(g_dev, 1);
  return 0;
}

}  // extern "C"
