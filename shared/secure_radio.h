#ifndef SECURE_RADIO_H
#define SECURE_RADIO_H

#include "wireless_packet.h"

#define SECURE_RADIO_BYTES 32U

typedef enum { RADIO_INPUT, RADIO_HELLO, RADIO_FEEDBACK, RADIO_CHALLENGE } RadioMessage;

typedef struct {
  uint64_t mouse_boot, receiver_boot;
  uint32_t hello, challenge;
} RadioBinding;

typedef struct {
  uint64_t boot;
  uint32_t counter;
} RadioReplay;

/* Reserves a number within this boot for a handshake ticket or nonce. */
uint32_t SecureRadio_NextCounter(void);

/* Untrusted header hints only. Always authenticate with Open before use. */
RadioMessage SecureRadio_Message(const uint8_t wire[SECURE_RADIO_BYTES]);
uint64_t SecureRadio_PeerBoot(const uint8_t wire[SECURE_RADIO_BYTES]);

bool SecureRadio_Seal(RadioMessage message, const RadioBinding *binding,
                      const uint8_t body[WIRELESS_PACKET_SIZE], uint8_t wire[SECURE_RADIO_BYTES]);
bool SecureRadio_Open(RadioMessage message, const RadioBinding *binding, RadioReplay *replay,
                      const uint8_t wire[SECURE_RADIO_BYTES], uint8_t body[WIRELESS_PACKET_SIZE]);

void SecureRadio_WriteTicket(uint8_t body[WIRELESS_PACKET_SIZE], uint32_t ticket);
uint32_t SecureRadio_ReadTicket(const uint8_t body[WIRELESS_PACKET_SIZE]);

#endif
