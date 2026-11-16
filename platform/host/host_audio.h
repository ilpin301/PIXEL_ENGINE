#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Null audio backend only: runs the registered fill callback once on the caller's thread (tests, benches).
 * Returns 0, or -1 if pge_hal_audio_init has not succeeded yet. */
int pge_host_audio_pump(int16_t* stereo, uint32_t frames);

#ifdef __cplusplus
}
#endif
