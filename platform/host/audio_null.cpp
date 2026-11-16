// Headless host audio HAL: no device; tests and benches drive the callback through pge_host_audio_pump.
#include "host_audio.h"
#include "pge/hal/audio.h"

static pge_audio_fill_fn g_fill;
static uint32_t g_rate;
static int g_running;

extern "C" {

int pge_hal_audio_init(uint32_t sample_rate, pge_audio_fill_fn fill) {
  if (sample_rate == 0 || !fill) return -1;
  g_rate = sample_rate;
  g_fill = fill;
  g_running = 0;
  return 0;
}

int pge_hal_audio_start(void) { g_running = 1; return 0; }
int pge_hal_audio_stop(void) { g_running = 0; return 0; }

int pge_host_audio_pump(int16_t* stereo, uint32_t frames) {
  if (!g_fill) return -1;
  g_fill(stereo, frames);
  return 0;
}

}  // extern "C"
