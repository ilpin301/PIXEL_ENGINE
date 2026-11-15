/* Board bring-up, time HAL, debug log, SDRAM self-test, fault handlers. */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "board.h"
#include "stm32f4xx_hal.h"
#include "stm32f429i_discovery.h"
#include "pge/hal/time.h"

/* ---- debug log ---------------------------------------------------------------------------- */
#define LOG_SIZE 32768u
#define LOG_START 8u   /* keeps the "PGELOG1\n" magic; the ring wraps back to here */
__attribute__((used)) char pge_dbg_log[LOG_SIZE] = "PGELOG1\n";
__attribute__((used)) volatile uint32_t pge_dbg_pos = LOG_START;
__attribute__((used)) volatile uint32_t pge_dbg_now_us;

void pge_dbg_puts(const char* s) {
  uint32_t p = pge_dbg_pos;
  for (; *s; s++) {
    pge_dbg_log[p++] = *s;
    if (p >= LOG_SIZE) p = LOG_START;
  }
  pge_dbg_log[p] = '\0';   /* terminator ahead of the write head; overwritten by the next char */
  pge_dbg_pos = p;
}

void pge_dbg_printf(const char* fmt, ...) {
  char buf[128];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  pge_dbg_puts(buf);
}

/* ---- time: wrapping microseconds from the 180 MHz DWT cycle counter ------------------------ */
static uint32_t s_last_cyc, s_rem_cyc, s_acc_us, s_cyc_per_us;

static void time_update(void) {   /* caller has interrupts masked, or is the SysTick handler */
  if (!s_cyc_per_us) return;
  uint32_t now = DWT->CYCCNT, d = now - s_last_cyc;
  s_last_cyc = now;
  s_rem_cyc += d;
  s_acc_us += s_rem_cyc / s_cyc_per_us;
  s_rem_cyc %= s_cyc_per_us;
}

uint32_t pge_hal_time_us(void) {
  uint32_t pm = __get_PRIMASK();
  __disable_irq();
  time_update();
  uint32_t us = s_acc_us;
  __set_PRIMASK(pm);
  return us;
}

void pge_hal_time_delay_us(uint32_t us) {
  uint32_t t0 = pge_hal_time_us();
  while ((uint32_t)(pge_hal_time_us() - t0) < us) {}
}

/* ---- interrupts --------------------------------------------------------------------------- */
void SysTick_Handler(void) {
  HAL_IncTick();
  time_update();
}

static void fault_spin(void) {
  BSP_LED_On(LED3);
  for (;;) {}
}
void NMI_Handler(void)        { fault_spin(); }
void HardFault_Handler(void)  { fault_spin(); }
void MemManage_Handler(void)  { fault_spin(); }
void BusFault_Handler(void)   { fault_spin(); }
void UsageFault_Handler(void) { fault_spin(); }

/* ---- bring-up ----------------------------------------------------------------------------- */
/* 180 MHz from 8 MHz HSE (same numbers as the Cube BSP example). */
static void clock_config(void) {
  RCC_OscInitTypeDef osc = {0};
  RCC_ClkInitTypeDef clk = {0};

  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  osc.HSEState = RCC_HSE_ON;
  osc.PLL.PLLState = RCC_PLL_ON;
  osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  osc.PLL.PLLM = 8;
  osc.PLL.PLLN = 360;
  osc.PLL.PLLP = RCC_PLLP_DIV2;
  osc.PLL.PLLQ = 7;
  if (HAL_RCC_OscConfig(&osc) != HAL_OK) fault_spin();
  if (HAL_PWREx_EnableOverDrive() != HAL_OK) fault_spin();

  clk.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
  clk.APB1CLKDivider = RCC_HCLK_DIV4;
  clk.APB2CLKDivider = RCC_HCLK_DIV2;
  if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_5) != HAL_OK) fault_spin();
}

void pge_board_init(void) {
  HAL_Init();
  clock_config();
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
  s_last_cyc = DWT->CYCCNT;
  s_cyc_per_us = SystemCoreClock / 1000000u;
  BSP_LED_Init(LED3);
  BSP_LED_Init(LED4);
}

uint32_t pge_board_sysclk_hz(void) { return SystemCoreClock; }
void pge_board_delay_ms(uint32_t ms) { HAL_Delay(ms); }
void pge_board_led(int which, int on) {
  Led_TypeDef led = which == 3 ? LED3 : LED4;
  if (on) BSP_LED_On(led); else BSP_LED_Off(led);
}

/* ---- SDRAM self-test ---------------------------------------------------------------------- */
#define SDRAM_TEST_BASE  0xD0100000u
#define SDRAM_TEST_WORDS (256u * 1024u / 4u)

int pge_sdram_selftest(void) {
  volatile uint32_t* m = (volatile uint32_t*)SDRAM_TEST_BASE;
  for (int pass = 0; pass < 2; pass++) {
    uint32_t inv = pass ? 0xFFFFFFFFu : 0u;
    for (uint32_t i = 0; i < SDRAM_TEST_WORDS; i++)
      m[i] = ((SDRAM_TEST_BASE + 4u * i) * 0x9E3779B1u) ^ inv;
    for (uint32_t i = 0; i < SDRAM_TEST_WORDS; i++) {
      uint32_t want = ((SDRAM_TEST_BASE + 4u * i) * 0x9E3779B1u) ^ inv;
      uint32_t got = m[i];
      if (got != want) {
        pge_dbg_printf("sdram FAIL pass=%d addr=0x%08lx want=0x%08lx got=0x%08lx\n", pass,
                       (unsigned long)(SDRAM_TEST_BASE + 4u * i), (unsigned long)want, (unsigned long)got);
        return -1;
      }
    }
  }
  pge_dbg_puts("sdram PASS 256KB@0xD0100000\n");
  return 0;
}
