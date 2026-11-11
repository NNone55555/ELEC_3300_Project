#ifndef MOUSE_BUTTONS_H
#define MOUSE_BUTTONS_H

#include <stdint.h>

/* Bit positions match the five HID mouse buttons. */
enum {
  MOUSE_BUTTON_LEFT = 1U << 0,
  MOUSE_BUTTON_RIGHT = 1U << 1,
  MOUSE_BUTTON_MIDDLE = 1U << 2,
  MOUSE_BUTTON_SIDE1 = 1U << 3,
  MOUSE_BUTTON_SIDE2 = 1U << 4
};

typedef struct {
  uint8_t pressed;    /* Debounced: 1 means pressed (switch to ground). */
  uint8_t pin_levels; /* Raw: 1 means GPIO high, in the same bit order. */
} MouseButtonsSnapshot;

void MouseButtons_Init(void);
void MouseButtons_Sample1ms(void);
MouseButtonsSnapshot MouseButtons_GetSnapshot(void);

#endif
