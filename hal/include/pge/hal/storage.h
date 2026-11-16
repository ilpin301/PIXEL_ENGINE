#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Minimal file storage. Paths are platform-relative (host: relative to the current directory).
 * Files are binary. All int-returning functions use pge_error: 0 = ok, -1 = InvalidArg, -4 = Io.
 *
 *  open    mode 0 = read, 1 = write-truncate (creates the file). Writes the handle to *out.
 *  read    returns bytes read (0 = end of file), < 0 = error.
 *  write   returns bytes written, < 0 = error.
 *  rename  `to` must NOT exist, else Io. Not an atomic replace.
 *  remove  Io if the file does not exist or cannot be removed. */
typedef int32_t pge_file;  /* < 0 = invalid */

int pge_hal_storage_mount(void);
int pge_hal_storage_open(const char* path, int mode, pge_file* out);
int32_t pge_hal_storage_read(pge_file f, void* buf, uint32_t n);
int32_t pge_hal_storage_write(pge_file f, const void* buf, uint32_t n);
int pge_hal_storage_close(pge_file f);
int pge_hal_storage_rename(const char* from, const char* to);
int pge_hal_storage_remove(const char* path);

#ifdef __cplusplus
}
#endif
