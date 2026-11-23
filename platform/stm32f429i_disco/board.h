#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

void     pge_board_init(void);                  /* HAL, 180 MHz clock, DWT, SysTick, LEDs */
void     pge_board_init_ex(int usb48);          /* usb48 != 0: 168 MHz so PLLQ gives the 48 MHz the USB OTG FS PHY needs (180 MHz gives 51.4 MHz) */
uint32_t pge_board_sysclk_hz(void);
void     pge_board_delay_ms(uint32_t ms);       /* HAL_Delay */
void     pge_board_led(int which, int on);      /* which: 3 (red, error) or 4 (green, ok) */
int      pge_sdram_selftest(void);              /* 256 KB at 0xD0100000; 0 = pass, logs result */

/* Debug ring buffer in RAM for OpenOCD (no UART on this board): find the string "PGELOG1". */
extern char              pge_dbg_log[32768];
extern volatile uint32_t pge_dbg_pos;           /* next write offset */
extern volatile uint32_t pge_dbg_now_us;        /* updated by main each frame */
void pge_dbg_puts(const char* s);               /* any length; pge_dbg_printf truncates at 127 B */
void pge_dbg_printf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

/* Display options (display_ltdc.c). Call before pge_hal_display_init; may also be changed between inits.
 * dma2d: portrait (240x320) flush through DMA2D M2M, ignored in landscape. backbuf: second framebuffer + vsync swap. */
void pge_f429_set_opts(int dma2d, int backbuf);
void pge_f429_bench_fill(int use_dma2d, uint16_t color);   /* fill the whole 240x320 write framebuffer (micro benchmark) */

#ifdef __cplusplus
}
#endif
