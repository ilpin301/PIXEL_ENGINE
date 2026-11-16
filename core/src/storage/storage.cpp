#include "pge/storage.h"
#include <string.h>
#include "pge/config.h"
#include "pge/hal/storage.h"

namespace pge {

namespace {

constexpr uint32_t kHdrSize = 32;
constexpr uint32_t kMaxPayload = 0x7FFFFFFFu;

// 16-entry nibble table of CRC-32/IEEE (64 B ROM).
const uint32_t kCrcNib[16] = {0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC, 0x76DC4190, 0x6B6B51F4,
                              0x4DB26158, 0x5005713C, 0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C,
                              0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C};

uint16_t le16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
uint32_t le32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
void put16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
void put32(uint8_t* p, uint32_t v) {
  p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

// Reads until n bytes arrived or end of file; got < n means end of file.
Error readUpTo(pge_file f, uint8_t* dst, uint32_t n, uint32_t& got) {
  got = 0;
  while (got < n) {
    const int32_t r = pge_hal_storage_read(f, dst + got, n - got);
    if (r < 0) return (Error)r;
    if (r == 0) break;
    got += (uint32_t)r;
  }
  return Ok;
}

Error writeAll(pge_file f, const uint8_t* src, uint32_t n) {
  while (n) {
    const int32_t w = pge_hal_storage_write(f, src, n);
    if (w < 0) return (Error)w;
    if (w == 0) return Io;
    src += w;
    n -= (uint32_t)w;
  }
  return Ok;
}

// Runs one producer pass: every chunk goes into crc/len and, when f >= 0, to the file.
Error producePass(Producer produce, void* ctx, uint8_t* io, uint32_t io_bytes, pge_file f, uint32_t& crc,
                  uint32_t& len) {
  crc = 0;
  len = 0;
  for (;;) {
    uint32_t w = 0;
    const Error e = produce(ctx, len, io, io_bytes, &w);
    if (e != Ok) return e;
    if (w == 0) return Ok;
    if (w > io_bytes) return InvalidArg;
    if (len > kMaxPayload - w) return InvalidArg;
    crc = crc32Update(crc, io, w);
    if (f >= 0) {
      const Error we = writeAll(f, io, w);
      if (we != Ok) return we;
    }
    len += w;
  }
}

// path + suffix into out (PGE_PATH_MAX bytes); the caller checked strlen(path) <= PGE_PATH_MAX - 5.
void withSuffix(char* out, const char* path, const char* suffix) {
  const size_t n = strlen(path);
  memcpy(out, path, n);
  memcpy(out + n, suffix, 5);  // ".tmp" or ".bak" plus NUL
}

}  // namespace

uint32_t crc32Update(uint32_t crc, const void* data, uint32_t n) {
  const uint8_t* p = static_cast<const uint8_t*>(data);
  crc = ~crc;
  while (n--) {
    crc ^= *p++;
    crc = kCrcNib[crc & 15] ^ (crc >> 4);
    crc = kCrcNib[crc & 15] ^ (crc >> 4);
  }
  return ~crc;
}

Error Storage::init(Arena& a, uint32_t io_bytes) {
  if (io_bytes == 0) io_bytes = PGE_STORAGE_IO_BYTES;
  uint8_t* b = a.allocArray<uint8_t>(io_bytes);
  if (!b) return OutOfMemory;
  buf_ = b;
  cap_ = io_bytes;
  return Ok;
}

void Storage::setBuffer(void* buf, uint32_t bytes) {
  buf_ = static_cast<uint8_t*>(buf);
  cap_ = bytes;
}

Error Storage::mount() {
  const int rc = pge_hal_storage_mount();
  mounted_ = rc == 0;
  return (Error)rc;
}

Error Storage::ready() const {
  if (!mounted_) return NotSupported;
  if (!buf_ || cap_ == 0) return NotInitialized;
  return Ok;
}

bool Storage::exists(const char* path) const {
  if (!mounted_ || !path) return false;
  pge_file f;
  if (pge_hal_storage_open(path, 0, &f) != 0) return false;
  pge_hal_storage_close(f);
  return true;
}

Error Storage::readFile(const char* path, void* buf, uint32_t cap, uint32_t& len) {
  Error e = ready();
  if (e != Ok) return e;
  if (!path || (!buf && cap)) return InvalidArg;
  pge_file f;
  if (pge_hal_storage_open(path, 0, &f) != 0) return Io;
  uint32_t got = 0;
  e = readUpTo(f, static_cast<uint8_t*>(buf), cap, got);
  if (e == Ok && got == cap) {  // buffer full: any further byte means the file is too long
    uint8_t probe;
    const int32_t r = pge_hal_storage_read(f, &probe, 1);
    if (r < 0) e = (Error)r;
    else if (r > 0) e = OutOfMemory;
  }
  const int cr = pge_hal_storage_close(f);
  if (e == Ok && cr != 0) e = (Error)cr;
  if (e == Ok) len = got;
  return e;
}

Error Storage::writeFile(const char* path, const void* data, uint32_t len) {
  Error e = ready();
  if (e != Ok) return e;
  if (!path || (!data && len)) return InvalidArg;
  pge_file f;
  const int rc = pge_hal_storage_open(path, 1, &f);
  if (rc != 0) return (Error)rc;
  e = writeAll(f, static_cast<const uint8_t*>(data), len);
  const int cr = pge_hal_storage_close(f);
  return e != Ok ? e : (Error)cr;
}

// Opens one save file, validates header and CRC, and optionally streams the payload to consume (second pass, so the
// CRC is known good before the first consume call). consumed tells the caller whether consume ran.
Error Storage::loadOne(const char* path, SaveInfo& info, Consumer consume, void* ctx, bool& consumed) {
  consumed = false;
  pge_file f;
  const int orc = pge_hal_storage_open(path, 0, &f);
  if (orc != 0) return (Error)orc;

  uint8_t h[kHdrSize];
  uint32_t got = 0;
  uint32_t plen = 0;
  Error e = readUpTo(f, h, kHdrSize, got);
  if (e == Ok) {
    if (got < kHdrSize || memcmp(h, "PGES", 4) != 0 || le16(h + 4) != 1 || le16(h + 6) != kHdrSize ||
        crc32Update(0, h, 28) != le32(h + 28))
      e = Corrupt;
  }
  if (e == Ok) {
    plen = le32(h + 16);
    uint32_t crc = 0, total = 0;
    for (;;) {
      const int32_t r = pge_hal_storage_read(f, buf_, cap_);
      if (r < 0) { e = (Error)r; break; }
      if (r == 0) break;
      if ((uint32_t)r > plen - total) { e = Corrupt; break; }  // trailing bytes
      crc = crc32Update(crc, buf_, (uint32_t)r);
      total += (uint32_t)r;
    }
    if (e == Ok && (total != plen || crc != le32(h + 24))) e = Corrupt;  // truncated or bad CRC
  }
  const int cr = pge_hal_storage_close(f);
  if (e == Ok && cr != 0) e = (Error)cr;
  if (e != Ok) return e;

  info.world_seed = le32(h + 8);
  info.w_chunks = le16(h + 12);
  info.h_chunks = le16(h + 14);
  info.game_tick = le32(h + 20);
  if (!consume) return Ok;

  const int rrc = pge_hal_storage_open(path, 0, &f);
  if (rrc != 0) return (Error)rrc;
  e = readUpTo(f, h, kHdrSize, got);
  if (e == Ok && got < kHdrSize) e = Corrupt;
  uint32_t off = 0;
  while (e == Ok && off < plen) {
    const uint32_t want = plen - off < cap_ ? plen - off : cap_;
    const int32_t r = pge_hal_storage_read(f, buf_, want);
    if (r < 0) { e = (Error)r; break; }
    if (r == 0) { e = Corrupt; break; }  // file shrank between the passes
    consumed = true;
    e = consume(ctx, off, buf_, (uint32_t)r);
    off += (uint32_t)r;
  }
  pge_hal_storage_close(f);
  return e;
}

Error Storage::loadSave(const char* path, SaveInfo& info, Consumer consume, void* ctx) {
  Error e = ready();
  if (e != Ok) return e;
  if (!path || strlen(path) > PGE_PATH_MAX - 5) return InvalidArg;
  bool consumed = false;
  const Error e1 = loadOne(path, info, consume, ctx, consumed);
  if (e1 == Ok || consumed) return e1;
  char bak[PGE_PATH_MAX];
  withSuffix(bak, path, ".bak");
  const Error e2 = loadOne(bak, info, consume, ctx, consumed);
  if (e2 == Ok || consumed) return e2;
  return e1 == Io ? e2 : e1;
}

Error Storage::saveAtomic(const char* path, const SaveInfo& info, Producer produce, void* ctx) {
  Error e = ready();
  if (e != Ok) return e;
  if (!path || !produce || strlen(path) > PGE_PATH_MAX - 5) return InvalidArg;
  char tmp[PGE_PATH_MAX], bak[PGE_PATH_MAX];
  withSuffix(tmp, path, ".tmp");
  withSuffix(bak, path, ".bak");

  uint32_t crc, len;
  e = producePass(produce, ctx, buf_, cap_, -1, crc, len);
  if (e != Ok) return e;

  uint8_t h[kHdrSize];
  memcpy(h, "PGES", 4);
  put16(h + 4, 1);
  put16(h + 6, kHdrSize);
  put32(h + 8, info.world_seed);
  put16(h + 12, info.w_chunks);
  put16(h + 14, info.h_chunks);
  put32(h + 16, len);
  put32(h + 20, info.game_tick);
  put32(h + 24, crc);
  put32(h + 28, crc32Update(0, h, 28));

  pge_file f;
  const int orc = pge_hal_storage_open(tmp, 1, &f);
  if (orc != 0) return (Error)orc;
  uint32_t crc2 = 0, len2 = 0;
  e = writeAll(f, h, kHdrSize);
  if (e == Ok) e = producePass(produce, ctx, buf_, cap_, f, crc2, len2);
  if (e == Ok && (len2 != len || crc2 != crc)) e = Corrupt;  // producer was not deterministic
  const int cr = pge_hal_storage_close(f);
  if (e == Ok && cr != 0) e = (Error)cr;
  if (e == Ok) {
    SaveInfo vi;
    bool c;
    e = loadOne(tmp, vi, nullptr, nullptr, c);  // full read-back
  }
  if (e != Ok) {
    pge_hal_storage_remove(tmp);
    return e;
  }

  // If path is a valid save it becomes bak; if it is missing or corrupt, bak may be the only good copy, so it stays.
  // Either way the disk holds at least one loadable file (path or bak) at every step.
  SaveInfo vi;
  bool c;
  if (loadOne(path, vi, nullptr, nullptr, c) == Ok) {
    int rc;
    if (exists(bak) && (rc = pge_hal_storage_remove(bak)) != 0) return (Error)rc;
    if ((rc = pge_hal_storage_rename(path, bak)) != 0) return (Error)rc;
  } else {
    int rc;
    if (exists(path) && (rc = pge_hal_storage_remove(path)) != 0) return (Error)rc;
  }
  const int rc = pge_hal_storage_rename(tmp, path);
  return (Error)rc;
}

}  // namespace pge
