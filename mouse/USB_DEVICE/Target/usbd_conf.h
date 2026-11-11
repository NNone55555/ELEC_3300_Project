#ifndef ELEC3300_USBD_CONF_H
#define ELEC3300_USBD_CONF_H

#include "stm32f1xx_hal.h"
#include <string.h>

#define USBD_MAX_NUM_INTERFACES 1U
#define USBD_MAX_NUM_CONFIGURATION 1U
#define USBD_MAX_STR_DESC_SIZ 128U
#define USBD_SUPPORT_USER_STRING_DESC 0U
#define USBD_SELF_POWERED 1U
#define USBD_DEBUG_LEVEL 0U
#define USBD_LPM_ENABLED 0U
#define USBD_memset memset
#define USBD_memcpy memcpy
#define USBD_UsrLog(...)
#define USBD_ErrLog(...)
#define USBD_DbgLog(...)

/* The one HID class uses static storage; no allocation in USB interrupts. */
#endif
