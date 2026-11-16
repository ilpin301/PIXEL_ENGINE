#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Stereo audio output. Samples are interleaved L,R int16. `fill` is called from the platform audio context (a thread
 * or an interrupt): it must be bounded and allocation-free. init may open the device but must not start playback.
 * Return 0 = ok, < 0 = pge_error values (-1 InvalidArg, -3 NotSupported, -4 Io).
 *
 *  init    sample_rate in Hz; a second call replaces the previous registration.
 *  start   playback begins, `fill` is called from now on.
 *  stop    playback pauses; the device stays open. */
typedef void (*pge_audio_fill_fn)(int16_t* stereo, uint32_t frames);

int pge_hal_audio_init(uint32_t sample_rate, pge_audio_fill_fn fill);
int pge_hal_audio_start(void);
int pge_hal_audio_stop(void);

#ifdef __cplusplus
}
#endif
