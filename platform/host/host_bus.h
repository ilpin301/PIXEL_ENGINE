#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Host-only slow-bus simulation. clock_hz 0 = off (default). */
void pge_host_bus_set(uint32_t clock_hz, uint32_t bits_per_clock);
/* Busy-waits for the time `bytes` would take on the simulated bus; no-op when off. */
void pge_host_bus_charge(uint32_t bytes);

#ifdef __cplusplus
}
#endif
