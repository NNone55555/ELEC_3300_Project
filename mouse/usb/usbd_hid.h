#ifndef ELEC3300_USBD_HID_H
#define ELEC3300_USBD_HID_H

#include "usbd_ioreq.h"

#define HID_EPIN_ADDR 0x81U
#define HID_EPIN_SIZE 16U
#define HID_FS_BINTERVAL 1U
#define HID_MOUSE_REPORT_ID 1U
#define HID_KEYBOARD_REPORT_ID 2U
#define HID_MOUSE_REPORT_SIZE 5U
#define HID_KEYBOARD_REPORT_SIZE 9U

extern USBD_ClassTypeDef USBD_HID;

/* Copies the report; returns BUSY rather than silently discarding input. */
uint8_t USBD_HID_SendReport(USBD_HandleTypeDef *device, const uint8_t *report,
                            uint16_t size);
uint32_t USBD_HID_GetSession(void);
uint8_t USBD_HID_GetKeyboardLeds(void);

#endif
