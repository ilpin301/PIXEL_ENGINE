// pge_hal_storage_* on LittleFS. The block device (lfs_config read/prog/erase/sync and the static
// read/prog/lookahead buffers) is supplied by the platform, not here. Power-loss behaviour is not verified on
// hardware. K5: no heap, so every file goes through lfs_file_opencfg with a static per-slot cache buffer.
#include "pge_storage_littlefs.h"
#include "pge/hal/storage.h"

enum { kSlots = 4, kInvalidArg = -1, kIo = -4 };

static lfs_file_t g_file[kSlots];
static lfs_file_config g_fcfg[kSlots];
static uint8_t g_cache[kSlots][PGE_LFS_CACHE_MAX];
static bool g_used[kSlots];
static lfs_t* g_fs;
static const struct lfs_config* g_cfg;
static bool g_mounted;

static bool valid(pge_file f) { return f >= 0 && f < kSlots && g_used[f]; }

extern "C" {

int pge_lfs_storage_bind(lfs_t* fs, const struct lfs_config* cfg) {
  if (!fs || !cfg || cfg->cache_size > PGE_LFS_CACHE_MAX) return kInvalidArg;
  g_fs = fs;
  g_cfg = cfg;
  return 0;
}

// Never lfs_format here: a failed mount must not wipe the saves. Formatting is the platform's decision.
int pge_hal_storage_mount(void) {
  if (!g_fs) return kIo;
  if (g_mounted) return 0;
  if (lfs_mount(g_fs, g_cfg) != 0) return kIo;
  g_mounted = true;
  return 0;
}

int pge_hal_storage_open(const char* path, int mode, pge_file* out) {
  if (!path || !out || (mode != 0 && mode != 1)) return kInvalidArg;
  *out = -1;
  int i = 0;
  while (i < kSlots && g_used[i]) i++;
  if (i == kSlots || !g_mounted) return kIo;
  g_fcfg[i].buffer = g_cache[i];
  g_fcfg[i].attrs = nullptr;
  g_fcfg[i].attr_count = 0;
  int flags = mode ? (LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC) : LFS_O_RDONLY;
  if (lfs_file_opencfg(g_fs, &g_file[i], path, flags, &g_fcfg[i]) != 0) return kIo;
  g_used[i] = true;
  *out = i;
  return 0;
}

int32_t pge_hal_storage_read(pge_file f, void* buf, uint32_t n) {
  if (!valid(f) || (!buf && n)) return kInvalidArg;
  lfs_ssize_t got = lfs_file_read(g_fs, &g_file[f], buf, (lfs_size_t)n);
  return got < 0 ? (int32_t)kIo : (int32_t)got;
}

int32_t pge_hal_storage_write(pge_file f, const void* buf, uint32_t n) {
  if (!valid(f) || (!buf && n)) return kInvalidArg;
  lfs_ssize_t put = lfs_file_write(g_fs, &g_file[f], buf, (lfs_size_t)n);
  if (put < 0 || (uint32_t)put != n) return kIo;
  return (int32_t)put;
}

int pge_hal_storage_close(pge_file f) {
  if (!valid(f)) return kInvalidArg;
  int r = lfs_file_close(g_fs, &g_file[f]);
  g_used[f] = false;
  return r == 0 ? 0 : kIo;
}

// lfs_rename replaces an existing target, the HAL says `to` must not exist: probe first.
int pge_hal_storage_rename(const char* from, const char* to) {
  if (!from || !to) return kInvalidArg;
  if (!g_mounted) return kIo;
  struct lfs_info info;
  if (lfs_stat(g_fs, to, &info) != LFS_ERR_NOENT) return kIo;
  return lfs_rename(g_fs, from, to) == 0 ? 0 : kIo;
}

int pge_hal_storage_remove(const char* path) {
  if (!path) return kInvalidArg;
  if (!g_mounted) return kIo;
  return lfs_remove(g_fs, path) == 0 ? 0 : kIo;
}

}  // extern "C"
