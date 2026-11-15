#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define PGE_INPUT_QUIT 0x80000000u  /* set in buttons when the platform asks to quit */

#define PGE_BTN_UP     0x01
#define PGE_BTN_DOWN   0x02
#define PGE_BTN_LEFT   0x04
#define PGE_BTN_RIGHT  0x08
#define PGE_BTN_A      0x10
#define PGE_BTN_B      0x20
#define PGE_BTN_START  0x40
#define PGE_BTN_SELECT 0x80

/* touch_x/touch_y: DISPLAY pixel coordinates of the landscape plane that flush rects use (origin top-left);
 * touch_down is 1 while a finger / mouse button is down (x/y are 0 otherwise).
 * axis[]: -32768..32767, 0 = centre, axis[0] +right, axis[1] +down. */
typedef struct {
  uint32_t buttons;  /* PGE_BTN_* bits, plus PGE_INPUT_QUIT */
  int16_t axis[4];
  uint8_t touch_down;
  int16_t touch_x, touch_y;
} pge_raw_input;

int pge_hal_input_init(void);                 /* pge_error: 0 = ok */
int pge_hal_input_poll(pge_raw_input* out);   /* pge_error: 0 = ok */

#ifdef __cplusplus
}
#endif
