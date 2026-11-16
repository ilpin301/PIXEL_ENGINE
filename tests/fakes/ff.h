#pragma once
// Test fake of the FatFs R0.12c API subset used by drivers/storage_fatfs. RAM disk lives in fake_fs.cpp.
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t BYTE;
typedef unsigned int UINT;
typedef char TCHAR;

typedef struct { int h; } FIL;
typedef struct { int mounted; } FATFS;

typedef enum {
  FR_OK = 0,
  FR_DISK_ERR,
  FR_INT_ERR,
  FR_NOT_READY,
  FR_NO_FILE,
  FR_NO_PATH,
  FR_INVALID_NAME,
  FR_DENIED,
  FR_EXIST,
  FR_INVALID_OBJECT,
  FR_WRITE_PROTECTED,
  FR_INVALID_DRIVE,
  FR_NOT_ENABLED,
  FR_NO_FILESYSTEM,
  FR_MKFS_ABORTED,
  FR_TIMEOUT,
  FR_LOCKED,
  FR_NOT_ENOUGH_CORE,
  FR_TOO_MANY_OPEN_FILES,
  FR_INVALID_PARAMETER
} FRESULT;

#define FA_READ 0x01
#define FA_WRITE 0x02
#define FA_CREATE_ALWAYS 0x08

FRESULT f_open(FIL* fp, const TCHAR* path, BYTE mode);
FRESULT f_close(FIL* fp);
FRESULT f_read(FIL* fp, void* buff, UINT btr, UINT* br);
FRESULT f_write(FIL* fp, const void* buff, UINT btw, UINT* bw);
FRESULT f_rename(const TCHAR* path_old, const TCHAR* path_new);
FRESULT f_unlink(const TCHAR* path);
FRESULT f_mount(FATFS* fs, const TCHAR* path, BYTE opt);

#ifdef __cplusplus
}
#endif
