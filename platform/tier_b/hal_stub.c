/* Tier B skeleton: stubs only, no hardware access, untested. Copy to platform/<board>/ and implement. */
#include <stddef.h>
#include "pge/hal/display.h"
#include "pge/hal/input.h"
#include "pge/hal/time.h"
#include "pge/hal/audio.h"
#include "pge/hal/storage.h"
#include "pge_profile.h"

#define PGE_ERR_NOTSUPPORTED (-3)

/* A real port brings up the panel and reports its size and flags (ASYNC/TRANSPOSED/VSYNC) in *out. */
pge_error pge_hal_display_init(uint16_t want_w, uint16_t want_h, pge_display_info* out) {
  (void)want_w; (void)want_h;
  if (out) { out->panel_w = PGE_LOGICAL_W; out->panel_h = PGE_LOGICAL_H; out->flags = 0; }
  return PGE_ERR_NOTSUPPORTED;
}

/* A real port sends r.w*r.h RGB565 pixels (row-major, stride r.w) to the panel at rect r. */
pge_error pge_hal_display_flush(pge_rect r, uint16_t* px) {
  (void)r; (void)px;
  return PGE_ERR_NOTSUPPORTED;
}

/* A real port blocks here until the last flush has ended (nothing to wait for in a stub). */
pge_error pge_hal_display_wait(void) { return 0; }

/* A real port may use this as a hint at the start of a frame; empty is allowed. */
void pge_hal_display_frame_begin(void) {}

/* A real port swaps buffers here when VSYNC is set; otherwise it is a hint. */
void pge_hal_display_frame_end(void) {}

/* A real port sets up buttons, axes and touch here. */
int pge_hal_input_init(void) { return PGE_ERR_NOTSUPPORTED; }

/* A real port fills *out with buttons (PGE_BTN_* bits), axes and touch state in display pixels. */
int pge_hal_input_poll(pge_raw_input* out) {
  (void)out;
  return PGE_ERR_NOTSUPPORTED;
}

/* A real port returns a monotonic microsecond counter that wraps at 2^32. */
uint32_t pge_hal_time_us(void) { return 0; }

/* A real port busy-waits or sleeps for the given number of microseconds. */
void pge_hal_time_delay_us(uint32_t us) { (void)us; }

/* A real port opens the audio device at sample_rate and registers fill without starting playback. */
int pge_hal_audio_init(uint32_t sample_rate, pge_audio_fill_fn fill) {
  (void)sample_rate; (void)fill;
  return PGE_ERR_NOTSUPPORTED;
}

/* A real port starts playback so that fill is called from the audio context. */
int pge_hal_audio_start(void) { return PGE_ERR_NOTSUPPORTED; }

/* A real port pauses playback; the device stays open. */
int pge_hal_audio_stop(void) { return PGE_ERR_NOTSUPPORTED; }

/* A real port mounts the file storage here. */
int pge_hal_storage_mount(void) { return PGE_ERR_NOTSUPPORTED; }

/* A real port opens path (mode 0 = read, 1 = write-truncate) and writes the handle to *out. */
int pge_hal_storage_open(const char* path, int mode, pge_file* out) {
  (void)path; (void)mode;
  if (out) *out = -1;
  return PGE_ERR_NOTSUPPORTED;
}

/* A real port returns the number of bytes read (0 = end of file), < 0 on error. */
int32_t pge_hal_storage_read(pge_file f, void* buf, uint32_t n) {
  (void)f; (void)buf; (void)n;
  return PGE_ERR_NOTSUPPORTED;
}

/* A real port returns the number of bytes written, < 0 on error. */
int32_t pge_hal_storage_write(pge_file f, const void* buf, uint32_t n) {
  (void)f; (void)buf; (void)n;
  return PGE_ERR_NOTSUPPORTED;
}

/* A real port closes the file handle. */
int pge_hal_storage_close(pge_file f) {
  (void)f;
  return PGE_ERR_NOTSUPPORTED;
}

/* A real port renames from to to; to must not exist (not an atomic replace). */
int pge_hal_storage_rename(const char* from, const char* to) {
  (void)from; (void)to;
  return PGE_ERR_NOTSUPPORTED;
}

/* A real port removes the file at path; Io if it does not exist or cannot be removed. */
int pge_hal_storage_remove(const char* path) {
  (void)path;
  return PGE_ERR_NOTSUPPORTED;
}
