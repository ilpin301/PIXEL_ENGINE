// pge_hal_storage_* on FatFs (ChaN). Required ffconf (FatFs R0.12c in Cube): _USE_LFN >= 1 with a static buffer
// (no heap), because names like "x.sav.tmp" are not 8.3; _FS_READONLY 0; _FS_MINIMIZE 0.
// 4 FIL objects cost about 4 * (sector buffer + ~50 B) RAM. A diskio driver (SD / USB MSC) is NOT part of this
// file. Real power-loss behaviour is not verified.
#include "pge_storage_fatfs.h"
#include "pge/hal/storage.h"

enum { kSlots = 4, kInvalidArg = -1, kIo = -4 };

static FIL g_fil[kSlots];
static bool g_used[kSlots];
static FATFS* g_fs;
static const char* g_drive;
static bool g_mounted;

static bool valid(pge_file f) { return f >= 0 && f < kSlots && g_used[f]; }

extern "C" {

int pge_fatfs_storage_bind(FATFS* fs, const char* drive) {
  if (!fs) return kInvalidArg;
  g_fs = fs;
  g_drive = drive ? drive : "";
  return 0;
}

int pge_hal_storage_mount(void) {
  if (!g_fs) return kIo;
  if (g_mounted) return 0;
  if (f_mount(g_fs, g_drive, 1) != FR_OK) return kIo;
  g_mounted = true;
  return 0;
}

int pge_hal_storage_open(const char* path, int mode, pge_file* out) {
  if (!path || !out || (mode != 0 && mode != 1)) return kInvalidArg;
  *out = -1;
  int i = 0;
  while (i < kSlots && g_used[i]) i++;
  if (i == kSlots || !g_mounted) return kIo;
  BYTE flags = mode ? (BYTE)(FA_WRITE | FA_CREATE_ALWAYS) : (BYTE)FA_READ;
  if (f_open(&g_fil[i], path, flags) != FR_OK) return kIo;
  g_used[i] = true;
  *out = i;
  return 0;
}

int32_t pge_hal_storage_read(pge_file f, void* buf, uint32_t n) {
  if (!valid(f) || (!buf && n)) return kInvalidArg;
  UINT got = 0;
  if (f_read(&g_fil[f], buf, (UINT)n, &got) != FR_OK) return kIo;
  return (int32_t)got;
}

int32_t pge_hal_storage_write(pge_file f, const void* buf, uint32_t n) {
  if (!valid(f) || (!buf && n)) return kInvalidArg;
  UINT put = 0;
  if (f_write(&g_fil[f], buf, (UINT)n, &put) != FR_OK || put != n) return kIo;
  return (int32_t)put;
}

int pge_hal_storage_close(pge_file f) {
  if (!valid(f)) return kInvalidArg;
  FRESULT r = f_close(&g_fil[f]);
  g_used[f] = false;
  return r == FR_OK ? 0 : kIo;
}

// FatFs fails with FR_EXIST when `to` exists, which is the HAL contract: no pre-check.
int pge_hal_storage_rename(const char* from, const char* to) {
  if (!from || !to) return kInvalidArg;
  return f_rename(from, to) == FR_OK ? 0 : kIo;
}

int pge_hal_storage_remove(const char* path) {
  if (!path) return kInvalidArg;
  return f_unlink(path) == FR_OK ? 0 : kIo;
}

}  // extern "C"
