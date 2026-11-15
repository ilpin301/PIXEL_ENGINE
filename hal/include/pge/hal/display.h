#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef int pge_error;                      /* 0 = ok, see 02 section 5 */
typedef struct { uint16_t x, y, w, h; } pge_rect;

#define PGE_DISP_ASYNC      0x01  /* flush may return before the transfer ends */
#define PGE_DISP_TRANSPOSED 0x02  /* platform rotates internally (panel is portrait) */
#define PGE_DISP_VSYNC      0x04  /* two scan-out buffers: frame_end swaps and blocks until latched */
typedef struct { uint16_t panel_w, panel_h; uint32_t flags; } pge_display_info;

pge_error pge_hal_display_init(uint16_t want_w, uint16_t want_h, pge_display_info* out);
pge_error pge_hal_display_flush(pge_rect r, uint16_t* px);   /* px: r.w*r.h RGB565, row-major, stride r.w */
pge_error pge_hal_display_wait(void);                        /* returns when the last flush ended */
void      pge_hal_display_frame_begin(void);                 /* hint, may be empty */
void      pge_hal_display_frame_end(void);                   /* VSYNC set: buffer swap; else hint */

#ifdef __cplusplus
}
#endif
