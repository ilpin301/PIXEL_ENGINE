#pragma once
// key = value settings file (docs/architecture/03 section 4.3). Fixed table allocated once from the init arena.
#include <stdint.h>
#include "pge/error.h"
#include "pge/memory.h"
#include "pge/storage.h"

namespace pge {

class Settings {
 public:
  // max_keys 0 = PGE_CONFIG_MAX_KEYS. OutOfMemory if the arena fails.
  Error init(Arena& a, uint16_t max_keys = 0);
  // One "key = value" per line ('\n' or "\r\n"); spaces/tabs around key and value are trimmed; blank lines and lines
  // starting with '#' are ignored. Lines without '=', with an empty key, a key > 23 or a value > 31 chars, or that
  // do not fit into a full table are skipped and counted. A repeated key replaces its value. Never reads past len.
  void parse(const char* text, uint32_t len);
  // readFile into the caller's scratch, then parse. Returns readFile's error; settings are untouched on error.
  Error load(Storage& st, const char* path, void* scratch, uint32_t scratch_bytes);

  // Decimal (optional +/-) or 0x hex; anything else, int32 overflow or an empty value gives def.
  int32_t getInt(const char* key, int32_t def) const;
  const char* getStr(const char* key, const char* def) const;
  uint16_t count() const { return count_; }
  uint16_t skipped() const { return skipped_; }

 private:
  struct Entry;
  Entry* find(const char* key, uint32_t n) const;
  Entry* tab_ = nullptr;
  uint16_t cap_ = 0, count_ = 0, skipped_ = 0;
};

}  // namespace pge
