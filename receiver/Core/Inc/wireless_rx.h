#ifndef WIRELESS_RX_H
#define WIRELESS_RX_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
  bool usb_ready;
  bool linked;
  bool radio_error;
  uint16_t user_id;
  uint32_t accepted;
  uint32_t duplicates;
  uint32_t hellos;
  uint32_t rejected;
  uint32_t link_timeouts;
  uint32_t usb_disconnects;
} WirelessRxStatus;

/* Called by the receiver main loop. No waits for USB or radio. */
void WirelessRx_Service(void);
WirelessRxStatus WirelessRx_GetStatus(void);

#endif
