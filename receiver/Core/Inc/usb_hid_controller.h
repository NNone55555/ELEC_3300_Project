#ifndef USB_HID_CONTROLLER_H
#define USB_HID_CONTROLLER_H

#include <stdint.h>

#pragma pack(1)

typedef struct {
    uint8_t report_id;
    uint8_t buttons;
    int8_t x;
    int8_t y;
    int8_t wheel;
} MouseReport;

typedef struct {
    uint8_t report_id;
    uint8_t modifiers;
    uint8_t reserved;
    uint8_t keys[6];
} KeyboardReport;

#pragma pack()

void Send_Mouse_Action(uint8_t buttons, int8_t x, int8_t y, int8_t wheel);
void Send_Keyboard_Action(uint8_t modifiers, uint8_t* keys);

#endif
