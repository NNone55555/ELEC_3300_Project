#ifndef WIRELESS_PACKET_H
#define WIRELESS_PACKET_H

#include <stdbool.h>
#include <stdint.h>

#define WIRELESS_PACKET_SIZE 12U
#define WIRELESS_NO_USER UINT16_MAX
#define WIRELESS_LINK_TIMEOUT_MS 180U
#define WIRELESS_ACK_READY 0x01U
#define WIRELESS_ACK_SYNCED 0x02U

typedef enum {
  WIRELESS_SYNC = 0xD1,
  WIRELESS_MOUSE = 0xD2,
  WIRELESS_KEYBOARD = 0xD3,
  WIRELESS_ACK = 0xD4
} WirelessKind;

/* The wire layout is explicit in wireless_packet.c. Never send this struct. */
typedef struct {
  WirelessKind kind;
  uint8_t sequence;
  uint16_t epoch;
  union {
    struct {
      int16_t x, y;
      int8_t wheel;
      uint8_t buttons;
      uint16_t user_id;
    } mouse;
    struct {
      uint8_t modifiers;
      uint8_t keys[6];
    } keyboard;
    struct {
      uint8_t flags;
      uint8_t leds;
      WirelessKind accepted_kind;
    } ack;
  } data;
} WirelessPacket;

void WirelessPacket_Encode(const WirelessPacket *packet, uint8_t bytes[WIRELESS_PACKET_SIZE]);
bool WirelessPacket_Decode(WirelessPacket *packet, const uint8_t bytes[WIRELESS_PACKET_SIZE]);

#endif
