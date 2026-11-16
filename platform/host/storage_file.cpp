// Host storage backend on <stdio.h>. Binary mode is mandatory on Windows.
// Budget hook simulates power loss; a partly written file stays on disk after close.
#include <stdio.h>
#include "host_storage.h"
#include "pge/hal/storage.h"

enum { kSlots = 4, kInvalidArg = -1, kIo = -4 };

static FILE* g_files[kSlots];
static int64_t g_budget = -1;

// Returns how many of n units are allowed and charges them to the budget.
static uint32_t consume(uint32_t n) {
  if (g_budget < 0) return n;
  uint32_t ok = (uint64_t)g_budget < n ? (uint32_t)g_budget : n;
  g_budget -= ok;
  return ok;
}

static FILE* slot(pge_file f) {
  return (f >= 0 && f < kSlots) ? g_files[f] : nullptr;
}

extern "C" {

void pge_host_storage_set_budget(int32_t units) { g_budget = units; }

int32_t pge_host_storage_open_files(void) {
  int32_t n = 0;
  for (int i = 0; i < kSlots; i++) n += g_files[i] != nullptr;
  return n;
}

int pge_hal_storage_mount(void) { return 0; }

int pge_hal_storage_open(const char* path, int mode, pge_file* out) {
  if (!path || !out || (mode != 0 && mode != 1)) return kInvalidArg;
  *out = -1;
  int i = 0;
  while (i < kSlots && g_files[i]) i++;
  if (i == kSlots) return kIo;
  if (mode == 1 && consume(1) < 1) return kIo;
  FILE* fp = fopen(path, mode ? "wb" : "rb");
  if (!fp) return kIo;
  g_files[i] = fp;
  *out = i;
  return 0;
}

int32_t pge_hal_storage_read(pge_file f, void* buf, uint32_t n) {
  FILE* fp = slot(f);
  if (!fp || (!buf && n)) return kInvalidArg;
  size_t got = fread(buf, 1, n, fp);
  if (ferror(fp)) return kIo;
  return (int32_t)got;
}

int32_t pge_hal_storage_write(pge_file f, const void* buf, uint32_t n) {
  FILE* fp = slot(f);
  if (!fp || (!buf && n)) return kInvalidArg;
  uint32_t allowed = consume(n);
  size_t put = fwrite(buf, 1, allowed, fp);
  if (ferror(fp) || put != allowed) return kIo;
  return allowed < n ? kIo : (int32_t)put;
}

int pge_hal_storage_close(pge_file f) {
  FILE* fp = slot(f);
  if (!fp) return kInvalidArg;
  g_files[f] = nullptr;
  return fclose(fp) == 0 ? 0 : kIo;
}

int pge_hal_storage_rename(const char* from, const char* to) {
  if (!from || !to) return kInvalidArg;
  if (consume(1) < 1) return kIo;
  FILE* probe = fopen(to, "rb");
  if (probe) {
    fclose(probe);
    return kIo;
  }
  return rename(from, to) == 0 ? 0 : kIo;
}

int pge_hal_storage_remove(const char* path) {
  if (!path) return kInvalidArg;
  if (consume(1) < 1) return kIo;
  return remove(path) == 0 ? 0 : kIo;
}

}  // extern "C"
