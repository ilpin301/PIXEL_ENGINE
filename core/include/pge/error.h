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
