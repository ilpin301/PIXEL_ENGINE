// HAL storage contract test (hal/include/pge/hal/storage.h). Runs unchanged against the host file backend and the
// FatFs / LittleFS backends over the fake RAM disk. Uses only pge_hal_storage_* plus the host budget/leak hooks.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "host_storage.h"
#include "pge/hal/storage.h"

#define CHECK(c)                                                          \
  do {                                                                    \
    if (!(c)) {                                                           \
      std::printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); \
      return 1;                                                           \
    }                                                                     \
  } while (0)

static const char* kA = "pge_hal_a.bin";
static const char* kB = "pge_hal_b.bin";
static const char* kC = "pge_hal_c.bin";
static const char* kD = "pge_hal_d.bin";
static const char* kE = "pge_hal_e.bin";

static void cleanup() {
  pge_hal_storage_remove(kA);
  pge_hal_storage_remove(kB);
  pge_hal_storage_remove(kC);
  pge_hal_storage_remove(kD);
  pge_hal_storage_remove(kE);
}

// Create `path` with n bytes of `fill`. Returns 0 on success.
static int put(const char* path, uint8_t fill, uint32_t n) {
  uint8_t b[256];
  std::memset(b, fill, sizeof(b));
  pge_file f = -1;
  if (pge_hal_storage_open(path, 1, &f) != 0) return 1;
  if (pge_hal_storage_write(f, b, n) != (int32_t)n) return 1;
  return pge_hal_storage_close(f);
}

// Read the whole file into buf (cap bytes). Returns length, or -1 if it cannot be opened / read errors.
static int32_t slurp(const char* path, uint8_t* buf, uint32_t cap) {
  pge_file f = -1;
  if (pge_hal_storage_open(path, 0, &f) != 0) return -1;
  int32_t total = 0;
  for (;;) {
    int32_t r = pge_hal_storage_read(f, buf + total, cap - (uint32_t)total);
    if (r < 0) { pge_hal_storage_close(f); return -1; }
    if (r == 0) break;
    total += r;
  }
  pge_hal_storage_close(f);
  return total;
}

static bool is(const char* path, uint8_t fill, uint32_t n) {
  uint8_t b[300];
  if (slurp(path, b, sizeof(b)) != (int32_t)n) return false;
  for (uint32_t i = 0; i < n; ++i) if (b[i] != fill) return false;
  return true;
}

static int caseRoundTrip() {
  uint8_t w[100], r[200];
  for (int i = 0; i < 100; ++i) w[i] = (uint8_t)(i * 7 + 3);
  pge_file f = -1;
  CHECK(pge_hal_storage_open(kA, 1, &f) == 0);
  CHECK(f >= 0);
  CHECK(pge_hal_storage_write(f, w, 100) == 100);
  CHECK(pge_hal_storage_close(f) == 0);
  CHECK(pge_hal_storage_open(kA, 0, &f) == 0);
  int32_t total = 0;
  while (total < 100) {
    int32_t n = pge_hal_storage_read(f, r + total, (uint32_t)(200 - total));
    CHECK(n > 0);
    total += n;
  }
  CHECK(total == 100);
  CHECK(std::memcmp(w, r, 100) == 0);
  CHECK(pge_hal_storage_read(f, r, 200) == 0);
  CHECK(pge_hal_storage_read(f, r, 200) == 0);
  CHECK(pge_hal_storage_close(f) == 0);
  CHECK(pge_hal_storage_remove(kA) == 0);
  return 0;
}

static int caseTruncate() {
  CHECK(put(kA, 0x11, 100) == 0);
  CHECK(is(kA, 0x11, 100));
  CHECK(put(kA, 0x22, 30) == 0);
  CHECK(is(kA, 0x22, 30));
  CHECK(pge_hal_storage_remove(kA) == 0);
  return 0;
}

static int caseOpenMissing() {
  pge_file f = 123;
  CHECK(pge_hal_storage_open(kA, 0, &f) == -4);
  CHECK(f == -1);
  return 0;
}

static int caseOpenInvalid() {
  pge_file f = 55;
  CHECK(pge_hal_storage_open(kA, 2, &f) == -1);
  CHECK(pge_hal_storage_open(nullptr, 1, &f) == -1);
  CHECK(pge_hal_storage_open(kA, 1, nullptr) == -1);
  CHECK(pge_hal_storage_open(nullptr, 0, &f) == -1);
  CHECK(pge_hal_storage_open(kA, 0, nullptr) == -1);
  CHECK(pge_host_storage_open_files() == 0);
  return 0;
}

static int caseRename() {
  CHECK(put(kA, 0xA1, 40) == 0);
  CHECK(put(kB, 0xB2, 50) == 0);
  CHECK(pge_hal_storage_rename(kA, kB) == -4);  // onto an existing target: refused, nothing replaced
  CHECK(is(kA, 0xA1, 40));
  CHECK(is(kB, 0xB2, 50));
  CHECK(pge_hal_storage_rename(kA, kC) == 0);   // free name
  pge_file f = 77;
  CHECK(pge_hal_storage_open(kA, 0, &f) == -4);
  CHECK(is(kC, 0xA1, 40));
  CHECK(is(kB, 0xB2, 50));
  CHECK(pge_hal_storage_rename(kA, kD) == -4);  // missing source
  CHECK(pge_hal_storage_open(kD, 0, &f) == -4);
  CHECK(pge_hal_storage_remove(kB) == 0);
  CHECK(pge_hal_storage_remove(kC) == 0);
  return 0;
}

static int caseRemove() {
  CHECK(pge_hal_storage_remove(kA) == -4);
  CHECK(put(kA, 0x5A, 10) == 0);
  CHECK(pge_hal_storage_remove(kA) == 0);
  pge_file f = -1;
  CHECK(pge_hal_storage_open(kA, 0, &f) == -4);
  CHECK(pge_hal_storage_remove(kA) == -4);
  return 0;
}

static int caseHandles() {
  const char* names[5] = {kA, kB, kC, kD, kE};
  pge_file h[5] = {-1, -1, -1, -1, -1};
  CHECK(pge_host_storage_open_files() == 0);
  for (int i = 0; i < 4; ++i) {
    CHECK(pge_hal_storage_open(names[i], 1, &h[i]) == 0);
    CHECK(h[i] >= 0);
  }
  CHECK(pge_host_storage_open_files() == 4);
  CHECK(pge_hal_storage_open(names[4], 1, &h[4]) == -4);  // 5th handle: table full
  CHECK(pge_host_storage_open_files() == 4);
  for (int i = 0; i < 4; ++i) CHECK(pge_hal_storage_close(h[i]) == 0);
  CHECK(pge_host_storage_open_files() == 0);
  CHECK(pge_hal_storage_close(h[0]) == -1);  // already closed
  uint8_t b[4] = {1, 2, 3, 4};
  CHECK(pge_hal_storage_read(99, b, 4) == -1);
  CHECK(pge_hal_storage_write(99, b, 4) == -1);
  CHECK(pge_hal_storage_close(99) == -1);
  CHECK(pge_hal_storage_read(-1, b, 4) == -1);
  CHECK(pge_hal_storage_write(-1, b, 4) == -1);
  CHECK(pge_hal_storage_close(-1) == -1);
  for (int i = 0; i < 4; ++i) CHECK(pge_hal_storage_remove(names[i]) == 0);
  return 0;
}

static int caseNullBuf() {
  pge_file f = -1;
  CHECK(pge_hal_storage_open(kA, 1, &f) == 0);
  CHECK(pge_hal_storage_write(f, nullptr, 5) == -1);
  CHECK(pge_hal_storage_write(f, nullptr, 0) == 0);
  CHECK(pge_hal_storage_close(f) == 0);
  CHECK(is(kA, 0, 0));
  CHECK(pge_hal_storage_remove(kA) == 0);
  return 0;
}

static int caseBudget() {
  uint8_t b[20], r[64];
  std::memset(b, 0x6B, sizeof(b));
  pge_file f = -1;
  pge_host_storage_set_budget(11);  // 1 for the open + 10 bytes
  CHECK(pge_hal_storage_open(kA, 1, &f) == 0);
  CHECK(pge_hal_storage_write(f, b, 20) == -4);
  CHECK(pge_hal_storage_close(f) == 0);
  pge_host_storage_set_budget(-1);
  CHECK(slurp(kA, r, sizeof(r)) == 10);
  for (int i = 0; i < 10; ++i) CHECK(r[i] == 0x6B);

  pge_host_storage_set_budget(0);
  f = -1;
  CHECK(pge_hal_storage_open(kA, 1, &f) == -4);
  pge_host_storage_set_budget(-1);
  CHECK(pge_host_storage_open_files() == 0);
  CHECK(is(kA, 0x6B, 10));  // failed truncating open left the file alone
  CHECK(pge_hal_storage_remove(kA) == 0);
  return 0;
}

int main() {
  CHECK(pge_hal_storage_mount() == 0);
  pge_host_storage_set_budget(-1);
  cleanup();
  int rc = 0;
  if (!rc) rc = caseRoundTrip();
  if (!rc) rc = caseTruncate();
  if (!rc) rc = caseOpenMissing();
  if (!rc) rc = caseOpenInvalid();
  if (!rc) rc = caseRename();
  if (!rc) rc = caseRemove();
  if (!rc) rc = caseHandles();
  if (!rc) rc = caseNullBuf();
  if (!rc) rc = caseBudget();
  pge_host_storage_set_budget(-1);
  cleanup();
  if (!rc) std::printf("hal_storage: all passed\n");
  return rc;
}
