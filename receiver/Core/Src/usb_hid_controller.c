#include "usb_hid_controller.h"
#include "usb_device.h"
#include "usbd_hid.h"
#include <stddef.h>
extern USBD_HandleTypeDef hUsbDeviceFS;

_Static_assert(sizeof(MouseReport) == HID_MOUSE_REPORT_SIZE, "Mouse report layout");
_Static_assert(sizeof(KeyboardReport) == HID_KEYBOARD_REPORT_SIZE, "Keyboard report layout");

/* Migrated from receiver_old; report IDs/layouts are unchanged. The HID class
 * owns persistent transfer storage, so these local report objects are safe.
 */
bool Send_Mouse_Action(uint8_t buttons, int8_t x, int8_t y, int8_t wheel) {
    MouseReport report = {HID_MOUSE_REPORT_ID, buttons & 0x1FU, x, y, wheel};
    return USBD_HID_SendReport(&hUsbDeviceFS, (const uint8_t*)&report, sizeof(report)) == USBD_OK;
}

bool Send_Keyboard_Action(uint8_t modifiers, const uint8_t keys[6]) {
    if (keys == NULL) return false;
    KeyboardReport report = {HID_KEYBOARD_REPORT_ID, modifiers, 0, {0}};
    for(int i = 0; i < 6; i++) {
        report.keys[i] = keys[i];
    }

    return USBD_HID_SendReport(&hUsbDeviceFS, (const uint8_t*)&report, sizeof(report)) == USBD_OK;
}

bool UsbHid_IsConfigured(void) {
    return hUsbDeviceFS.dev_state == USBD_STATE_CONFIGURED && hUsbDeviceFS.pClassData != NULL;
}
