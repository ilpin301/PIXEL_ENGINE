#pragma once
// Test fake of the LittleFS API subset used by drivers/storage_littlefs. RAM disk lives in fake_fs.cpp.
// Deliberately no lfs_file_open (heap variant) and no lfs_format.
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t lfs_size_t;
typedef int32_t lfs_ssize_t;
typedef uint32_t lfs_off_t;
typedef uint32_t lfs_block_t;

enum lfs_error {
  LFS_ERR_OK = 0,
  LFS_ERR_IO = -5,
  LFS_ERR_NOENT = -2,
  LFS_ERR_EXIST = -17,
  LFS_ERR_INVAL = -22
};

enum lfs_open_flags {
  LFS_O_RDONLY = 1,
  LFS_O_WRONLY = 2,
  LFS_O_CREAT = 0x0100,
  LFS_O_TRUNC = 0x0400
};

struct lfs_config {
  void* context;
  int (*read)(const struct lfs_config* c, lfs_block_t block, lfs_off_t off, void* buffer, lfs_size_t size);
  int (*prog)(const struct lfs_config* c, lfs_block_t block, lfs_off_t off, const void* buffer, lfs_size_t size);
  int (*erase)(const struct lfs_config* c, lfs_block_t block);
  int (*sync)(const struct lfs_config* c);
  lfs_size_t read_size;
  lfs_size_t prog_size;
  lfs_size_t block_size;
  lfs_size_t block_count;
  int32_t block_cycles;
  lfs_size_t cache_size;
  lfs_size_t lookahead_size;
  void* read_buffer;
  void* prog_buffer;
  void* lookahead_buffer;
};

typedef struct lfs { int mounted; } lfs_t;
typedef struct lfs_file { int h; } lfs_file_t;

struct lfs_attr;
struct lfs_file_config {
  void* buffer;
  struct lfs_attr* attrs;
  uint32_t attr_count;
};

struct lfs_info {
  uint8_t type;
  lfs_size_t size;
  char name[256];
};

int lfs_mount(lfs_t* lfs, const struct lfs_config* cfg);
int lfs_file_opencfg(lfs_t* lfs, lfs_file_t* file, const char* path, int flags, const struct lfs_file_config* cfg);
lfs_ssize_t lfs_file_read(lfs_t* lfs, lfs_file_t* file, void* buffer, lfs_size_t size);
lfs_ssize_t lfs_file_write(lfs_t* lfs, lfs_file_t* file, const void* buffer, lfs_size_t size);
int lfs_file_close(lfs_t* lfs, lfs_file_t* file);
int lfs_rename(lfs_t* lfs, const char* oldpath, const char* newpath);
int lfs_remove(lfs_t* lfs, const char* path);
int lfs_stat(lfs_t* lfs, const char* path, struct lfs_info* info);

#ifdef __cplusplus
}
#endif
