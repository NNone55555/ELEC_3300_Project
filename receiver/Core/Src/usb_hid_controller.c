#include "usb_hid_controller.h"
#include "usb_device.h"
#include "usbd_hid.h"
extern USBD_HandleTypeDef hUsbDeviceFS;

void Send_Mouse_Action(uint8_t buttons, int8_t x, int8_t y, int8_t wheel) {
    MouseReport report = {1, buttons, x, y, wheel};
    USBD_HID_SendReport(&hUsbDeviceFS, (uint8_t*)&report, sizeof(report));
}

void Send_Keyboard_Action(uint8_t modifiers, uint8_t* keys) {
    KeyboardReport report = {2, modifiers, 0, {0}};
    for(int i = 0; i < 6; i++) {
        report.keys[i] = keys[i];
    }

    USBD_HID_SendReport(&hUsbDeviceFS, (uint8_t*)&report, sizeof(report));
}
