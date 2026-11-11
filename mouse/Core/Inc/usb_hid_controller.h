#ifndef USB_HID_CONTROLLER_H
#define USB_HID_CONTROLLER_H

#include <stdbool.h>
#include <stdint.h>

/* Same report IDs and layouts as the receiver's wrapper: 1 mouse, 2 keyboard. */
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

/* True means accepted. False means disconnected/busy/error; caller retries.
 * Keyboard callers must send a zero-key release report after a key press.
 */
bool Send_Mouse_Action(uint8_t buttons, int8_t x, int8_t y, int8_t wheel);
bool Send_Keyboard_Action(uint8_t modifiers, const uint8_t keys[6]);
bool UsbHid_IsConfigured(void);

/* Motion task adds deltas; one USB task services the mouse every millisecond.
 * Counts larger than the signed 8-bit report are drained over several reports.
 * Side actions use compact profile macro IDs; keyboard chords always release.
 */
void UsbHid_AddMotion(int16_t x, int16_t y, int32_t wheel);
void UsbHid_ServiceMouse(uint8_t buttons);
uint32_t UsbHid_GetButtonOverflows(void);
void UsbHid_SetSideMacros(uint8_t side1, uint8_t side2);
void UsbHid_GetSideMacros(uint8_t *side1, uint8_t *side2);

#endif
