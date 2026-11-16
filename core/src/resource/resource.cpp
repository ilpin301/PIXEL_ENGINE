#include "pge/resource.h"
#include <string.h>

namespace pge {

void Resources::init(const AssetEntry* table, uint16_t count, Storage* storage) {
  table_ = table;
  count_ = table ? count : 0;
  storage_ = storage;
}

Error Resources::info(AssetId id, AssetInfo& out) const {
  if (id >= count_) return InvalidArg;
  const AssetEntry& e = table_[id];
  out.type = e.type;
  out.size = e.size;
  out.in_rom = e.rom != nullptr;
  return Ok;
}

const void* Resources::rom(AssetId id) const { return id < count_ ? table_[id].rom : nullptr; }

Error Resources::load(AssetId id, void* dst, uint32_t cap, uint32_t& len) {
  if (id >= count_) return InvalidArg;
  const AssetEntry& e = table_[id];
  if (!dst && e.size) return InvalidArg;
  if (e.rom) {
    if (cap < e.size) return OutOfMemory;
    if (e.size) memcpy(dst, e.rom, e.size);
    len = e.size;
    return Ok;
  }
  if (!storage_) return NotSupported;
  if (cap < e.size) return OutOfMemory;
  uint32_t got = 0;
  const Error r = storage_->readFile(e.path, dst, cap, got);
  if (r != Ok) return r;
  if (got != e.size) return Corrupt;
  len = got;
  return Ok;
}

Error Resources::loadPersistent(Arena& a, AssetId id, const void** out) {
  if (id >= count_ || !out) return InvalidArg;
  const AssetEntry& e = table_[id];
  if (e.rom) {
    *out = e.rom;
    return Ok;
  }
  if (!storage_) return NotSupported;
  void* p = a.alloc(e.size ? e.size : 1, 8);  // Arena rejects 0 bytes
  if (!p) return OutOfMemory;
  uint32_t len = 0;
  const Error r = load(id, p, e.size, len);
  if (r != Ok) return r;
  *out = p;
  return Ok;
}

}  // namespace pge
