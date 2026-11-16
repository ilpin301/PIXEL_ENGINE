#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Power-loss simulation. units < 0 = unlimited (default). Otherwise each unit is one byte written,
 * or one open-for-write / rename / remove call. When the budget is spent the operation fails with Io;
 * a write that is only partly covered writes the covered bytes first, then returns Io.
 * close and read are never limited. */
void pge_host_storage_set_budget(int32_t units);

/* Number of currently open handles (leak check). */
int32_t pge_host_storage_open_files(void);

#ifdef __cplusplus
}
#endif
