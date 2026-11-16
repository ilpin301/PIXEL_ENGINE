#pragma once
#include "ff.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Binds the storage backend to a caller-owned FATFS work area and logical drive ("" or "0:").
 * Call before pge_hal_storage_mount(). Returns 0, or -1 if fs is null. */
int pge_fatfs_storage_bind(FATFS* fs, const char* drive);

#ifdef __cplusplus
}
#endif
