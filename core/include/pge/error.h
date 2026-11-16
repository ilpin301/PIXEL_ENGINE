#pragma once

namespace pge {

enum Error : int {
  Ok = 0,
  InvalidArg = -1,
  OutOfMemory = -2,
  NotSupported = -3,
  Io = -4,
  Timeout = -5,
  Busy = -6,
  Corrupt = -7,
  NotInitialized = -8
};

}  // namespace pge

// Always-on trust-boundary guard: returns `err` from the calling function (no fatal HAL exists yet).
#define PGE_CHECK(cond, err) do { if (!(cond)) return (err); } while (0)
