#pragma once
// Storage layer over the C HAL (pge/hal/storage.h): small whole-file I/O plus the streamed, CRC-checked, two-slot save
// file (docs/architecture/03 section 4). The payload is never held whole in RAM; everything goes through one I/O buffer.
#include <stdint.h>
#include "pge/error.h"
#include "pge/memory.h"

namespace pge {

// CRC-32/IEEE (reflected, poly 0xEDB88320), chainable: start with 0. crc32Update(0, "123456789", 9) == 0xCBF43926.
uint32_t crc32Update(uint32_t crc, const void* data, uint32_t n);

// Producer fills dst with payload bytes [offset, offset + *written); *written = 0 ends the payload, *written <= max.
// It is called twice per save with the same results (the state it reads must not change during saveAtomic).
typedef Error (*Producer)(void* ctx, uint32_t offset, void* dst, uint32_t max, uint32_t* written);
// Called with ascending offsets after the whole payload passed its CRC check.
typedef Error (*Consumer)(void* ctx, uint32_t offset, const void* src, uint32_t n);

struct SaveInfo {
  uint32_t world_seed, game_tick;
  uint16_t w_chunks, h_chunks;
};

class Storage {
 public:
  // Allocates the I/O buffer (0 = PGE_STORAGE_IO_BYTES). OutOfMemory if the arena fails.
  Error init(Arena& a, uint32_t io_bytes = 0);
  // Uses a caller buffer instead (tests use 1, 7, 33 bytes); bytes >= 1.
  void setBuffer(void* buf, uint32_t bytes);
  // Remembers success. Until it succeeds the other calls return NotSupported; without a buffer, NotInitialized.
  Error mount();

  // Whole small file. Longer than cap -> OutOfMemory; open failure -> Io.
  Error readFile(const char* path, void* buf, uint32_t cap, uint32_t& len);
  Error writeFile(const char* path, const void* data, uint32_t len);  // plain truncate + write
  // Writes path.tmp, verifies it by read-back, then swaps it in keeping the previous good file as path.bak.
  // Paths longer than PGE_PATH_MAX - 5 characters give InvalidArg. A non-deterministic producer gives Corrupt.
  Error saveAtomic(const char* path, const SaveInfo& info, Producer produce, void* ctx);
  // Tries path, then path.bak if path failed before consume was called. CRC is verified before the first consume.
  // A consume error is returned at once without fallback. info is filled before the first consume call.
  Error loadSave(const char* path, SaveInfo& info, Consumer consume, void* ctx);
  bool exists(const char* path) const;  // open for read succeeds

 private:
  Error ready() const;
  Error loadOne(const char* path, SaveInfo& info, Consumer consume, void* ctx, bool& consumed);

  uint8_t* buf_ = nullptr;
  uint32_t cap_ = 0;
  bool mounted_ = false;
};

}  // namespace pge
