#include "wireless_packet.h"
#include <string.h>

static void Write16(uint8_t *out, uint16_t value)
{
  out[0] = (uint8_t)value;
  out[1] = (uint8_t)(value >> 8);
}

static uint16_t Read16(const uint8_t *in)
{
  return (uint16_t)(in[0] | ((uint16_t)in[1] << 8));
}

static int16_t ReadSigned16(const uint8_t *in)
{
  uint16_t value = Read16(in);
  return value <= INT16_MAX ? (int16_t)value : (int16_t)((int32_t)value - 65536);
}

void WirelessPacket_Encode(const WirelessPacket *packet, uint8_t bytes[WIRELESS_PACKET_SIZE])
{
  memset(bytes, 0, WIRELESS_PACKET_SIZE);
  bytes[0] = (uint8_t)packet->kind;
  bytes[1] = packet->sequence;
  Write16(&bytes[2], packet->epoch);
  switch (packet->kind)
  {
    case WIRELESS_MOUSE:
      Write16(&bytes[4], (uint16_t)packet->data.mouse.x);
      Write16(&bytes[6], (uint16_t)packet->data.mouse.y);
      bytes[8] = (uint8_t)packet->data.mouse.wheel;
      bytes[9] = packet->data.mouse.buttons;
      Write16(&bytes[10], packet->data.mouse.user_id);
      break;
    case WIRELESS_KEYBOARD:
      bytes[4] = packet->data.keyboard.modifiers;
      memcpy(&bytes[5], packet->data.keyboard.keys, sizeof(packet->data.keyboard.keys));
      break;
    case WIRELESS_ACK:
      bytes[4] = packet->data.ack.flags;
      bytes[5] = packet->data.ack.leds;
      bytes[6] = (uint8_t)packet->data.ack.accepted_kind;
      break;
    case WIRELESS_SYNC:
      break;
  }
}

bool WirelessPacket_Decode(WirelessPacket *packet, const uint8_t bytes[WIRELESS_PACKET_SIZE])
{
  packet->kind = (WirelessKind)bytes[0];
  packet->sequence = bytes[1];
  packet->epoch = Read16(&bytes[2]);
  if (packet->kind != WIRELESS_ACK && packet->epoch == 0U)
    return false;

  switch (packet->kind)
  {
    case WIRELESS_SYNC:
      return packet->sequence == 0U;
    case WIRELESS_MOUSE:
      if ((bytes[9] & 0xE0U) != 0U)
        return false;
      packet->data.mouse.x = ReadSigned16(&bytes[4]);
      packet->data.mouse.y = ReadSigned16(&bytes[6]);
      packet->data.mouse.wheel = bytes[8] <= INT8_MAX ? (int8_t)bytes[8] : (int8_t)((int)bytes[8] - 256);
      packet->data.mouse.buttons = bytes[9];
      packet->data.mouse.user_id = Read16(&bytes[10]);
      return true;
    case WIRELESS_KEYBOARD:
      for (unsigned i = 5U; i < 11U; ++i)
        if (bytes[i] > 0x65U) /* Matches the existing USB report descriptor. */
          return false;
      packet->data.keyboard.modifiers = bytes[4];
      memcpy(packet->data.keyboard.keys, &bytes[5], sizeof(packet->data.keyboard.keys));
      return bytes[11] == 0U;
    case WIRELESS_ACK:
      packet->data.ack.flags = bytes[4];
      packet->data.ack.leds = bytes[5];
      packet->data.ack.accepted_kind = (WirelessKind)bytes[6];
      if ((bytes[4] & ~(WIRELESS_ACK_READY | WIRELESS_ACK_SYNCED)) != 0U)
        return false;
      if ((bytes[4] & WIRELESS_ACK_SYNCED) == 0U)
        return packet->epoch == 0U;
      return (bytes[4] & WIRELESS_ACK_READY) != 0U && packet->epoch != 0U
             && (bytes[6] == WIRELESS_SYNC || bytes[6] == WIRELESS_MOUSE || bytes[6] == WIRELESS_KEYBOARD);
    default:
      return false;
  }
}
