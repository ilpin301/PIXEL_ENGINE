// Storage + save tests (S5.3a): CRC, streamed round trip at several I/O buffer sizes, ByteSink windows, ChangeReader
// robustness, file-level corruption, .bak fallback, exhaustive power-loss simulation of saveAtomic, edge cases.
// Disk access goes only through the HAL (pge_hal_storage_*) plus Storage/saveGame/loadGame, so a RAM-disk HAL fake can
// rerun the whole file; the only host-specific calls are pge_host_storage_set_budget / pge_host_storage_open_files.
// Test files live in the current directory (pge_ts_*) and are removed at the start and end of the run.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "host_storage.h"
#include "pge/config.h"
#include "pge/hal/storage.h"
#include "pge/save.h"
#include "pge/storage.h"
#include "pge/world.h"

#define CHECK(c)                                                          \
  do {                                                                    \
    if (!(c)) {                                                           \
      std::printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); \
      return 1;                                                           \
    }                                                                     \
  } while (0)

using namespace pge;
typedef std::vector<uint8_t> Bytes;

static TileProps g_props[256];

// ---------------------------------------------------------------- disk helpers (HAL only)
static const char* const kMain = "pge_ts_main.sav";
static const char* const kAux = "pge_ts_aux.sav";
static std::string g_long;  // longest accepted path: PGE_PATH_MAX - 5 characters

static void rmAll(const std::string& p) {
  pge_hal_storage_remove(p.c_str());
  pge_hal_storage_remove((p + ".tmp").c_str());
  pge_hal_storage_remove((p + ".bak").c_str());
}
static void cleanAll() {
  pge_host_storage_set_budget(-1);
  rmAll(kMain);
  rmAll(kAux);
  rmAll(g_long);
}
static bool has(const std::string& p) {
  pge_file f;
  if (pge_hal_storage_open(p.c_str(), 0, &f) != 0) return false;
  pge_hal_storage_close(f);
  return true;
}
static bool readBytes(const std::string& p, Bytes& out) {
  pge_file f;
  out.clear();
  if (pge_hal_storage_open(p.c_str(), 0, &f) != 0) return false;
  uint8_t tmp[256];
  for (;;) {
    const int32_t r = pge_hal_storage_read(f, tmp, sizeof tmp);
    if (r <= 0) break;
    out.insert(out.end(), tmp, tmp + r);
  }
  pge_hal_storage_close(f);
  return true;
}
static bool writeBytes(const std::string& p, const Bytes& b) {
  pge_file f;
  if (pge_hal_storage_open(p.c_str(), 1, &f) != 0) return false;
  bool ok = true;
  size_t off = 0;
  while (ok && off < b.size()) {
    const int32_t w = pge_hal_storage_write(f, b.data() + off, (uint32_t)(b.size() - off));
    if (w <= 0) ok = false;
    else off += (size_t)w;
  }
  pge_hal_storage_close(f);
  return ok;
}
static std::string tmpOf(const char* p) { return std::string(p) + ".tmp"; }
static std::string bakOf(const char* p) { return std::string(p) + ".bak"; }

// ---------------------------------------------------------------- world / storage environments
struct Env {
  Arena arena;
  std::vector<uint64_t> mem;
  World w;
  Error init(uint32_t seed = 7, int wc = 4, int hc = 2, uint32_t cap = 512, int slots = 3) {
    mem.assign(1 << 13, 0);
    arena.init(mem.data(), mem.size() * 8);
    WorldConfig c{};
    c.w_chunks = (uint16_t)wc;
    c.h_chunks = (uint16_t)hc;
    c.seed = seed;
    c.gen = flatGen;
    c.props = g_props;
    c.cache_slots = (uint16_t)slots;
    c.change_capacity = cap;
    c.tile_px = 16;
    return w.init(c, arena);
  }
};

struct StEnv {
  Arena arena;
  std::vector<uint64_t> mem;
  Bytes io;
  Storage st;
  Error setup(uint32_t io_bytes) {  // 0 = default buffer from the arena
    mem.assign(1 << 13, 0);
    arena.init(mem.data(), mem.size() * 8);
    if (io_bytes == 0) {
      const Error e = st.init(arena, 0);
      if (e != Ok) return e;
    } else {
      io.assign(io_bytes, 0);
      st.setBuffer(io.data(), io_bytes);
    }
    return st.mount();
  }
};

static int ldc(World& w, int cx, int cy) { return w.ensureLoaded(Rect{cx * 512, cy * 512, 16, 16}, 8); }

static uint32_t g_rng;
static uint32_t rnd() { g_rng = g_rng * 1664525u + 1013904223u; return g_rng >> 8; }

static Bytes pattern(uint32_t n, uint32_t seed) {
  Bytes b(n);
  for (uint32_t i = 0; i < n; i++) b[i] = (uint8_t)(i * 7 + 3 + seed);
  return b;
}

// Many edits: loaded and unloaded chunks, runs crossing chunk boundaries and the fg->wall layer boundary.
static int richEdits(World& w) {
  g_rng = 777;
  for (int i = 0; i < 150; i++) {  // scatter first; explicit edits below overwrite where they overlap
    const int tx = (int)(rnd() % 128), ty = (int)(rnd() % 64);
    const Layer l = (rnd() & 1) ? Layer::Wall : Layer::Fg;
    CHECK(w.set(tx, ty, l, (uint8_t)(1 + rnd() % 250)) == Ok);
  }
  ldc(w, 0, 0);
  ldc(w, 1, 0);
  for (int tx = 0; tx <= 40; tx++) CHECK(w.set(tx, 3, Layer::Fg, 5) == Ok);   // crosses chunk 0 -> 1
  for (int tx = 50; tx <= 52; tx++) CHECK(w.set(tx, 3, Layer::Fg, 6) == Ok);
  for (int tx = 53; tx <= 54; tx++) CHECK(w.set(tx, 3, Layer::Fg, 7) == Ok);  // adjacent, other tile: separate run
  for (int tx = 0; tx <= 20; tx++) CHECK(w.set(tx, 5, Layer::Wall, 9) == Ok);
  CHECK(w.set(31, 31, Layer::Fg, 11) == Ok);  // loc 1023
  CHECK(w.set(0, 0, Layer::Wall, 11) == Ok);  // loc 1024: same tile, one run across the layer boundary
  CHECK(w.set(63, 31, Layer::Fg, 12) == Ok);
  CHECK(w.set(32, 0, Layer::Wall, 13) == Ok);  // same crossing, different tile: two runs
  CHECK(w.set(10, 10, Layer::Fg, 5) == Ok);
  CHECK(w.set(10, 10, Layer::Fg, 6) == Ok);
  CHECK(w.set(10, 10, Layer::Fg, 7) == Ok);
  CHECK(w.set(10, 10, Layer::Fg, 7) == Ok);
  ldc(w, 3, 1);
  ldc(w, 2, 1);
  ldc(w, 1, 1);  // 3 slots: chunks 0 and 1 are evicted, their edits live in the log only
  for (int tx = 100; tx <= 127; tx++) CHECK(w.set(tx, 33, Layer::Wall, 14) == Ok);
  CHECK(w.set(65, 40, Layer::Fg, 15) == Ok);
  for (int tx = 5; tx <= 9; tx++) CHECK(w.set(tx, 40, Layer::Fg, 8) == Ok);  // chunk (0,1), not loaded
  CHECK(w.changeCount() > 200 && w.changeCount() < w.changeCapacity());
  return 0;
}

// Whole chunks as single runs, up to the 2048-location maximum; fills the log (cap 4096) completely.
static int fullEdits(World& w) {
  for (int ty = 32; ty < 64; ty++)
    for (int tx = 32; tx < 64; tx++) {
      CHECK(w.set(tx, ty, Layer::Fg, 4) == Ok);
      CHECK(w.set(tx, ty, Layer::Wall, 4) == Ok);  // chunk 5: one run, loc 0, len 2048
    }
  for (int ty = 0; ty < 32; ty++)
    for (int tx = 32; tx < 64; tx++) CHECK(w.set(tx, ty, Layer::Fg, 5) == Ok);  // chunk 1 fg only
  for (int ty = 0; ty < 32; ty++)
    for (int tx = 64; tx < 96; tx++) CHECK(w.set(tx, ty, Layer::Wall, 6) == Ok);  // chunk 2 wall only: loc 1024.., len 1024
  CHECK(w.changeCount() == 4096);
  CHECK(w.set(100, 50, Layer::Fg, 3) == OutOfMemory);
  CHECK(w.set(40, 40, Layer::Fg, 9) == Ok);  // existing entry: overwrite is fine on a full log
  CHECK(w.set(40, 40, Layer::Fg, 4) == Ok);
  return 0;
}

static int smallEdits(World& w, int v) {
  CHECK(w.set(3 + v, 2, Layer::Fg, (uint8_t)(10 + v)) == Ok);
  for (int tx = 40; tx <= 44; tx++) CHECK(w.set(tx, 7, Layer::Fg, (uint8_t)(20 + v)) == Ok);
  CHECK(w.set(70, 33, Layer::Wall, (uint8_t)(30 + v)) == Ok);
  CHECK(w.set(100 + v, 50, Layer::Fg, 1) == Ok);
  return 0;
}

static int sameLog(const World& a, const World& b) {
  CHECK(a.changeCount() == b.changeCount());
  for (uint32_t i = 0; i < a.changeCount(); i++) {
    CHECK(a.changeKeyAt(i) == b.changeKeyAt(i));
    CHECK(a.changeTileAt(i) == b.changeTileAt(i));
  }
  return 0;
}

// Same change log and identical get() over every tile of every chunk.
static int sameWorld(World& a, World& b) {
  CHECK(a.chunksW() == b.chunksW() && a.chunksH() == b.chunksH());
  CHECK(sameLog(a, b) == 0);
  for (int cy = 0; cy < a.chunksH(); cy++)
    for (int cx = 0; cx < a.chunksW(); cx++) {
      CHECK(ldc(a, cx, cy) == 0 && ldc(b, cx, cy) == 0);
      for (int ty = cy * 32; ty < cy * 32 + 32; ty++)
        for (int tx = cx * 32; tx < cx * 32 + 32; tx++)
          for (int l = 0; l < 2; l++) {
            const uint8_t x = a.get(tx, ty, (Layer)l), y = b.get(tx, ty, (Layer)l);
            if (x != y || x == kTileVoid) {
              std::printf("tile mismatch at %d,%d layer %d: %d vs %d\n", tx, ty, l, x, y);
              return 1;
            }
          }
    }
  return 0;
}

// ---------------------------------------------------------------- raw save payloads via the Producer API
static void le32put(Bytes& p, uint32_t v) {
  for (int i = 0; i < 4; i++) p.push_back((uint8_t)(v >> (8 * i)));
}
static void sec(Bytes& p, uint8_t type, const Bytes& body) {
  p.push_back(type);
  p.push_back(0);
  le32put(p, (uint32_t)body.size());
  p.insert(p.end(), body.begin(), body.end());
}
static Error rawProduce(void* c, uint32_t off, void* dst, uint32_t max, uint32_t* w) {
  const Bytes& p = *static_cast<const Bytes*>(c);
  uint32_t n = 0;
  if (off < p.size()) {
    n = (uint32_t)p.size() - off;
    if (n > max) n = max;
    memcpy(dst, p.data() + off, n);
  }
  *w = n;
  return Ok;
}
static Error saveRaw(Storage& st, const char* path, Bytes payload, uint32_t seed = 7, int wc = 4, int hc = 2,
                     uint32_t tick = 1) {
  SaveInfo i;
  i.world_seed = seed;
  i.game_tick = tick;
  i.w_chunks = (uint16_t)wc;
  i.h_chunks = (uint16_t)hc;
  return st.saveAtomic(path, i, rawProduce, &payload);
}
// The type-1 body of a world as bytes (via the windowed writer).
static Bytes bodyOf(const World& w) {
  ByteSink c;
  writeChanges(w, c);
  Bytes b(c.pos() + 1);
  ByteSink s(0, b.data(), (uint32_t)b.size());
  writeChanges(w, s);
  b.resize(s.written());
  return b;
}

struct CCtx {
  uint32_t calls = 0, bytes = 0;
  bool bad_offset = false;
  Error fail = Ok;
  Bytes data;
};
static Error cons(void* c, uint32_t off, const void* src, uint32_t n) {
  CCtx& x = *static_cast<CCtx*>(c);
  x.calls++;
  if (off != x.bytes) x.bad_offset = true;
  x.bytes += n;
  x.data.insert(x.data.end(), static_cast<const uint8_t*>(src), static_cast<const uint8_t*>(src) + n);
  return x.fail;
}

static void fixHdr(Bytes& b) {
  const uint32_t c = crc32Update(0, b.data(), 28);
  for (int i = 0; i < 4; i++) b[28 + i] = (uint8_t)(c >> (8 * i));
}

// ================================================================ 1. crc32
static int testCrc() {
  const char* s = "123456789";
  CHECK(crc32Update(0, s, 9) == 0xCBF43926u);
  CHECK(crc32Update(0, s, 0) == 0);
  for (uint32_t k = 0; k <= 9; k++) CHECK(crc32Update(crc32Update(0, s, k), s + k, 9 - k) == 0xCBF43926u);
  const Bytes b = pattern(1000, 5);
  const uint32_t one = crc32Update(0, b.data(), 1000);
  uint32_t c = 0;
  for (uint32_t i = 0; i < 1000; i += 37) c = crc32Update(c, b.data() + i, i + 37 <= 1000 ? 37 : 1000 - i);
  CHECK(c == one);
  CHECK(pge_host_storage_open_files() == 0);
  return 0;
}

// ================================================================ 2. round trip
static int rtOne(Storage& st, Env& src, uint32_t tick, const void* bp, uint32_t bl, bool dirty, bool check_cells) {
  cleanAll();
  CHECK(saveGame(st, kMain, src.w, tick, bp, bl) == Ok);
  CHECK(!has(tmpOf(kMain)));
  Env dst;
  CHECK(dst.init(7, src.w.chunksW(), src.w.chunksH(), src.w.changeCapacity()) == Ok);
  if (dirty) {  // stale edits in the target must be replaced, not merged
    CHECK(dst.w.set(1, 1, Layer::Fg, 9) == Ok);
    CHECK(dst.w.set(100, 60, Layer::Wall, 9) == Ok);
  }
  Bytes out(bl);
  uint32_t t = 0, olen = 99;
  CHECK(loadGame(st, kMain, dst.w, t, out.data(), bl, olen) == Ok);  // blob_cap == blob size exactly
  CHECK(t == tick);
  CHECK(olen == bl);
  if (bl) CHECK(memcmp(out.data(), bp, bl) == 0);
  CHECK(sameWorld(src.w, dst.w) == 0);
  if (check_cells) {
    ldc(dst.w, 0, 0);
    ldc(dst.w, 1, 0);
    CHECK(dst.w.get(40, 3, Layer::Fg) == 5 && dst.w.get(10, 10, Layer::Fg) == 7);
    CHECK(dst.w.get(31, 31, Layer::Fg) == 11 && dst.w.get(0, 0, Layer::Wall) == 11);
    CHECK(dst.w.get(0, 5, Layer::Wall) == 9 && dst.w.get(20, 5, Layer::Wall) == 9);
  }
  return 0;
}

static int testRoundTrip() {
  Env rich, empty, full;
  CHECK(rich.init() == Ok && empty.init() == Ok && full.init(7, 4, 2, 4096) == Ok);
  CHECK(richEdits(rich.w) == 0);
  CHECK(fullEdits(full.w) == 0);
  const Bytes blob = pattern(300, 1);
  const uint32_t sizes[4] = {1, 7, 33, 0};
  for (uint32_t io : sizes) {
    StEnv se;
    CHECK(se.setup(io) == Ok);
    Storage& st = se.st;
    CHECK(rtOne(st, rich, 0xDEADBEEFu, blob.data(), 300, true, true) == 0);
    CHECK(rtOne(st, rich, 1, nullptr, 0, true, true) == 0);
    CHECK(rtOne(st, rich, 2, blob.data(), 0, false, true) == 0);  // pointer given, length 0: no blob section
    CHECK(rtOne(st, rich, 3, blob.data(), 1, false, true) == 0);
    CHECK(rtOne(st, empty, 0, nullptr, 0, true, false) == 0);     // empty log, no blob
    CHECK(rtOne(st, empty, 0xFFFFFFFFu, blob.data(), 300, true, false) == 0);  // empty log + blob
    CHECK(rtOne(st, full, 77, blob.data(), 64, true, false) == 0);
    CHECK(pge_host_storage_open_files() == 0);
  }
  return 0;
}

// ================================================================ 3. writer windows
static void streamAll(ByteSink& s, const Bytes& pat) {
  s.put(1);
  s.put16(0x0302);
  s.put32(0x07060504u);
  s.putVarint(300);
  s.putVarint(0);
  s.putBytes(pat.data(), (uint32_t)pat.size());
  s.putBytes(pat.data(), 0);
  s.put(0xAA);
  s.putBytes(pat.data(), (uint32_t)pat.size());
}

static int testWindows() {
  Env e;
  CHECK(e.init() == Ok);
  CHECK(richEdits(e.w) == 0);
  ByteSink cnt;
  writeChanges(e.w, cnt);
  const uint32_t n = cnt.pos();
  CHECK(n > 200);
  Bytes ref(n + 16, 0xCC);
  {
    ByteSink s(0, ref.data(), (uint32_t)ref.size());
    writeChanges(e.w, s);
    CHECK(s.written() == n && s.pos() == n);
  }
  ref.resize(n);
  const uint32_t strides[] = {1, 2, 3, 5, 7, 16, 33, 100, n, n + 5};
  for (uint32_t stride : strides) {
    Bytes cat;
    for (uint32_t off = 0; off < n; off += stride) {
      Bytes tmp(stride, 0xCC);
      ByteSink s(off, tmp.data(), stride);
      writeChanges(e.w, s);
      const uint32_t want = stride < n - off ? stride : n - off;
      CHECK(s.written() == want);
      cat.insert(cat.end(), tmp.begin(), tmp.begin() + want);
    }
    CHECK(cat == ref);
  }
  {  // window starting at or past the end stores nothing
    Bytes tmp(8, 0xCC);
    ByteSink s(n, tmp.data(), 8);
    writeChanges(e.w, s);
    CHECK(s.written() == 0 && tmp[0] == 0xCC);
  }
  {  // random window sizes
    g_rng = 4242;
    Bytes cat;
    uint32_t off = 0;
    while (off < n) {
      const uint32_t m = 1 + rnd() % 50;
      Bytes tmp(m, 0xCC);
      ByteSink s(off, tmp.data(), m);
      writeChanges(e.w, s);
      const uint32_t want = m < n - off ? m : n - off;
      CHECK(s.written() == want);
      cat.insert(cat.end(), tmp.begin(), tmp.begin() + want);
      off += m;
    }
    CHECK(cat == ref);
  }
  {  // ByteSink primitives, putBytes included
    const Bytes pat = pattern(40, 9);
    ByteSink c2;
    streamAll(c2, pat);
    const uint32_t total = c2.pos();
    Bytes r2(total + 4, 0xCC);
    ByteSink s2(0, r2.data(), (uint32_t)r2.size());
    streamAll(s2, pat);
    CHECK(s2.written() == total && s2.pos() == total);
    CHECK(r2[0] == 1 && r2[1] == 2 && r2[2] == 3 && r2[3] == 4 && r2[6] == 7 && r2[7] == 0xAC && r2[8] == 0x02 && r2[9] == 0);
    r2.resize(total);
    for (uint32_t stride = 1; stride <= 13; stride++) {
      Bytes cat;
      for (uint32_t off = 0; off < total; off += stride) {
        Bytes tmp(stride, 0xCC);
        ByteSink s(off, tmp.data(), stride);
        streamAll(s, pat);
        const uint32_t want = stride < total - off ? stride : total - off;
        CHECK(s.written() == want && s.pos() == total);
        cat.insert(cat.end(), tmp.begin(), tmp.begin() + want);
      }
      CHECK(cat == r2);
    }
  }
  CHECK(pge_host_storage_open_files() == 0);
  return 0;
}

// ================================================================ 4. ChangeReader
static Error runReader(World& w, const Bytes& b, int mode, size_t split = 0) {
  ChangeReader r;
  r.begin(w);
  Error e = Ok;
  if (mode == 1) {  // one byte at a time
    for (size_t i = 0; i < b.size() && e == Ok; i++) e = r.feed(&b[i], 1);
  } else if (mode == 2) {  // two parts
    e = r.feed(b.data(), (uint32_t)split);
    if (e == Ok) e = r.feed(b.data() + split, (uint32_t)(b.size() - split));
  } else {
    e = r.feed(b.data(), (uint32_t)b.size());
  }
  if (e != Ok) return e;
  return r.end();
}

// Result of a body fed whole and byte-wise must agree.
static int bodyResult(const Bytes& b, Error& out, uint32_t cap = 512, uint32_t* count = nullptr) {
  Env e1, e2;
  CHECK(e1.init(7, 4, 2, cap) == Ok && e2.init(7, 4, 2, cap) == Ok);
  const Error r1 = runReader(e1.w, b, 0), r2 = runReader(e2.w, b, 1);
  CHECK(r1 == r2);
  out = r1;
  if (count) *count = e1.w.changeCount();
  return 0;
}

static int expectCorrupt(const Bytes& b) {
  Error e = Ok;
  CHECK(bodyResult(b, e) == 0);
  CHECK(e == Corrupt);
  return 0;
}

static int testReader() {
  Env src;
  CHECK(src.init() == Ok);
  CHECK(richEdits(src.w) == 0);
  const Bytes body = bodyOf(src.w);
  CHECK(body.size() > 200);

  {  // whole == byte-wise == every two-part split == the source
    Env a, b;
    CHECK(a.init() == Ok && b.init() == Ok);
    CHECK(runReader(a.w, body, 0) == Ok && runReader(b.w, body, 1) == Ok);
    CHECK(sameWorld(a.w, b.w) == 0 && sameWorld(a.w, src.w) == 0);
    for (size_t sp = 1; sp < body.size(); sp++) {
      Env c;
      CHECK(c.init() == Ok);
      CHECK(runReader(c.w, body, 2, sp) == Ok);
      CHECK(sameLog(c.w, src.w) == 0);
    }
  }
  {  // every prefix is Ok (chunk boundary) or Corrupt; the empty body and the 1-, 2-, 3- and N-1-byte prefixes
    uint32_t oks = 0;
    for (size_t p = 0; p < body.size(); p++) {
      Env c;
      CHECK(c.init() == Ok);
      const Bytes pre(body.begin(), body.begin() + p);
      const Error e = runReader(c.w, pre, 0);
      CHECK(e == Ok || e == Corrupt);
      if (p == 0) CHECK(e == Ok && c.w.changeCount() == 0);
      if (p >= 1 && p <= 3) CHECK(e == Corrupt);
      if (p == body.size() - 1) CHECK(e == Corrupt);
      if (e == Ok) oks++;
    }
    CHECK(oks >= 1 && oks < body.size() / 4);  // chunk boundaries only
  }
  // hand-built corrupt bodies: u16 chunk, varint n_runs, then runs (delta, len, tile)
  CHECK(expectCorrupt({8, 0, 1, 0, 1, 5}) == 0);                       // chunk 8 of 8
  CHECK(expectCorrupt({0xFF, 0xFF, 1, 0, 1, 5}) == 0);                 // chunk 65535
  CHECK(expectCorrupt({1, 0, 1, 0, 1, 5, 1, 0, 1, 0, 1, 5}) == 0);     // chunk repeated
  CHECK(expectCorrupt({2, 0, 1, 0, 1, 5, 1, 0, 1, 0, 1, 5}) == 0);     // chunks descending
  CHECK(expectCorrupt({0, 0, 0}) == 0);                                // n_runs 0
  CHECK(expectCorrupt({0, 0, 1, 0, 0, 5}) == 0);                       // run_len 0
  CHECK(expectCorrupt({0, 0, 1, 0xFF, 0x0F, 2, 5}) == 0);              // loc 2047 + len 2 > 2048
  CHECK(expectCorrupt({0, 0, 1, 0x80, 0x10, 1, 5}) == 0);              // loc 2048 + len 1
  CHECK(expectCorrupt({0, 0, 1, 0, 0x81, 0x10, 5}) == 0);              // len 2049
  CHECK(expectCorrupt({0, 0, 1, 0, 1, 255}) == 0);                     // tile 255
  CHECK(expectCorrupt({0, 0, 0x81, 0x81, 0x81, 0x01, 1, 0, 1, 5}) == 0);  // 4-byte varint (n_runs)
  CHECK(expectCorrupt({0, 0, 1, 0x81, 0x81, 0x81, 0x01, 1, 5}) == 0);     // 4-byte varint (delta)
  CHECK(expectCorrupt({0, 0, 1, 0, 0x81, 0x81, 0x81, 0x01, 5}) == 0);     // 4-byte varint (len)
  CHECK(expectCorrupt({0, 0, 3, 0, 1, 5, 0, 1, 5}) == 0);                 // announces 3 runs, has 2
  CHECK(expectCorrupt({0, 0, 1, 0, 1}) == 0);                              // no tile byte
  {  // valid boundaries are accepted
    Error e;
    uint32_t n = 0;
    CHECK(bodyResult({0, 0, 1, 0, 0x80, 0x10, 5}, e, 4096, &n) == 0);    // 2048 locations
    CHECK(e == Ok && n == 2048);
    CHECK(bodyResult({7, 0, 1, 0xFF, 0x0F, 1, 5}, e, 4096, &n) == 0);    // last location of the last chunk
    CHECK(e == Ok && n == 1);
    CHECK(bodyResult({0, 0, 2, 0, 1, 5, 0, 1, 6}, e, 4096, &n) == 0);   // adjacent runs, delta 0
    CHECK(e == Ok && n == 2);
  }
  {  // log too small: OutOfMemory (both feeding styles)
    Error e;
    CHECK(bodyResult(body, e, 100) == 0);
    CHECK(e == OutOfMemory);
    CHECK(bodyResult(body, e, src.w.changeCount()) == 0 && e == Ok);       // exactly enough
    CHECK(bodyResult(body, e, src.w.changeCount() - 1) == 0 && e == OutOfMemory);
  }
  CHECK(pge_host_storage_open_files() == 0);
  return 0;
}

// loadGame leaves the log cleared after any failure that happened after the world was touched.
static int testLoadFailClears() {
  StEnv se;
  CHECK(se.setup(7) == Ok);
  Env src;
  CHECK(src.init() == Ok);
  CHECK(richEdits(src.w) == 0);
  const Bytes body = bodyOf(src.w);
  const Bytes dup = {0, 0, 1, 0, 1, 5, 0, 0, 1, 0, 1, 5};  // chunk 0 twice: fails after entries were applied
  struct Case { Bytes payload; Error want; uint32_t cap; };
  Bytes trunc(body.begin(), body.end() - 1);
  Case cases[] = {
      {dup, Corrupt, 512},
      {{0, 0, 1, 0, 1, 255}, Corrupt, 512},
      {trunc, Corrupt, 512},   // section length matches, but the last run is unfinished: end() Corrupt
      {body, OutOfMemory, 100},
      {{9, 0, 1, 0, 1, 5}, Corrupt, 512},
  };
  for (Case& c : cases) {
    Bytes payload;
    sec(payload, 1, c.payload);
    cleanAll();
    CHECK(saveRaw(se.st, kMain, payload) == Ok);
    Env dst;
    CHECK(dst.init(7, 4, 2, c.cap) == Ok);
    CHECK(dst.w.set(1, 1, Layer::Fg, 9) == Ok && dst.w.set(70, 40, Layer::Wall, 9) == Ok);
    uint32_t t = 55, bl = 77;
    uint8_t blob[8];
    CHECK(loadGame(se.st, kMain, dst.w, t, blob, sizeof blob, bl) == c.want);
    CHECK(dst.w.changeCount() == 0);
    CHECK(bl == 0);
    CHECK(ldc(dst.w, 0, 0) == 0 && dst.w.get(1, 1, Layer::Fg) == kTileAir);  // pure generator output again
  }
  CHECK(pge_host_storage_open_files() == 0);
  return 0;
}

// ================================================================ 5. file-level corruption
static int testCorruptFile() {
  StEnv se;
  CHECK(se.setup(33) == Ok);
  Storage& st = se.st;
  Env src;
  CHECK(src.init() == Ok);
  CHECK(richEdits(src.w) == 0);
  const Bytes blob = pattern(200, 3);
  cleanAll();
  CHECK(saveGame(st, kMain, src.w, 1234, blob.data(), 200) == Ok);
  Bytes good;
  CHECK(readBytes(kMain, good));
  CHECK(good.size() > 32 + 200);

  SaveInfo info;
  {  // the pristine file loads
    CCtx c;
    CHECK(st.loadSave(kMain, info, cons, &c) == Ok);
    CHECK(c.bytes == good.size() - 32 && !c.bad_offset);
    CHECK(info.game_tick == 1234 && info.world_seed == 7 && info.w_chunks == 4 && info.h_chunks == 2);
  }
  // each damage is written over `path` (no .bak): loadSave must be Corrupt and must not have consumed anything
  struct Dmg { const char* what; Bytes b; };
  std::vector<Dmg> dmg;
  for (size_t i = 0; i < 32; i++) {  // a bit flip in every header byte
    Bytes b = good;
    b[i] ^= (uint8_t)(1u << (i % 8));
    dmg.push_back({"header flip", b});
  }
  for (size_t i = 32; i < good.size(); i++) {  // a bit flip in every payload byte
    Bytes b = good;
    b[i] ^= (uint8_t)(1u << (i % 8));
    dmg.push_back({"payload flip", b});
  }
  {
    Bytes b(good.begin(), good.end() - 1);
    dmg.push_back({"cut last byte", b});
    for (size_t n : {size_t(0), size_t(1), size_t(10), size_t(31), size_t(32), size_t(33)})
      dmg.push_back({"short file", Bytes(good.begin(), good.begin() + n)});
    b = good; b.push_back(0);
    dmg.push_back({"trailing garbage", b});
    b = good; b[4] = 2; fixHdr(b);
    dmg.push_back({"version 2", b});
    b = good; b[0] = 'X'; fixHdr(b);
    dmg.push_back({"magic", b});
    b = good; b[6] = 36; fixHdr(b);
    dmg.push_back({"header size", b});
    b = good; b[16]++; fixHdr(b);  // payload length one too big: looks truncated
    dmg.push_back({"len+1", b});
    b = good; b[16]--; fixHdr(b);  // one too small: trailing byte
    dmg.push_back({"len-1", b});
  }
  for (const Dmg& d : dmg) {
    cleanAll();
    CHECK(writeBytes(kMain, d.b));
    CCtx c;
    const Error e = st.loadSave(kMain, info, cons, &c);
    if (e != Corrupt || c.calls != 0) {
      std::printf("damage '%s' (size %u): got %d, consumer calls %u\n", d.what, (unsigned)d.b.size(), (int)e, c.calls);
      return 1;
    }
  }
  {  // the CRC is checked before the world is touched
    Bytes b = good;
    b[100] ^= 1;
    cleanAll();
    CHECK(writeBytes(kMain, b));
    Env dst;
    CHECK(dst.init() == Ok);
    CHECK(dst.w.set(1, 1, Layer::Fg, 9) == Ok && dst.w.set(2, 2, Layer::Fg, 9) == Ok && dst.w.set(3, 3, Layer::Fg, 9) == Ok);
    uint32_t t = 0, bl = 5;
    uint8_t out[300];
    CHECK(loadGame(st, kMain, dst.w, t, out, sizeof out, bl) == Corrupt);
    CHECK(dst.w.changeCount() == 3 && bl == 0);
  }
  {  // missing file
    cleanAll();
    CCtx c;
    CHECK(st.loadSave(kMain, info, cons, &c) == Io && c.calls == 0);
    Env dst;
    CHECK(dst.init() == Ok);
    uint32_t t = 0, bl = 5;
    CHECK(loadGame(st, kMain, dst.w, t, nullptr, 0, bl) == Io && bl == 0);
  }
  // seed / dimension mismatch: InvalidArg, world untouched
  for (int variant = 0; variant < 3; variant++) {
    cleanAll();
    CHECK(saveGame(st, kMain, src.w, 9, blob.data(), 200) == Ok);
    Env dst;
    CHECK(dst.init(variant == 0 ? 8 : 7, variant == 1 ? 3 : 4, variant == 2 ? 1 : 2) == Ok);
    CHECK(dst.w.set(1, 1, Layer::Fg, 9) == Ok && dst.w.set(2, 2, Layer::Fg, 9) == Ok && dst.w.set(3, 3, Layer::Fg, 9) == Ok);
    const uint32_t k0 = dst.w.changeKeyAt(0);
    uint32_t t = 0, bl = 5;
    uint8_t out[300];
    CHECK(loadGame(st, kMain, dst.w, t, out, sizeof out, bl) == InvalidArg);
    CHECK(dst.w.changeCount() == 3 && dst.w.changeKeyAt(0) == k0 && bl == 0);
    CHECK(ldc(dst.w, 0, 0) == 0 && dst.w.get(1, 1, Layer::Fg) == 9);
  }
  {  // blob larger than blob_cap
    cleanAll();
    CHECK(saveGame(st, kMain, src.w, 9, blob.data(), 200) == Ok);
    for (uint32_t cap : {0u, 1u, 199u}) {
      Env dst;
      CHECK(dst.init() == Ok);
      uint32_t t = 0, bl = 5;
      uint8_t out[300];
      CHECK(loadGame(st, kMain, dst.w, t, out, cap, bl) == OutOfMemory);
      CHECK(dst.w.changeCount() == 0 && bl == 0);
    }
    Env dst;
    CHECK(dst.init() == Ok);
    uint32_t t = 0, bl = 5;
    uint8_t out[300];
    CHECK(loadGame(st, kMain, dst.w, t, out, 200, bl) == Ok && bl == 200 && memcmp(out, blob.data(), 200) == 0);
  }
  // hand-built payloads
  const Bytes empty;
  const Bytes b5 = pattern(5, 8);
  {  // unknown section type 7 (before and after type 1) is skipped
    for (int variant = 0; variant < 2; variant++) {
      Bytes p;
      if (variant == 1) sec(p, 7, {9, 9});
      sec(p, 1, variant == 0 ? empty : bodyOf(src.w));
      sec(p, 7, {1, 2, 3});
      sec(p, 2, b5);
      sec(p, 200, empty);
      cleanAll();
      CHECK(saveRaw(st, kMain, p, 7, 4, 2, 321) == Ok);
      Env dst;
      CHECK(dst.init() == Ok);
      CHECK(dst.w.set(1, 1, Layer::Fg, 9) == Ok);
      uint32_t t = 0, bl = 0;
      uint8_t out[8];
      CHECK(loadGame(st, kMain, dst.w, t, out, sizeof out, bl) == Ok);
      CHECK(t == 321 && bl == 5 && memcmp(out, b5.data(), 5) == 0);
      if (variant == 0) CHECK(dst.w.changeCount() == 0);
      else CHECK(sameWorld(dst.w, src.w) == 0);
    }
  }
  {  // payload without type 1 and an empty payload both load as an empty log
    for (int variant = 0; variant < 2; variant++) {
      Bytes p;
      if (variant == 0) sec(p, 2, b5);
      cleanAll();
      CHECK(saveRaw(st, kMain, p) == Ok);
      Env dst;
      CHECK(dst.init() == Ok);
      CHECK(dst.w.set(1, 1, Layer::Fg, 9) == Ok);
      uint32_t t = 0, bl = 99;
      uint8_t out[8];
      CHECK(loadGame(st, kMain, dst.w, t, out, sizeof out, bl) == Ok);
      CHECK(dst.w.changeCount() == 0 && bl == (variant == 0 ? 5u : 0u));
    }
  }
  {  // malformed section structure
    Bytes dup1, dup2, partial, longer;
    sec(dup1, 1, empty); sec(dup1, 1, empty);
    sec(dup2, 2, b5); sec(dup2, 2, b5);
    sec(partial, 1, empty); partial.push_back(2); partial.push_back(0); partial.push_back(5);
    longer.push_back(2); longer.push_back(0); le32put(longer, 10); longer.insert(longer.end(), {1, 2, 3, 4});
    const Bytes* all[] = {&dup1, &dup2, &partial, &longer};
    for (const Bytes* p : all) {
      cleanAll();
      CHECK(saveRaw(st, kMain, *p) == Ok);
      Env dst;
      CHECK(dst.init() == Ok);
      uint32_t t = 0, bl = 5;
      uint8_t out[16];
      CHECK(loadGame(st, kMain, dst.w, t, out, sizeof out, bl) == Corrupt);
      CHECK(dst.w.changeCount() == 0 && bl == 0);
    }
  }
  CHECK(pge_host_storage_open_files() == 0);
  return 0;
}

// ================================================================ 6. fallback to .bak
static int testFallback() {
  StEnv se;
  CHECK(se.setup(7) == Ok);
  Storage& st = se.st;
  Env a, c;
  CHECK(a.init() == Ok && c.init() == Ok);
  CHECK(smallEdits(a.w, 1) == 0 && smallEdits(c.w, 2) == 0);
  const Bytes blobA = pattern(20, 1), blobC = pattern(30, 2);
  Bytes Ab, Cb;
  cleanAll();
  CHECK(saveGame(st, kMain, a.w, 100, blobA.data(), 20) == Ok && readBytes(kMain, Ab));
  cleanAll();
  CHECK(saveGame(st, kMain, c.w, 200, blobC.data(), 30) == Ok && readBytes(kMain, Cb));
  Bytes Cbad = Cb;
  Cbad[40] ^= 0x10;
  Bytes Abad = Ab;
  Abad[35] ^= 0x01;

  struct Case { const char* name; const Bytes* path; const Bytes* bak; const Bytes* tmp; Error want; int who; };
  const Case cases[] = {
      {"corrupt path, valid bak", &Cbad, &Ab, nullptr, Ok, 100},
      {"missing path, valid bak, tmp ignored", nullptr, &Ab, &Cb, Ok, 100},
      {"valid path, valid bak", &Cb, &Ab, nullptr, Ok, 200},
      {"only tmp", nullptr, nullptr, &Cb, Io, 0},
      {"both corrupt", &Cbad, &Abad, nullptr, Corrupt, 0},
      {"missing path, corrupt bak", nullptr, &Abad, nullptr, Corrupt, 0},
      {"corrupt path, missing bak", &Cbad, nullptr, nullptr, Corrupt, 0},
  };
  for (const Case& k : cases) {
    cleanAll();
    if (k.path) CHECK(writeBytes(kMain, *k.path));
    if (k.bak) CHECK(writeBytes(bakOf(kMain), *k.bak));
    if (k.tmp) CHECK(writeBytes(tmpOf(kMain), *k.tmp));
    Env d;
    CHECK(d.init() == Ok);
    uint32_t t = 0, bl = 0;
    uint8_t out[64];
    const Error e = loadGame(st, kMain, d.w, t, out, sizeof out, bl);
    if (e != k.want) {
      std::printf("fallback case '%s': got %d want %d\n", k.name, (int)e, (int)k.want);
      return 1;
    }
    if (k.want == Ok) {
      const Env& ref = k.who == 100 ? a : c;
      const Bytes& rb = k.who == 100 ? blobA : blobC;
      CHECK(t == (uint32_t)k.who && bl == rb.size() && memcmp(out, rb.data(), bl) == 0);
      CHECK(sameLog(d.w, ref.w) == 0);
    } else {
      CHECK(d.w.changeCount() == 0);
    }
  }
  {  // corrupt path: the consumer never sees its bytes, only the bak payload
    cleanAll();
    CHECK(writeBytes(kMain, Cbad) && writeBytes(bakOf(kMain), Ab));
    SaveInfo info;
    CCtx cx;
    CHECK(st.loadSave(kMain, info, cons, &cx) == Ok);
    CHECK(info.game_tick == 100 && cx.bytes == Ab.size() - 32 && !cx.bad_offset);
    CHECK(cx.data == Bytes(Ab.begin() + 32, Ab.end()));
  }
  {  // a consumer error is returned at once, no fallback to the bak
    cleanAll();
    CHECK(writeBytes(kMain, Cb) && writeBytes(bakOf(kMain), Ab));
    SaveInfo info;
    CCtx cx;
    cx.fail = Io;
    CHECK(st.loadSave(kMain, info, cons, &cx) == Io);
    CHECK(cx.calls == 1);
  }
  CHECK(pge_host_storage_open_files() == 0);
  return 0;
}

// ================================================================ 7. power loss at every I/O unit
struct Variant {
  Env env;
  Bytes blob;
  uint32_t tick = 0;
};
static int mkVar(Variant& v, int sv, uint32_t tick, uint32_t bl) {
  CHECK(v.env.init() == Ok);
  CHECK(smallEdits(v.env.w, sv) == 0);
  v.tick = tick;
  v.blob = pattern(bl, (uint32_t)sv);
  return 0;
}
// 0 = A, 1 = C, -1 = fails to load or matches neither
static int classify(Storage& st, Variant& A, Variant& C) {
  Env d;
  if (d.init() != Ok) return -2;
  uint8_t buf[64];
  uint32_t t = 0, bl = 0;
  if (loadGame(st, kMain, d.w, t, buf, sizeof buf, bl) != Ok) return -1;
  Variant* v[2] = {&A, &C};
  for (int i = 0; i < 2; i++)
    if (t == v[i]->tick && bl == v[i]->blob.size() && memcmp(buf, v[i]->blob.data(), bl) == 0 &&
        sameLog(d.w, v[i]->env.w) == 0)
      return i;
  return -1;
}

static int testCrash() {
  StEnv se;
  CHECK(se.setup(33) == Ok);
  Storage& st = se.st;
  Variant A, C, Z;
  CHECK(mkVar(A, 1, 100, 20) == 0 && mkVar(C, 2, 200, 30) == 0 && mkVar(Z, 3, 50, 10) == 0);
  Bytes Ab, Zb;
  cleanAll();
  CHECK(saveGame(st, kMain, A.env.w, 100, A.blob.data(), 20) == Ok && readBytes(kMain, Ab));
  cleanAll();
  CHECK(saveGame(st, kMain, Z.env.w, 50, Z.blob.data(), 10) == Ok && readBytes(kMain, Zb));
  Bytes Abad = Ab;
  Abad[Abad.size() - 1] ^= 0x80;
  const Bytes junk = pattern(70, 0xEE);
  auto build = [&](int s) -> bool {
    cleanAll();
    switch (s) {
      case 1: return writeBytes(kMain, Ab);
      case 2: return writeBytes(kMain, Ab) && writeBytes(bakOf(kMain), Zb);
      case 3: return writeBytes(bakOf(kMain), Ab) && writeBytes(tmpOf(kMain), junk);
      default: return writeBytes(kMain, Abad) && writeBytes(bakOf(kMain), Ab);
    }
  };
  for (int s = 1; s <= 4; s++) {
    CHECK(build(s));
    CHECK(classify(st, A, C) == 0);  // every initial state loads as A
    int covered = 0, last = 0;
    bool done = false;
    for (int budget = 0; budget < 5000; budget++) {
      CHECK(build(s));
      pge_host_storage_set_budget(budget);
      const Error e = saveGame(st, kMain, C.env.w, 200, C.blob.data(), 30);
      pge_host_storage_set_budget(-1);
      CHECK(pge_host_storage_open_files() == 0);
      const int cls = classify(st, A, C);
      if (cls < 0) {
        std::printf("state S%d budget %d (saveGame %d): save unreadable or garbage\n", s, budget, (int)e);
        return 1;
      }
      if (cls < last) {
        std::printf("state S%d budget %d: content went back from C to A\n", s, budget);
        return 1;
      }
      last = cls;
      if (e == Ok) {
        CHECK(cls == 1);
        done = true;
        break;
      }
      covered++;
      CHECK(saveGame(st, kMain, C.env.w, 200, C.blob.data(), 30) == Ok);  // recovery on the crashed disk
      CHECK(classify(st, A, C) == 1);
      CHECK(pge_host_storage_open_files() == 0);
    }
    CHECK(done);
    CHECK(last == 1);
    std::printf("  crash points covered in state S%d: %d\n", s, covered);
  }
  return 0;
}

// ================================================================ 8. saveAtomic / Storage edge cases
struct PCtx {
  int mode = 0;
  int pass = 0;
  Bytes data;
};
static Error prod(void* c, uint32_t off, void* dst, uint32_t max, uint32_t* w) {
  PCtx& x = *static_cast<PCtx*>(c);
  if (off == 0) x.pass++;
  Bytes d = x.data;
  if (x.mode == 1 && x.pass == 2) d[0] ^= 0xFF;     // other bytes, same length
  if (x.mode == 2 && x.pass == 2) d.push_back(7);   // other length
  if (x.mode == 3 && x.pass == 1) { *w = max + 1; return Ok; }
  if (x.mode == 4 && x.pass == 2) { *w = max + 1; return Ok; }
  if (x.mode == 5 && x.pass == 2) return Io;
  if (x.mode == 6 && x.pass == 1) return Busy;
  uint32_t n = 0;
  if (off < d.size()) {
    n = (uint32_t)d.size() - off;
    if (n > max) n = max;
    memcpy(dst, d.data() + off, n);
  }
  *w = n;
  return Ok;
}

static int testEdges() {
  StEnv se;
  CHECK(se.setup(7) == Ok);
  Storage& st = se.st;
  SaveInfo si;
  si.world_seed = 7; si.game_tick = 5; si.w_chunks = 4; si.h_chunks = 2;
  PCtx pc;
  pc.data = pattern(50, 4);

  {  // path length limits
    cleanAll();
    const std::string tooLong(PGE_PATH_MAX - 4, 'y');
    CHECK(st.saveAtomic(tooLong.c_str(), si, prod, &pc) == InvalidArg);
    SaveInfo out;
    CHECK(st.loadSave(tooLong.c_str(), out, nullptr, nullptr) == InvalidArg);
    CHECK(!has(tooLong) && !has(tooLong + ".tmp"));
    CHECK(st.saveAtomic(nullptr, si, prod, &pc) == InvalidArg);
    CHECK(st.saveAtomic(kMain, si, nullptr, &pc) == InvalidArg);
    CHECK(g_long.size() == PGE_PATH_MAX - 5);
    pc = PCtx(); pc.data = pattern(50, 4);
    CHECK(st.saveAtomic(g_long.c_str(), si, prod, &pc) == Ok);   // exactly the longest accepted path
    CHECK(st.saveAtomic(g_long.c_str(), si, prod, &pc) == Ok);   // second save creates the .bak at full length
    CHECK(has(g_long) && has(g_long + ".bak") && !has(g_long + ".tmp"));
    CHECK(st.loadSave(g_long.c_str(), out, nullptr, nullptr) == Ok && out.game_tick == 5);
    cleanAll();
  }
  {  // failing producers: error out, no .tmp left, an existing good file untouched, no .bak invented
    for (int mode = 1; mode <= 6; mode++) {
      cleanAll();
      PCtx good;
      good.data = pattern(50, 4);
      CHECK(st.saveAtomic(kMain, si, prod, &good) == Ok);
      Bytes before;
      CHECK(readBytes(kMain, before));
      PCtx bad;
      bad.mode = mode;
      bad.data = pattern(50, 4);
      const Error want = mode == 1 || mode == 2 ? Corrupt : mode == 3 || mode == 4 ? InvalidArg : mode == 5 ? Io : Busy;
      const Error e = st.saveAtomic(kMain, si, prod, &bad);
      if (e != want) {
        std::printf("producer mode %d: got %d want %d\n", mode, (int)e, (int)want);
        return 1;
      }
      Bytes after;
      CHECK(readBytes(kMain, after) && after == before);
      CHECK(!has(tmpOf(kMain)) && !has(bakOf(kMain)));
      CHECK(pge_host_storage_open_files() == 0);
    }
  }
  {  // Storage not usable before mount / before a buffer exists
    Storage s;
    Bytes buf(16);
    SaveInfo out;
    uint32_t len = 0;
    uint8_t tmp[4];
    CHECK(s.saveAtomic(kMain, si, prod, &pc) == NotSupported);
    CHECK(s.loadSave(kMain, out, nullptr, nullptr) == NotSupported);
    CHECK(s.readFile(kMain, tmp, 4, len) == NotSupported);
    CHECK(s.writeFile(kMain, tmp, 4) == NotSupported);
    CHECK(!s.exists(kMain));
    CHECK(s.mount() == Ok);
    CHECK(s.saveAtomic(kMain, si, prod, &pc) == NotInitialized);
    CHECK(s.readFile(kMain, tmp, 4, len) == NotInitialized);
    s.setBuffer(buf.data(), 16);
    cleanAll();
    pc = PCtx(); pc.data = pattern(50, 4);
    CHECK(s.saveAtomic(kMain, si, prod, &pc) == Ok);
    CHECK(s.exists(kMain));
  }
  {  // readFile / writeFile / exists
    cleanAll();
    const Bytes d = pattern(100, 6);
    CHECK(!st.exists(kAux));
    CHECK(st.writeFile(kAux, d.data(), 100) == Ok);
    CHECK(st.exists(kAux) && !st.exists(nullptr));
    Bytes raw;
    CHECK(readBytes(kAux, raw) && raw == d);  // plain file, no header
    Bytes out(200, 0xCC);
    uint32_t len = 999;
    CHECK(st.readFile(kAux, out.data(), 200, len) == Ok && len == 100 && memcmp(out.data(), d.data(), 100) == 0);
    len = 999;
    CHECK(st.readFile(kAux, out.data(), 100, len) == Ok && len == 100);       // cap == size
    CHECK(st.readFile(kAux, out.data(), 99, len) == OutOfMemory);            // longer than cap
    CHECK(st.readFile(kAux, nullptr, 0, len) == OutOfMemory);
    CHECK(st.writeFile(kAux, nullptr, 0) == Ok);                              // truncates to empty
    len = 999;
    CHECK(st.readFile(kAux, out.data(), 200, len) == Ok && len == 0);
    len = 999;
    CHECK(st.readFile(kAux, nullptr, 0, len) == Ok && len == 0);
    CHECK(st.writeFile(kAux, d.data(), 10) == Ok);                            // overwrite shortens
    CHECK(st.readFile(kAux, out.data(), 200, len) == Ok && len == 10);
    pge_hal_storage_remove(kAux);
    CHECK(!st.exists(kAux));
    CHECK(st.readFile(kAux, out.data(), 200, len) == Io);
    CHECK(st.writeFile(nullptr, d.data(), 1) == InvalidArg);
    CHECK(st.readFile(nullptr, out.data(), 1, len) == InvalidArg);
  }
  CHECK(pge_host_storage_open_files() == 0);
  return 0;
}

int main() {
  g_long = std::string("pge_ts_") + std::string(PGE_PATH_MAX - 5 - 7, 'x');
  g_props[1].flags = kTileSolid;
  g_props[2].flags = kTileSolid;
  struct T { const char* name; int (*fn)(); };
  const T tests[] = {
      {"crc32", testCrc},
      {"round trip", testRoundTrip},
      {"writer windows", testWindows},
      {"change reader", testReader},
      {"load failure clears log", testLoadFailClears},
      {"file corruption", testCorruptFile},
      {"bak fallback", testFallback},
      {"crash exhaustive", testCrash},
      {"edge cases", testEdges},
  };
  int failed = 0;
  cleanAll();
  for (const T& t : tests) {
    cleanAll();
    const int r = t.fn();
    pge_host_storage_set_budget(-1);
    if (r != 0) {
      std::printf("FAIL %s\n", t.name);
      failed++;
    } else {
      std::printf("PASS %s\n", t.name);
    }
    cleanAll();
  }
  cleanAll();
  std::printf(failed ? "%d test(s) FAILED\n" : "all storage tests passed\n", failed);
  return failed ? 1 : 0;
}
