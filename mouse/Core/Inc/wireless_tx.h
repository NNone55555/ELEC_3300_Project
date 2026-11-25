#ifndef WIRELESS_TX_H
#define WIRELESS_TX_H

#include <stdbool.h>
#include <stdint.h>

/* Called by the input service task with interrupts enabled. */
void WirelessTx_Service(bool enabled);
bool WirelessTx_IsReady(void);
uint32_t WirelessTx_GetSession(void);
uint8_t WirelessTx_GetKeyboardLeds(void);
void WirelessTx_SetUserId(uint16_t user_id);

/* One in-flight input report. True means this report was copied for delivery. */
bool WirelessTx_SendMouse(uint8_t buttons, int16_t x, int16_t y, int8_t wheel);
bool WirelessTx_SendKeyboard(uint8_t modifiers, const uint8_t keys[6]);

#endif
