/* FatFs R0.12c config for the F429 USB stick (own file; macro names are FatFs's). LFN on: storage_fatfs needs names like x.sav.tmp. */
#define _FFCONF 68300
#define _FS_READONLY 0
#define _FS_MINIMIZE 0
#define _USE_STRFUNC 0
#define _USE_FIND 0
#define _USE_MKFS 0
#define _USE_FASTSEEK 0
#define _USE_EXPAND 0
#define _USE_CHMOD 0
#define _USE_LABEL 0
#define _USE_FORWARD 0
#define _CODE_PAGE 437
#define _USE_LFN 1   /* static buffer, no heap; not reentrant */
#define _MAX_LFN 255
#define _LFN_UNICODE 0
#define _STRF_ENCODE 3
#define _FS_RPATH 0
#define _VOLUMES 1
#define _STR_VOLUME_ID 0
#define _VOLUME_STRS "USB"
#define _MULTI_PARTITION 0
#define _MIN_SS 512
#define _MAX_SS 512
#define _USE_TRIM 0
#define _FS_NOFSINFO 0
#define _FS_TINY 0
#define _FS_EXFAT 0
#define _FS_NORTC 1   /* no RTC on the board, files get this fixed date */
#define _NORTC_MON 1
#define _NORTC_MDAY 1
#define _NORTC_YEAR 2026
#define _FS_LOCK 0
#define _FS_REENTRANT 0
#define _FS_TIMEOUT 1000
#define _SYNC_t void*
