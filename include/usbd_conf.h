#ifndef OPENDAC_USBD_CONF_H
#define OPENDAC_USBD_CONF_H
#include "stm32f4xx_hal.h"
#include <string.h>
#define USBD_MAX_NUM_INTERFACES 3U
#define USBD_MAX_SUPPORTED_CLASS 1U
#define USBD_MAX_NUM_CONFIGURATION 1U
#define USBD_MAX_STR_DESC_SIZ 128U
#define USBD_SELF_POWERED 0U
#define USBD_SUPPORT_USER_STRING 0U
#define USBD_DEBUG_LEVEL 0U
#define USBD_memset memset
#define USBD_memcpy memcpy
#define USBD_UsrLog(...) ((void)0)
#define USBD_ErrLog(...) ((void)0)
#define USBD_DbgLog(...) ((void)0)
#endif
