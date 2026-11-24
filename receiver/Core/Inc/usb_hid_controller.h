#ifndef USB_HID_CONTROLLER_H
#define USB_HID_CONTROLLER_H

#include <stdint.h>
#include <stdbool.h>

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

/* True: copied and accepted. False: USB is unavailable, busy, or failed;
 * caller retains the action for retry. Keyboard presses require a zero-key
 * release. Do not call again with a new action until the previous one succeeds.
 */
bool Send_Mouse_Action(uint8_t buttons, int8_t x, int8_t y, int8_t wheel);
bool Send_Keyboard_Action(uint8_t modifiers, const uint8_t keys[6]);
bool UsbHid_IsConfigured(void);

#endif
