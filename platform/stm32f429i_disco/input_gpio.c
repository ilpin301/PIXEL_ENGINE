/* hal/input.h on the STM32F429I-DISCO: USER button, external buttons and a KY-023 analog stick.
 * Pins (active level):  PA0 USER (high, board pulldown) | PC11 fire, PC12 use, PD2 prev weapon, PD4 next weapon,
 * PD5 menu, PB7 stick switch (all low, internal pull-up) | stick VRy PC3 = ADC1_IN13 (horizontal),
 * VRx PA5 = ADC1_IN5 (vertical).  Unconnected stick pins float: the centre and the direction bits are then noise.
 * Mapping: A = PC11 | PA0, B = PC12, SELECT = PD2, START = PD5.  PD4 and PB7 are read but unmapped:
 * there is no free PGE_BTN_* bit.  touch_down stays 0 (touch comes in S4.3); PGE_INPUT_QUIT is never set.
 * Axis sign (to verify on the board): axis = ADC reading minus the centre taken at init, scaled to int16, so
 * axis[0] > 0 = RIGHT and axis[1] > 0 = DOWN if the voltage rises that way; flip the sign here if not. */
#include "pge/hal/input.h"
#include "stm32f4xx_hal.h"

#define AXIS_DEAD 12000

static ADC_HandleTypeDef s_adc;
static int s_ready;
static int32_t s_centre[2] = {2048, 2048};

static uint32_t adc_read(uint32_t channel) {
  ADC_ChannelConfTypeDef ch = {0};
  ch.Channel = channel;
  ch.Rank = 1;
  ch.SamplingTime = ADC_SAMPLETIME_56CYCLES;
  if (HAL_ADC_ConfigChannel(&s_adc, &ch) != HAL_OK) return 2048;
  HAL_ADC_Start(&s_adc);
  uint32_t v = 2048;
  if (HAL_ADC_PollForConversion(&s_adc, 10) == HAL_OK) v = HAL_ADC_GetValue(&s_adc);
  HAL_ADC_Stop(&s_adc);
  return v;
}

static int16_t scale(uint32_t v, int32_t centre) {
  int32_t d = (int32_t)v - centre;
  int32_t span = d >= 0 ? 4095 - centre : centre;   /* full travel on this side of the centre */
  if (span < 1) return 0;
  d = d * 32767 / span;
  return (int16_t)(d > 32767 ? 32767 : d < -32768 ? -32768 : d);
}

int pge_hal_input_init(void) {
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_ADC1_CLK_ENABLE();

  GPIO_InitTypeDef g = {0};
  g.Mode = GPIO_MODE_INPUT;
  g.Pull = GPIO_NOPULL;
  g.Pin = GPIO_PIN_0;
  HAL_GPIO_Init(GPIOA, &g);
  g.Pull = GPIO_PULLUP;
  g.Pin = GPIO_PIN_7;
  HAL_GPIO_Init(GPIOB, &g);
  g.Pin = GPIO_PIN_11 | GPIO_PIN_12;
  HAL_GPIO_Init(GPIOC, &g);
  g.Pin = GPIO_PIN_2 | GPIO_PIN_4 | GPIO_PIN_5;
  HAL_GPIO_Init(GPIOD, &g);
  g.Mode = GPIO_MODE_ANALOG;
  g.Pull = GPIO_NOPULL;
  g.Pin = GPIO_PIN_5;
  HAL_GPIO_Init(GPIOA, &g);
  g.Pin = GPIO_PIN_3;
  HAL_GPIO_Init(GPIOC, &g);

  s_adc.Instance = ADC1;
  s_adc.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;   /* 90 MHz PCLK2 / 4 = 22.5 MHz */
  s_adc.Init.Resolution = ADC_RESOLUTION_12B;
  s_adc.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  s_adc.Init.ScanConvMode = DISABLE;
  s_adc.Init.ContinuousConvMode = DISABLE;
  s_adc.Init.DiscontinuousConvMode = DISABLE;
  s_adc.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  s_adc.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  s_adc.Init.NbrOfConversion = 1;
  s_adc.Init.DMAContinuousRequests = DISABLE;
  s_adc.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  if (HAL_ADC_Init(&s_adc) != HAL_OK) return -1;

  uint32_t sum0 = 0, sum1 = 0;
  for (int i = 0; i < 16; i++) {
    sum0 += adc_read(ADC_CHANNEL_13);
    sum1 += adc_read(ADC_CHANNEL_5);
  }
  s_centre[0] = (int32_t)(sum0 / 16u);
  s_centre[1] = (int32_t)(sum1 / 16u);
  s_ready = 1;
  return 0;
}

int pge_hal_input_poll(pge_raw_input* out) {
  if (!out || !s_ready) return -1;
  uint32_t b = 0;
  if (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_11) == GPIO_PIN_RESET || HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_SET)
    b |= PGE_BTN_A;
  if (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_12) == GPIO_PIN_RESET) b |= PGE_BTN_B;
  if (HAL_GPIO_ReadPin(GPIOD, GPIO_PIN_2) == GPIO_PIN_RESET) b |= PGE_BTN_SELECT;
  if (HAL_GPIO_ReadPin(GPIOD, GPIO_PIN_5) == GPIO_PIN_RESET) b |= PGE_BTN_START;

  int16_t ax = scale(adc_read(ADC_CHANNEL_13), s_centre[0]);
  int16_t ay = scale(adc_read(ADC_CHANNEL_5), s_centre[1]);
  if (ax > AXIS_DEAD) b |= PGE_BTN_RIGHT;
  if (ax < -AXIS_DEAD) b |= PGE_BTN_LEFT;
  if (ay > AXIS_DEAD) b |= PGE_BTN_DOWN;
  if (ay < -AXIS_DEAD) b |= PGE_BTN_UP;

  out->buttons = b;
  out->axis[0] = ax;
  out->axis[1] = ay;
  out->axis[2] = 0;
  out->axis[3] = 0;
  out->touch_down = 0;
  out->touch_x = 0;
  out->touch_y = 0;
  return 0;
}
