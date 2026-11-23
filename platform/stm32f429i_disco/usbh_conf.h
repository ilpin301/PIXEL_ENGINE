/* USB Host library config for PGE on the F429 (own file; macro names are the ST USB Host library's) */
#ifndef __USBH_CONF_H
#define __USBH_CONF_H

#include "stm32f4xx.h"
#include <stddef.h>
#include <string.h>

#define USBH_MAX_NUM_ENDPOINTS        2
#define USBH_MAX_NUM_INTERFACES       2
#define USBH_MAX_NUM_CONFIGURATION    1
#define USBH_MAX_NUM_SUPPORTED_CLASS  1
#define USBH_KEEP_CFG_DESCRIPTOR      0
#define USBH_MAX_SIZE_CONFIGURATION   0x200
#define USBH_MAX_DATA_BUFFER          0x200
#define USBH_DEBUG_LEVEL              0
#define USBH_USE_OS                   0

/* no heap after init (K5): the MSC class handle comes from one static block in usb_msc.c */
void* pge_usbh_malloc(size_t n);
#define USBH_malloc    pge_usbh_malloc
#define USBH_free(p)   ((void)(p))
#define USBH_memset    memset
#define USBH_memcpy    memcpy

#define USBH_UsrLog(...)
#define USBH_ErrLog(...)
#define USBH_DbgLog(...)

#endif
