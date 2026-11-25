#ifndef ELEC3300_USBD_HID_H
#define ELEC3300_USBD_HID_H

#include "usbd_ioreq.h"

#define HID_EPIN_ADDR 0x81U
#define HID_EPIN_SIZE 16U
#ifdef HID_FS_BINTERVAL
#undef HID_FS_BINTERVAL /* Override CubeMX's stock 10 ms mouse setting. */
#endif
#define HID_FS_BINTERVAL 1U
#define HID_MOUSE_REPORT_ID 1U
#define HID_KEYBOARD_REPORT_ID 2U
#define HID_MOUSE_REPORT_SIZE 5U
#define HID_KEYBOARD_REPORT_SIZE 9U

/* Public handle keeps CubeMX usbd_conf.c static allocation compatible. */
typedef struct {
  volatile uint8_t busy;
  uint8_t tx[HID_EPIN_SIZE];
  uint8_t mouse[HID_MOUSE_REPORT_SIZE];
  uint8_t keyboard[HID_KEYBOARD_REPORT_SIZE];
  uint8_t idle[3];
  uint16_t elapsed[3];
  uint8_t led_output[2];
  uint8_t led_pending;
  uint8_t alternate;
  uint16_t status;
} USBD_HID_HandleTypeDef;

extern USBD_ClassTypeDef USBD_HID;

/* Copies the report; returns BUSY rather than silently discarding input. */
uint8_t USBD_HID_SendReport(USBD_HandleTypeDef *device, const uint8_t *report,
                            uint16_t size);
uint32_t USBD_HID_GetSession(void);
void USBD_HID_OnSuspend(void);
uint8_t USBD_HID_GetKeyboardLeds(void);

#endif
