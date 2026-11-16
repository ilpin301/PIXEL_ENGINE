// One RAM disk behind both the fake FatFs and the fake LittleFS API, plus the host test hooks
// (budget / open-file count) with the same semantics as platform/host/storage_file.cpp.
// Compiled into each test executable (not via a library) so the binder below always runs.
#include <stdint.h>
#include <string.h>
#include <map>
#include <string>
#include <vector>
#include "host_storage.h"
#include "ff.h"
#include "lfs.h"
#ifdef FAKE_BIND_FATFS
#include "pge_storage_fatfs.h"
#endif
#ifdef FAKE_BIND_LFS
#include "pge_storage_littlefs.h"
#endif

namespace {

typedef std::vector<uint8_t> Bytes;
struct Ent {
  bool used;
  std::string name;
  size_t pos;
  bool writable;
};
enum { kEnts = 16 };

std::map<std::string, Bytes> g_files;
Ent g_ents[kEnts];
int64_t g_budget = -1;
bool g_fat_mounted;

// Returns how many of n units are allowed and charges them to the budget.
uint32_t consume(uint32_t n) {
  if (g_budget < 0) return n;
  uint32_t ok = (uint64_t)g_budget < n ? (uint32_t)g_budget : n;
  g_budget -= ok;
  return ok;
}

// Returns the new handle (index + 1) or 0 when the table is full.
int openEnt(const char* path, bool writable) {
  for (int i = 0; i < kEnts; i++) {
    if (g_ents[i].used) continue;
    g_ents[i].used = true;
    g_ents[i].name = path;
    g_ents[i].pos = 0;
    g_ents[i].writable = writable;
    return i + 1;
  }
  return 0;
}

Ent* entOf(int h) { return (h >= 1 && h <= kEnts && g_ents[h - 1].used) ? &g_ents[h - 1] : nullptr; }

// Shared byte-level read / write. Returns false when the handle has the wrong mode.
bool doRead(int h, void* buf, uint32_t n, uint32_t* got) {
  Ent* e = entOf(h);
  if (!e || e->writable) return false;
  const Bytes& d = g_files[e->name];
  size_t avail = e->pos < d.size() ? d.size() - e->pos : 0;
  *got = n < avail ? n : (uint32_t)avail;
  if (*got) memcpy(buf, d.data() + e->pos, *got);
  e->pos += *got;
  return true;
}

// *allowed = bytes actually written (budget-limited prefix is kept).
bool doWrite(int h, const void* buf, uint32_t n, uint32_t* allowed) {
  Ent* e = entOf(h);
  if (!e || !e->writable) return false;
  *allowed = consume(n);
  Bytes& d = g_files[e->name];
  if (e->pos + *allowed > d.size()) d.resize(e->pos + *allowed);
  if (*allowed) memcpy(d.data() + e->pos, buf, *allowed);
  e->pos += *allowed;
  return true;
}

}  // namespace

extern "C" {

void pge_host_storage_set_budget(int32_t units) { g_budget = units; }

int32_t pge_host_storage_open_files(void) {
  int32_t n = 0;
  for (int i = 0; i < kEnts; i++) n += g_ents[i].used;
  return n;
}

// ---- FatFs ----

FRESULT f_mount(FATFS* fs, const TCHAR*, BYTE) {
  if (!fs) return FR_INVALID_OBJECT;
  fs->mounted = 1;
  g_fat_mounted = true;
  return FR_OK;
}

FRESULT f_open(FIL* fp, const TCHAR* path, BYTE mode) {
  if (!fp || !path) return FR_INVALID_OBJECT;
  if (!g_fat_mounted) return FR_NOT_ENABLED;
  bool w = (mode & FA_WRITE) != 0;
  if (w) {
    if (consume(1) < 1) return FR_DISK_ERR;
  } else if (!g_files.count(path)) {
    return FR_NO_FILE;
  }
  int h = openEnt(path, w);
  if (!h) return FR_TOO_MANY_OPEN_FILES;
  if (w) g_files[path].clear();
  fp->h = h;
  return FR_OK;
}

FRESULT f_close(FIL* fp) {
  Ent* e = fp ? entOf(fp->h) : nullptr;
  if (!e) return FR_INVALID_OBJECT;
  e->used = false;
  fp->h = 0;
  return FR_OK;
}

FRESULT f_read(FIL* fp, void* buff, UINT btr, UINT* br) {
  *br = 0;
  uint32_t got = 0;
  if (!fp || !doRead(fp->h, buff, btr, &got)) return FR_INVALID_OBJECT;
  *br = got;
  return FR_OK;
}

FRESULT f_write(FIL* fp, const void* buff, UINT btw, UINT* bw) {
  *bw = 0;
  uint32_t put = 0;
  if (!fp || !doWrite(fp->h, buff, btw, &put)) return FR_INVALID_OBJECT;
  *bw = put;
  return put < btw ? FR_DISK_ERR : FR_OK;
}

FRESULT f_rename(const TCHAR* from, const TCHAR* to) {
  if (consume(1) < 1) return FR_DISK_ERR;
  std::map<std::string, Bytes>::iterator it = g_files.find(from);
  if (it == g_files.end()) return FR_NO_FILE;
  if (g_files.count(to)) return FR_EXIST;
  g_files[to].swap(it->second);
  g_files.erase(from);
  return FR_OK;
}

FRESULT f_unlink(const TCHAR* path) {
  if (consume(1) < 1) return FR_DISK_ERR;
  return g_files.erase(path) ? FR_OK : FR_NO_FILE;
}

// ---- LittleFS ----

int lfs_mount(lfs_t* lfs, const struct lfs_config*) {
  if (!lfs) return LFS_ERR_INVAL;
  lfs->mounted = 1;
  return 0;
}

int lfs_file_opencfg(lfs_t* lfs, lfs_file_t* file, const char* path, int flags, const struct lfs_file_config* cfg) {
  if (!lfs || !lfs->mounted || !file || !path) return LFS_ERR_INVAL;
  if (!cfg || !cfg->buffer) return LFS_ERR_INVAL;  // the backend must hand over its static buffer
  bool w = (flags & LFS_O_WRONLY) != 0;
  if (w) {
    if (consume(1) < 1) return LFS_ERR_IO;
  } else if (!g_files.count(path)) {
    return LFS_ERR_NOENT;
  }
  int h = openEnt(path, w);
  if (!h) return LFS_ERR_INVAL;
  if (w) g_files[path].clear();
  file->h = h;
  return 0;
}

lfs_ssize_t lfs_file_read(lfs_t* lfs, lfs_file_t* file, void* buffer, lfs_size_t size) {
  uint32_t got = 0;
  if (!lfs || !lfs->mounted || !file || !doRead(file->h, buffer, size, &got)) return LFS_ERR_INVAL;
  return (lfs_ssize_t)got;
}

lfs_ssize_t lfs_file_write(lfs_t* lfs, lfs_file_t* file, const void* buffer, lfs_size_t size) {
  uint32_t put = 0;
  if (!lfs || !lfs->mounted || !file || !doWrite(file->h, buffer, size, &put)) return LFS_ERR_INVAL;
  return put < size ? LFS_ERR_IO : (lfs_ssize_t)put;
}

int lfs_file_close(lfs_t*, lfs_file_t* file) {
  Ent* e = file ? entOf(file->h) : nullptr;
  if (!e) return LFS_ERR_INVAL;
  e->used = false;
  file->h = 0;
  return 0;
}

int lfs_rename(lfs_t*, const char* from, const char* to) {
  if (consume(1) < 1) return LFS_ERR_IO;
  std::map<std::string, Bytes>::iterator it = g_files.find(from);
  if (it == g_files.end()) return LFS_ERR_NOENT;
  if (std::string(from) == to) return 0;
  g_files[to].swap(it->second);  // replaces an existing target, like real lfs_rename
  g_files.erase(from);
  return 0;
}

int lfs_remove(lfs_t*, const char* path) {
  if (consume(1) < 1) return LFS_ERR_IO;
  return g_files.erase(path) ? 0 : LFS_ERR_NOENT;
}

int lfs_stat(lfs_t*, const char* path, struct lfs_info* info) {
  std::map<std::string, Bytes>::iterator it = g_files.find(path);
  if (it == g_files.end()) return LFS_ERR_NOENT;
  if (info) {
    memset(info, 0, sizeof(*info));
    info->size = (lfs_size_t)it->second.size();
  }
  return 0;
}

}  // extern "C"

// ---- binder: attaches the backend to the fake before main() ----

#ifdef FAKE_BIND_FATFS
static FATFS g_fatfs;
static struct Binder {
  Binder() { pge_fatfs_storage_bind(&g_fatfs, ""); }
} g_binder;
#endif

#ifdef FAKE_BIND_LFS
static lfs_t g_lfs;
static lfs_config g_cfg = {};
static struct Binder {
  Binder() {
    g_cfg.cache_size = 64;
    pge_lfs_storage_bind(&g_lfs, &g_cfg);
  }
} g_binder;
#endif
