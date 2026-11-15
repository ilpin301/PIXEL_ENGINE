// Host time + slow-bus simulation. Busy-wait (not sleep) is chosen for sub-ms accuracy;
// a slow-bus sim exists on the host only.
#include <chrono>
#include "host_bus.h"
#include "pge/hal/time.h"

typedef std::chrono::steady_clock Clock;

static uint32_t g_bus_hz, g_bus_bits;

static void spinNs(uint64_t ns) {
  Clock::time_point end = Clock::now() + std::chrono::nanoseconds(ns);
  while (Clock::now() < end) {}
}

extern "C" {

uint32_t pge_hal_time_us(void) {
  static const Clock::time_point t0 = Clock::now();
  return (uint32_t)std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - t0).count();
}

void pge_hal_time_delay_us(uint32_t us) { spinNs((uint64_t)us * 1000u); }

void pge_host_bus_set(uint32_t clock_hz, uint32_t bits_per_clock) {
  g_bus_hz = clock_hz;
  g_bus_bits = bits_per_clock;
}

void pge_host_bus_charge(uint32_t bytes) {
  if (g_bus_hz == 0 || g_bus_bits == 0) return;
  spinNs((uint64_t)bytes * 8u * 1000000000u / ((uint64_t)g_bus_hz * g_bus_bits));
}

}  // extern "C"
