// Settings + Resources tests (S5.3a pass 2): key=value parsing and limits, getInt/getStr, load() through a real file,
// ROM and storage-backed assets, error codes, arena use. Files live in the current directory.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include "host_storage.h"
#include "pge/config.h"
#include "pge/hal/storage.h"
#include "pge/resource.h"
#include "pge/settings.h"

#define CHECK(c)                                                          \
  do {                                                                    \
    if (!(c)) {                                                           \
      std::printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); \
      return 1;                                                           \
    }                                                                     \
  } while (0)

using namespace pge;

static const char* kCfgFile = "pge_tset_cfg.txt";
static const char* kAssetFile = "pge_tset_asset.bin";

struct Fx {
  alignas(8) uint8_t buf[2048];
  Arena arena;
  Settings s;
  Error init(uint16_t n = 0) {
    arena.init(buf, sizeof(buf));
    return s.init(arena, n);
  }
  void parse(const char* t) { s.parse(t, (uint32_t)std::strlen(t)); }
};

static std::string rep(char c, size_t n) { return std::string(n, c); }

static int testParse() {
  Fx f;
  CHECK(f.init() == Ok);
  f.parse("a=1\n  b \t=\t two words  \r\n# c=3\n  # d=4\n\n   \t\nd=x=y\ne=5");  // no final newline
  CHECK(f.s.count() == 4 && f.s.skipped() == 0);
  CHECK(std::strcmp(f.s.getStr("a", "?"), "1") == 0);
  CHECK(std::strcmp(f.s.getStr("b", "?"), "two words") == 0);  // trimmed, inner space kept
  CHECK(std::strcmp(f.s.getStr("d", "?"), "x=y") == 0);        // '=' inside the value kept
  CHECK(f.s.getInt("e", 0) == 5);
  CHECK(std::strcmp(f.s.getStr("c", "none"), "none") == 0);  // comment ignored
  CHECK(std::strcmp(f.s.getStr("A", "none"), "none") == 0);  // case sensitive

  f.parse("a=9\n");  // repeat key replaces
  CHECK(f.s.count() == 4 && f.s.getInt("a", 0) == 9);
  f.parse("g=\n");  // empty value is allowed, getInt gives the default
  CHECK(f.s.count() == 5 && f.s.getInt("g", 77) == 77 && std::strcmp(f.s.getStr("g", "?"), "") == 0);
  CHECK(f.s.skipped() == 0);

  // len is respected: never reads past it, text need not be NUL-terminated.
  Fx g;
  CHECK(g.init() == Ok);
  const char t[] = {'a', '=', '7', '8', '9'};
  g.s.parse(t, 3);
  CHECK(g.s.count() == 1 && std::strcmp(g.s.getStr("a", "?"), "7") == 0);
  g.s.parse("x=5\r", 4);  // CR at the very end of the text is a line end too
  CHECK(g.s.getInt("x", 0) == 5);
  g.s.parse("y=1\nzz", 6);  // trailing "zz" has no '='
  CHECK(g.s.getInt("y", 0) == 1 && g.s.count() == 3 && g.s.skipped() == 1);
  g.s.parse("q=1", 0);  // len 0 parses nothing
  CHECK(g.s.count() == 3);
  return 0;
}

static int testSkipped() {
  Fx f;
  CHECK(f.init() == Ok);
  f.parse("novalue\n");
  CHECK(f.s.skipped() == 1 && f.s.count() == 0);
  f.parse("=v\n");
  CHECK(f.s.skipped() == 2 && f.s.count() == 0);
  f.parse("  \t = v\n");  // key is only blanks
  CHECK(f.s.skipped() == 3 && f.s.count() == 0);

  const std::string k23 = rep('k', 23), k24 = rep('k', 24), v31 = rep('v', 31), v32 = rep('v', 32);
  f.parse((k24 + "=1\n").c_str());
  CHECK(f.s.skipped() == 4 && f.s.count() == 0);
  f.parse((k23 + "=1\n").c_str());
  CHECK(f.s.skipped() == 4 && f.s.count() == 1 && f.s.getInt(k23.c_str(), 0) == 1);
  f.parse(("a=" + v32 + "\n").c_str());
  CHECK(f.s.skipped() == 5 && f.s.count() == 1 && std::strcmp(f.s.getStr("a", "none"), "none") == 0);
  f.parse(("a=" + v31 + "\n").c_str());
  CHECK(f.s.skipped() == 5 && f.s.count() == 2 && std::strcmp(f.s.getStr("a", "?"), v31.c_str()) == 0);
  f.parse(("a=" + v32 + "\n").c_str());  // over-long value does not touch the old one
  CHECK(f.s.skipped() == 6 && std::strcmp(f.s.getStr("a", "?"), v31.c_str()) == 0);
  CHECK(f.s.getStr(k24.c_str(), nullptr) == nullptr);  // a prefix key is a different key
  CHECK(f.s.getInt(k23.c_str(), 0) == 1);

  // table full: new keys skipped and counted, existing keys can still be replaced
  Fx g;
  CHECK(g.init(3) == Ok);
  g.parse("a=1\nb=2\nc=3\nd=4\n");
  CHECK(g.s.count() == 3 && g.s.skipped() == 1 && g.s.getInt("d", -1) == -1);
  g.parse("b=20\n");
  CHECK(g.s.count() == 3 && g.s.skipped() == 1 && g.s.getInt("b", 0) == 20);

  // default capacity follows PGE_CONFIG_MAX_KEYS
  Fx h;
  CHECK(h.init() == Ok);
  std::string many;
  for (int i = 0; i < PGE_CONFIG_MAX_KEYS + 2; i++) many += "k" + std::to_string(i) + "=1\n";
  h.parse(many.c_str());
  CHECK(h.s.count() == PGE_CONFIG_MAX_KEYS && h.s.skipped() == 2);

  // arena too small for the table
  alignas(8) uint8_t tiny[16];
  Arena a;
  a.init(tiny, sizeof(tiny));
  Settings s;
  CHECK(s.init(a, 4) == OutOfMemory);
  // uninitialised Settings: everything is skipped, getters give defaults
  Settings u;
  u.parse("a=1\n", 4);
  CHECK(u.count() == 0 && u.skipped() == 1 && u.getInt("a", 5) == 5);
  return 0;
}

static int testGetInt() {
  Fx f;
  CHECK(f.init() == Ok);
  f.parse(
      "dec=123\nneg=-45\nplus=+7\nzeros=007\nhex=0x1F\nHEX=0XfF\nnhex=-0x10\nmaxh=0x7FFFFFFF\nbigh=0x80000000\n"
      "max=2147483647\nover=2147483648\nmin=-2147483648\nunder=-2147483649\nhuge=99999999999999999999\n"
      "junk=12abc\nalpha=abc\ninner=1 2\nsign=+\nminus=-\nox=0x\nbadhex=0xZZ\nempty=\nhexindec=1a\n");
  CHECK(f.s.skipped() == 0);
  CHECK(f.s.getInt("dec", 0) == 123);
  CHECK(f.s.getInt("neg", 0) == -45);
  CHECK(f.s.getInt("plus", 0) == 7);
  CHECK(f.s.getInt("zeros", 0) == 7);
  CHECK(f.s.getInt("hex", 0) == 31);
  CHECK(f.s.getInt("HEX", 0) == 255);
  CHECK(f.s.getInt("nhex", 0) == -16);
  CHECK(f.s.getInt("maxh", 0) == 2147483647);
  CHECK(f.s.getInt("bigh", 99) == 99);
  CHECK(f.s.getInt("max", 0) == 2147483647);
  CHECK(f.s.getInt("over", 99) == 99);
  CHECK(f.s.getInt("min", 0) == INT32_MIN);
  CHECK(f.s.getInt("under", 99) == 99);
  CHECK(f.s.getInt("huge", 99) == 99);
  CHECK(f.s.getInt("junk", 99) == 99);
  CHECK(f.s.getInt("alpha", 99) == 99);
  CHECK(f.s.getInt("inner", 99) == 99);
  CHECK(f.s.getInt("sign", 99) == 99);
  CHECK(f.s.getInt("minus", 99) == 99);
  CHECK(f.s.getInt("ox", 99) == 99);
  CHECK(f.s.getInt("badhex", 99) == 99);
  CHECK(f.s.getInt("empty", 99) == 99);
  CHECK(f.s.getInt("hexindec", 99) == 99);
  CHECK(f.s.getInt("missing", -3) == -3);
  CHECK(f.s.getInt(nullptr, -3) == -3);
  CHECK(std::strcmp(f.s.getStr("missing", "dflt"), "dflt") == 0);
  CHECK(f.s.getStr("missing", nullptr) == nullptr);
  CHECK(std::strcmp(f.s.getStr("junk", "dflt"), "12abc") == 0);
  return 0;
}

static int testLoad(Storage& st) {
  const char text[] = "a = 1\nname=pge\nz=0x10";  // no final newline, no NUL written
  const uint32_t n = (uint32_t)(sizeof(text) - 1);
  CHECK(st.writeFile(kCfgFile, text, n) == Ok);

  char scratch[64];
  std::memset(scratch, 'Q', sizeof(scratch));  // garbage after the file: parse must stop at len
  Fx f;
  CHECK(f.init() == Ok);
  CHECK(f.s.load(st, kCfgFile, scratch, sizeof(scratch)) == Ok);
  CHECK(f.s.count() == 3 && f.s.skipped() == 0);
  CHECK(f.s.getInt("a", 0) == 1 && std::strcmp(f.s.getStr("name", "?"), "pge") == 0 && f.s.getInt("z", 0) == 16);

  Fx e;  // exact-size scratch is enough
  CHECK(e.init() == Ok);
  CHECK(e.s.load(st, kCfgFile, scratch, n) == Ok && e.s.count() == 3);

  Fx m;  // missing file: Io, settings untouched
  CHECK(m.init() == Ok);
  CHECK(m.s.load(st, "pge_tset_nofile.txt", scratch, sizeof(scratch)) == Io);
  CHECK(m.s.count() == 0 && m.s.skipped() == 0);

  Fx t;  // scratch too small: OutOfMemory, settings untouched
  CHECK(t.init() == Ok);
  CHECK(t.s.load(st, kCfgFile, scratch, n - 1) == OutOfMemory);
  CHECK(t.s.count() == 0 && t.s.skipped() == 0);
  return 0;
}

static const uint8_t kRom[5] = {1, 2, 3, 4, 5};

static int testResources(Storage& st) {
  const uint8_t file[6] = {10, 11, 12, 13, 14, 15};
  CHECK(st.writeFile(kAssetFile, file, sizeof(file)) == Ok);

  static const AssetEntry table[] = {
      {AssetType::Tileset, kRom, 5, nullptr},                        // 0 ROM
      {AssetType::Sound, nullptr, 6, kAssetFile},                    // 1 storage, size matches
      {AssetType::Blob, nullptr, 7, kAssetFile},                     // 2 storage, file shorter than size
      {AssetType::Blob, nullptr, 6, "pge_tset_missing.bin"},         // 3 storage, no such file
      {AssetType::Font, nullptr, 4, kAssetFile},                     // 4 storage, file longer than size
      {AssetType::Pattern, kRom, 0, nullptr},                        // 5 empty ROM blob
  };
  const uint16_t N = sizeof(table) / sizeof(table[0]);
  Resources r;
  r.init(table, N, &st);

  AssetInfo ai{};
  CHECK(r.info(0, ai) == Ok && ai.type == AssetType::Tileset && ai.size == 5 && ai.in_rom);
  CHECK(r.info(1, ai) == Ok && ai.type == AssetType::Sound && ai.size == 6 && !ai.in_rom);
  CHECK(r.info(N, ai) == InvalidArg && r.info(65535, ai) == InvalidArg);
  CHECK(r.rom(0) == kRom && r.rom(1) == nullptr && r.rom(N) == nullptr && r.rom(65535) == nullptr);

  // ROM load
  uint8_t dst[16];
  uint32_t len = 99;
  std::memset(dst, 0xEE, sizeof(dst));
  CHECK(r.load(0, dst, sizeof(dst), len) == Ok && len == 5 && std::memcmp(dst, kRom, 5) == 0 && dst[5] == 0xEE);
  len = 99;
  CHECK(r.load(0, dst, 5, len) == Ok && len == 5);
  len = 99;
  CHECK(r.load(0, dst, 4, len) == OutOfMemory && len == 99);
  CHECK(r.load(0, nullptr, 16, len) == InvalidArg);
  CHECK(r.load(N, dst, sizeof(dst), len) == InvalidArg);
  CHECK(r.load(5, nullptr, 0, len) == Ok && len == 0);

  // storage load
  std::memset(dst, 0xEE, sizeof(dst));
  len = 99;
  CHECK(r.load(1, dst, sizeof(dst), len) == Ok && len == 6 && std::memcmp(dst, file, 6) == 0 && dst[6] == 0xEE);
  len = 99;
  CHECK(r.load(1, dst, 5, len) == OutOfMemory);
  CHECK(r.load(1, nullptr, 16, len) == InvalidArg);
  CHECK(r.load(2, dst, sizeof(dst), len) == Corrupt);
  CHECK(r.load(3, dst, sizeof(dst), len) == Io);
  CHECK(r.load(4, dst, sizeof(dst), len) == Corrupt);   // file 6 B, entry says 4
  CHECK(r.load(4, dst, 4, len) == OutOfMemory);         // readFile sees the file is longer than cap

  // loadPersistent: ROM is zero-copy and uses no arena
  alignas(8) uint8_t abuf[256];
  Arena arena;
  arena.init(abuf, sizeof(abuf));
  const void* out = nullptr;
  CHECK(r.loadPersistent(arena, 0, &out) == Ok && out == kRom && arena.used() == 0);

  // storage: allocated from the arena, bytes are the file's
  out = nullptr;
  CHECK(r.loadPersistent(arena, 1, &out) == Ok && out != nullptr && out != file);
  CHECK(arena.used() == 6 && std::memcmp(out, file, 6) == 0);
  CHECK(r.loadPersistent(arena, 2, &out) == Corrupt);
  CHECK(r.loadPersistent(arena, 3, &out) == Io);
  CHECK(r.loadPersistent(arena, N, &out) == InvalidArg);
  CHECK(r.loadPersistent(arena, 1, nullptr) == InvalidArg);

  // arena locked (after the first tick): storage assets fail, ROM assets still work
  arena.lock();
  const uint32_t failed = arena.failed();
  const void* sentinel = &arena;
  out = sentinel;
  CHECK(r.loadPersistent(arena, 1, &out) == OutOfMemory && out == sentinel && arena.failed() == failed + 1);
  CHECK(r.loadPersistent(arena, 0, &out) == Ok && out == kRom);

  // arena exhausted
  Arena small;
  small.init(abuf, 4);
  CHECK(r.loadPersistent(small, 1, &out) == OutOfMemory);

  // no Storage: ROM only
  Resources ro;
  ro.init(table, N, nullptr);
  Arena a2;
  a2.init(abuf, sizeof(abuf));
  CHECK(ro.load(1, dst, sizeof(dst), len) == NotSupported);
  CHECK(ro.loadPersistent(a2, 1, &out) == NotSupported && a2.used() == 0);
  CHECK(ro.load(0, dst, sizeof(dst), len) == Ok && len == 5);
  CHECK(ro.loadPersistent(a2, 0, &out) == Ok && out == kRom);

  // empty table
  Resources none;
  none.init(nullptr, 7, &st);
  CHECK(none.info(0, ai) == InvalidArg && none.rom(0) == nullptr);
  return 0;
}

int main() {
  alignas(8) static uint8_t io[33];
  Storage st;
  st.setBuffer(io, sizeof(io));
  if (st.mount() != Ok) {
    std::printf("mount failed\n");
    return 1;
  }
  int rc = testParse();
  if (!rc) rc = testSkipped();
  if (!rc) rc = testGetInt();
  if (!rc) rc = testLoad(st);
  if (!rc) rc = testResources(st);
  pge_hal_storage_remove(kCfgFile);
  pge_hal_storage_remove(kAssetFile);
  if (!rc && pge_host_storage_open_files() != 0) {
    std::printf("leaked open files: %d\n", (int)pge_host_storage_open_files());
    rc = 1;
  }
  if (rc == 0) std::printf("test_settings: all passed\n");
  return rc;
}
