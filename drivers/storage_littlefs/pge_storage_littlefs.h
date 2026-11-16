#pragma once
#include "lfs.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Largest lfs_config.cache_size the backend supports (one static buffer per open-file slot). */
#ifndef PGE_LFS_CACHE_MAX
#define PGE_LFS_CACHE_MAX 256
#endif

/* Binds the backend to a caller-owned lfs_t and a fully filled lfs_config (must outlive the backend).
 * Call before pge_hal_storage_mount(). Returns 0, or -1 if a pointer is null or cfg->cache_size is too big. */
int pge_lfs_storage_bind(lfs_t* fs, const struct lfs_config* cfg);

#ifdef __cplusplus
}
#endif
