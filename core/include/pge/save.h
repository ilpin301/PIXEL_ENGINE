#pragma once
// Save game (docs/architecture/03 section 4.2): world change log + opaque game blob, streamed through Storage.
// Payload = sections `u8 type, u8 flags (0), u32 len, body`; type 1 = world changes (always written), type 2 = game blob
// (only when non-empty). Unknown section types are skipped on load.
#include <stdint.h>
#include <string.h>
#include "pge/error.h"
#include "pge/storage.h"
#include "pge/world.h"

namespace pge {

// Window over a logical byte stream. Default constructed = counting mode (stores nothing). With (offset, dst, max) it
// stores only the stream bytes in [offset, offset + max); pos() still counts every byte put.
class ByteSink {
 public:
  ByteSink() : counting_(true) {}
  ByteSink(uint32_t offset, void* dst, uint32_t max) : dst_(static_cast<uint8_t*>(dst)), off_(offset), max_(max) {}

  void put(uint8_t b) {
    if (!counting_ && pos_ >= off_ && pos_ - off_ < max_) {
      dst_[pos_ - off_] = b;
      written_++;
    }
    pos_++;
  }
  void put16(uint16_t v) { put((uint8_t)v); put((uint8_t)(v >> 8)); }
  void put32(uint32_t v) { put16((uint16_t)v); put16((uint16_t)(v >> 16)); }
  void putVarint(uint32_t v) {  // LEB128
    while (v >= 0x80) { put((uint8_t)(v | 0x80)); v >>= 7; }
    put((uint8_t)v);
  }
  void putBytes(const void* p, uint32_t n) {
    if (!counting_) {
      const uint32_t a = pos_ > off_ ? pos_ : off_;
      uint32_t end = pos_ + n;
      if (end > off_ + max_) end = off_ + max_;
      if (a < end) {
        memcpy(dst_ + (a - off_), static_cast<const uint8_t*>(p) + (a - pos_), end - a);
        written_ += end - a;
      }
    }
    pos_ += n;
  }

  uint32_t pos() const { return pos_; }          // all bytes put, inside the window or not
  uint32_t written() const { return written_; }  // bytes stored into dst
  bool full() const { return !counting_ && pos_ >= off_ && pos_ - off_ >= max_; }  // encoders may stop early

 private:
  uint8_t* dst_ = nullptr;
  uint32_t off_ = 0, max_ = 0, pos_ = 0, written_ = 0;
  bool counting_ = false;
};

// Body of a type-1 section: per changed chunk `u16 chunk, varint n_runs`, runs `varint loc_delta, varint run_len, u8
// tile`. An empty log writes zero bytes. Returns early (Ok) once the sink is full.
Error writeChanges(const World& w, ByteSink& s);

// Byte-fed decoder of that body; fields may be split across feed() calls at any byte.
class ChangeReader {
 public:
  void begin(World& w);  // clears the world's change log
  Error feed(const uint8_t* p, uint32_t n);
  Error end();           // Corrupt if a chunk, run list or run is unfinished

 private:
  enum State : uint8_t { ChunkLo, ChunkHi, NRuns, Delta, RunLen, Tile };
  Error varintDone(uint32_t v);
  Error applyRun(uint8_t tile);

  World* w_ = nullptr;
  State st_ = ChunkLo;
  bool have_prev_ = false;
  uint8_t vbytes_ = 0;
  uint16_t chunk_ = 0, prev_chunk_ = 0, runs_left_ = 0, prev_end_ = 0, loc_ = 0, len_ = 0;
  uint32_t acc_ = 0;
};

// Saves the world's change log and an optional game blob (ignored when blob == nullptr or blob_len == 0).
Error saveGame(Storage& st, const char* path, const World& w, uint32_t game_tick, const void* blob, uint32_t blob_len);

// Loads a save into w, which must be the same world (seed and size in chunks) else InvalidArg and the world is
// untouched. After the call the caller must run ensureLoaded again. On ANY other error after the world was modified
// (Corrupt, OutOfMemory, consumer errors) the change log is cleared again, so the world is left as pure generator
// output, and blob_len is 0. On success game_tick is set and blob_len is the blob size (0 if the save has none).
Error loadGame(Storage& st, const char* path, World& w, uint32_t& game_tick, void* blob, uint32_t blob_cap,
               uint32_t& blob_len);

}  // namespace pge
