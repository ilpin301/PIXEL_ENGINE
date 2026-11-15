#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

uint32_t pge_hal_time_us(void);              /* monotonic, wraps at 2^32 us: subtract as uint32 */
void     pge_hal_time_delay_us(uint32_t us);

#ifdef __cplusplus
}
#endif
