#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <type_traits>

namespace pge {

// Bump allocator over a caller-owned buffer. Never frees single blocks; reset() drops everything.
class Arena {
 public:
  void init(void* buf, size_t bytes) {
    base_ = static_cast<uint8_t*>(buf);
    cap_ = bytes;
    used_ = high_ = 0;
    failed_ = 0;
    locked_ = false;
  }

  // nullptr (and failed() + 1) when exhausted, locked, bytes == 0 or align is not a power of two.
  void* alloc(size_t bytes, size_t align = 8) {
    if (locked_ || bytes == 0 || align == 0 || (align & (align - 1)) != 0) return fail();
    size_t pad = (align - ((reinterpret_cast<uintptr_t>(base_) + used_) & (align - 1))) & (align - 1);
    if (pad > cap_ - used_ || bytes > cap_ - used_ - pad) return fail();
    void* p = base_ + used_ + pad;
    used_ += pad + bytes;
    if (used_ > high_) high_ = used_;
    return p;
  }

  template <class T>
  T* allocArray(size_t n) {
    if (n > SIZE_MAX / sizeof(T)) { fail(); return nullptr; }
    return static_cast<T*>(alloc(n * sizeof(T), alignof(T)));
  }

  void reset() { used_ = 0; }      // keeps high-water, failed count and lock
  void lock() { locked_ = true; }  // K5: no allocation after init; later alloc fails and is counted

  size_t used() const { return used_; }
  size_t capacity() const { return cap_; }
  size_t highWater() const { return high_; }
  uint32_t failed() const { return failed_; }

 private:
  void* fail() { failed_++; return nullptr; }
  uint8_t* base_ = nullptr;
  size_t cap_ = 0, used_ = 0, high_ = 0;
  uint32_t failed_ = 0;
  bool locked_ = false;
};

struct PoolCounters {
  uint16_t high = 0;  // high-water of live slots
  uint16_t full = 0;  // failed alloc() calls, saturates at 65535
};

// Fixed pool with an index free-list. Exhaustion returns kInvalid and counts; it never allocates.
// shortcut: no generation counter, a stale handle whose slot was reallocated is not detected; upgrade if games hold handles long.
template <class T, uint16_t N>
class Pool {
  static_assert(N >= 1 && N <= 65533, "N must leave room for the sentinels");
  static_assert(std::is_trivially_copyable<T>::value, "Pool<T> holds raw bytes");

 public:
  typedef uint16_t Handle;
  static constexpr Handle kInvalid = 0xFFFF;

  Pool() {
    for (uint16_t i = 0; i < N; i++) next_[i] = static_cast<uint16_t>(i + 1);
    next_[N - 1] = kInvalid;
  }

  Handle alloc() {
    if (head_ == kInvalid) {
      if (counters_.full != 0xFFFF) counters_.full++;
      return kInvalid;
    }
    Handle h = head_;
    head_ = next_[h];
    next_[h] = kLive;
    memset(mem_ + sizeof(T) * h, 0, sizeof(T));
    if (++size_ > counters_.high) counters_.high = size_;
    return h;
  }

  bool free(Handle h) {
    if (h >= N || next_[h] != kLive) return false;
    next_[h] = head_;
    head_ = h;
    size_--;
    return true;
  }

  T* get(Handle h) { return h < N && next_[h] == kLive ? reinterpret_cast<T*>(mem_ + sizeof(T) * h) : nullptr; }

  const T* get(Handle h) const { return h < N && next_[h] == kLive ? reinterpret_cast<const T*>(mem_ + sizeof(T) * h) : nullptr; }

  uint16_t size() const { return size_; }
  uint16_t capacity() const { return N; }
  const PoolCounters& counters() const { return counters_; }

 private:
  static constexpr uint16_t kLive = 0xFFFE;  // next_[] value of a live slot; free slots hold the next free index
  alignas(T) uint8_t mem_[sizeof(T) * N];
  uint16_t next_[N];
  Handle head_ = 0;
  uint16_t size_ = 0;
  PoolCounters counters_;
};

}  // namespace pge
