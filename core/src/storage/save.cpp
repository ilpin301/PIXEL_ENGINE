#include "pge/save.h"
#include <string.h>

namespace pge {

namespace {

constexpr uint32_t kRunsMax = 2048;  // locations per chunk: 2 layers x 32 x 32
constexpr uint32_t kSecHdr = 6;      // u8 type, u8 flags, u32 len

uint32_t le32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

uint32_t locOf(const World& w, uint32_t i) { return w.changeKeyAt(i) & 0xFFFF; }

// ---- save side ----
struct SaveCtx {
  const World* w;
  const void* blob;
  uint32_t blob_len;  // 0 = no blob section
  uint32_t ch_len;
};

Error produceSave(void* c, uint32_t offset, void* dst, uint32_t max, uint32_t* written) {
  const SaveCtx& x = *static_cast<const SaveCtx*>(c);
  ByteSink s(offset, dst, max);
  s.put(1);
  s.put(0);
  s.put32(x.ch_len);
  writeChanges(*x.w, s);
  if (x.blob_len && !s.full()) {
    s.put(2);
    s.put(0);
    s.put32(x.blob_len);
    s.putBytes(x.blob, x.blob_len);
  }
  *written = s.written();
  return Ok;
}

// ---- load side ----
struct LoadCtx {
  World* w;
  SaveInfo info;
  uint8_t* blob;
  uint32_t blob_cap, blob_len;
  bool started, touched, seen1, seen2, in_body;
  uint8_t hdr[kSecHdr];
  uint8_t hdr_n, type;
  uint32_t len, body_pos;
  ChangeReader reader;

  Error start() {
    started = true;
    if (info.world_seed != w->seed() || info.w_chunks != w->chunksW() || info.h_chunks != w->chunksH())
      return InvalidArg;  // not this world: leave it untouched
    touched = true;
    w->clearChanges();  // a save without a type-1 section loads as an empty log
    return Ok;
  }

  Error finishSection() {
    in_body = false;
    if (type == 1) return reader.end();
    if (type == 2) blob_len = len;
    return Ok;
  }

  Error feed(const uint8_t* p, uint32_t n) {
    while (n) {
      if (!in_body) {
        uint32_t take = kSecHdr - hdr_n;
        if (take > n) take = n;
        memcpy(hdr + hdr_n, p, take);
        hdr_n = (uint8_t)(hdr_n + take);
        p += take;
        n -= take;
        if (hdr_n < kSecHdr) continue;
        hdr_n = 0;
        type = hdr[0];
        len = le32(hdr + 2);
        body_pos = 0;
        in_body = true;
        if (type == 1) {
          if (seen1) return Corrupt;
          seen1 = true;
          reader.begin(*w);
        } else if (type == 2) {
          if (seen2) return Corrupt;
          seen2 = true;
          if (len > blob_cap) return OutOfMemory;
        }
        if (len == 0) {
          const Error e = finishSection();
          if (e != Ok) return e;
        }
      } else {
        uint32_t take = len - body_pos;
        if (take > n) take = n;
        if (type == 1) {
          const Error e = reader.feed(p, take);
          if (e != Ok) return e;
        } else if (type == 2) {
          memcpy(blob + body_pos, p, take);
        }
        body_pos += take;
        p += take;
        n -= take;
        if (body_pos == len) {
          const Error e = finishSection();
          if (e != Ok) return e;
        }
      }
    }
    return Ok;
  }
};

Error consumeLoad(void* c, uint32_t offset, const void* src, uint32_t n) {
  (void)offset;
  LoadCtx& x = *static_cast<LoadCtx*>(c);
  if (!x.started) {
    const Error e = x.start();
    if (e != Ok) return e;
  }
  return x.feed(static_cast<const uint8_t*>(src), n);
}

}  // namespace

Error writeChanges(const World& w, ByteSink& s) {
  const uint32_t n = w.changeCount();
  uint32_t i = 0;
  while (i < n) {
    if (s.full()) return Ok;  // shortcut: the encoder re-runs from the start for every Producer call, O(n) per call, upgrade to a saved cursor if profiling asks
    const uint32_t chunk = w.changeKeyAt(i) >> 16;
    uint32_t end = i + 1;
    uint32_t runs = 1;
    while (end < n && (w.changeKeyAt(end) >> 16) == chunk) {
      if (locOf(w, end) != locOf(w, end - 1) + 1 || w.changeTileAt(end) != w.changeTileAt(end - 1)) runs++;
      end++;
    }
    s.put16((uint16_t)chunk);
    s.putVarint(runs);
    uint32_t prev_end = 0;
    for (uint32_t k = i; k < end;) {
      if (s.full()) return Ok;
      const uint32_t start = k, loc = locOf(w, k);
      const uint8_t tile = w.changeTileAt(k);
      k++;
      while (k < end && locOf(w, k) == locOf(w, k - 1) + 1 && w.changeTileAt(k) == tile) k++;
      s.putVarint(loc - prev_end);
      s.putVarint(k - start);
      s.put(tile);
      prev_end = loc + (k - start);
    }
    i = end;
  }
  return Ok;
}

void ChangeReader::begin(World& w) {
  w.clearChanges();
  w_ = &w;
  st_ = ChunkLo;
  have_prev_ = false;
  vbytes_ = 0;
  acc_ = 0;
}

Error ChangeReader::varintDone(uint32_t v) {
  switch (st_) {
    case NRuns:
      if (v < 1 || v > kRunsMax) return Corrupt;
      runs_left_ = (uint16_t)v;
      prev_end_ = 0;
      st_ = Delta;
      return Ok;
    case Delta:
      if (v > kRunsMax) return Corrupt;
      loc_ = (uint16_t)(prev_end_ + v);
      st_ = RunLen;
      return Ok;
    default:  // RunLen
      if (v < 1 || v > kRunsMax || loc_ + v > kRunsMax) return Corrupt;
      len_ = (uint16_t)v;
      st_ = Tile;
      return Ok;
  }
}

Error ChangeReader::applyRun(uint8_t tile) {
  if (tile == kTileVoid) return Corrupt;
  const int cw = w_->chunksW();
  const int cx = chunk_ % cw, cy = chunk_ / cw;
  for (uint32_t k = 0; k < len_; k++) {
    const uint32_t loc = loc_ + k;
    const Error e = w_->set(cx * kChunkSize + (int)(loc & 31), cy * kChunkSize + (int)((loc >> 5) & 31),
                            (loc >> 10) & 1 ? Layer::Wall : Layer::Fg, tile);
    if (e == InvalidArg) return Corrupt;
    if (e != Ok) return e;
  }
  prev_end_ = (uint16_t)(loc_ + len_);
  st_ = --runs_left_ == 0 ? ChunkLo : Delta;
  return Ok;
}

Error ChangeReader::feed(const uint8_t* p, uint32_t n) {
  for (; n; p++, n--) {
    const uint8_t b = *p;
    switch (st_) {
      case ChunkLo:
        chunk_ = b;
        st_ = ChunkHi;
        break;
      case ChunkHi:
        chunk_ = (uint16_t)(chunk_ | (b << 8));
        if ((uint32_t)chunk_ >= (uint32_t)w_->chunksW() * w_->chunksH() || (have_prev_ && chunk_ <= prev_chunk_))
          return Corrupt;
        prev_chunk_ = chunk_;
        have_prev_ = true;
        st_ = NRuns;
        break;
      case Tile: {
        const Error e = applyRun(b);
        if (e != Ok) return e;
        break;
      }
      default: {  // NRuns, Delta, RunLen: LEB128, at most 3 bytes
        acc_ |= (uint32_t)(b & 0x7F) << (7 * vbytes_);
        vbytes_++;
        if (b & 0x80) {
          if (vbytes_ == 3) return Corrupt;
          break;
        }
        const uint32_t v = acc_;
        acc_ = 0;
        vbytes_ = 0;
        const Error e = varintDone(v);
        if (e != Ok) return e;
        break;
      }
    }
  }
  return Ok;
}

Error ChangeReader::end() { return st_ == ChunkLo ? Ok : Corrupt; }

Error saveGame(Storage& st, const char* path, const World& w, uint32_t game_tick, const void* blob,
               uint32_t blob_len) {
  SaveCtx x;
  x.w = &w;
  x.blob = blob;
  x.blob_len = blob && blob_len ? blob_len : 0;
  ByteSink counter;
  writeChanges(w, counter);
  x.ch_len = counter.pos();
  if (x.blob_len > 0x7FFFFFFFu - 2 * kSecHdr - x.ch_len) return InvalidArg;
  SaveInfo info;
  info.world_seed = w.seed();
  info.game_tick = game_tick;
  info.w_chunks = w.chunksW();
  info.h_chunks = w.chunksH();
  return st.saveAtomic(path, info, produceSave, &x);
}

Error loadGame(Storage& st, const char* path, World& w, uint32_t& game_tick, void* blob, uint32_t blob_cap,
               uint32_t& blob_len) {
  LoadCtx x;
  memset(&x.info, 0, sizeof(x.info));
  x.w = &w;
  x.blob = static_cast<uint8_t*>(blob);
  x.blob_cap = blob_cap;
  x.blob_len = 0;
  x.started = x.touched = x.seen1 = x.seen2 = x.in_body = false;
  x.hdr_n = x.type = 0;
  x.len = x.body_pos = 0;
  Error e = st.loadSave(path, x.info, consumeLoad, &x);
  if (e == Ok && !x.started) e = x.start();  // empty payload: the consumer never ran
  if (e == Ok && (x.in_body || x.hdr_n != 0)) e = Corrupt;  // partial section at the end
  if (e != Ok) {
    if (x.touched) w.clearChanges();
    blob_len = 0;
    return e;
  }
  game_tick = x.info.game_tick;
  blob_len = x.blob_len;
  return Ok;
}

}  // namespace pge
