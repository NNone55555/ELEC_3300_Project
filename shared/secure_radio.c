#include "secure_radio.h"
#include "aes_ccm.h"
#include "boot_epoch.h"
#include "radio_key.h"
#include <string.h>

#define BODY_OFFSET AES_CCM_NONCE_BYTES
#define TAG_OFFSET (BODY_OFFSET + WIRELESS_PACKET_SIZE)
#define BOOT_MASK (UINT64_MAX >> 2)

_Static_assert(SECURE_RADIO_BYTES == TAG_OFFSET + AES_CCM_TAG_BYTES, "Radio frame size");
static uint32_t next_counter;

static void WriteLittle(uint8_t *out, uint64_t value, unsigned length)
{
  for (unsigned i = 0; i < length; ++i)
  {
    out[i] = (uint8_t)value;
    value >>= 8;
  }
}

static uint64_t ReadLittle(const uint8_t *in, unsigned length)
{
  uint64_t value = 0;
  while (length != 0U)
    value = (value << 8) | in[--length];
  return value;
}

uint32_t SecureRadio_NextCounter(void)
{
  return next_counter == UINT32_MAX ? 0U : ++next_counter;
}

RadioMessage SecureRadio_Message(const uint8_t wire[SECURE_RADIO_BYTES])
{
  return (RadioMessage)(wire[7] >> 6);
}

uint64_t SecureRadio_PeerBoot(const uint8_t wire[SECURE_RADIO_BYTES])
{
  return ReadLittle(wire, 8) & BOOT_MASK;
}

/* HELLO authenticates only its nonce and body. CHALLENGE binds to the mouse's
 * specific HELLO. Input and feedback bind to both boot epochs and tickets.
 */
static size_t AssociatedData(RadioMessage message, const RadioBinding *binding, uint8_t aad[28])
{
  memcpy(aad, "ELR1", 4);
  if (message == RADIO_HELLO)
    return 4;
  WriteLittle(aad + 4, binding->mouse_boot, 8);
  WriteLittle(aad + 12, binding->hello, 4);
  if (message == RADIO_CHALLENGE)
    return 16;
  WriteLittle(aad + 16, binding->receiver_boot, 8);
  WriteLittle(aad + 24, binding->challenge, 4);
  return 28;
}

bool SecureRadio_Seal(RadioMessage message, const RadioBinding *binding,
                      const uint8_t body[WIRELESS_PACKET_SIZE], uint8_t wire[SECURE_RADIO_BYTES])
{
  uint64_t boot = BootEpoch_Value();
  uint32_t counter = SecureRadio_NextCounter();
  if (boot == 0 || counter == 0)
    return false;

  WriteLittle(wire, boot | ((uint64_t)message << 62), 8);
  WriteLittle(wire + 8, counter, 4);
  memcpy(wire + BODY_OFFSET, body, WIRELESS_PACKET_SIZE);
  uint8_t aad[28];
  size_t length = AssociatedData(message, binding, aad);
  return AesCcm_Seal(RADIO_KEY, wire, aad, length, wire + BODY_OFFSET, WIRELESS_PACKET_SIZE,
                     wire + TAG_OFFSET);
}

bool SecureRadio_Open(RadioMessage message, const RadioBinding *binding, RadioReplay *replay,
                      const uint8_t wire[SECURE_RADIO_BYTES], uint8_t body[WIRELESS_PACKET_SIZE])
{
  uint64_t boot = SecureRadio_PeerBoot(wire);
  uint32_t counter = (uint32_t)ReadLittle(wire + 8, 4);
  if (SecureRadio_Message(wire) != message || boot == 0 || counter == 0
      || boot < replay->boot || (boot == replay->boot && counter <= replay->counter))
    return false;
  if ((message == RADIO_INPUT && boot != binding->mouse_boot)
      || (message == RADIO_FEEDBACK && boot != binding->receiver_boot))
    return false;

  memcpy(body, wire + BODY_OFFSET, WIRELESS_PACKET_SIZE);
  uint8_t aad[28];
  size_t length = AssociatedData(message, binding, aad);
  if (!AesCcm_Open(RADIO_KEY, wire, aad, length, body, WIRELESS_PACKET_SIZE, wire + TAG_OFFSET))
    return false;
  replay->boot = boot;
  replay->counter = counter;
  return true;
}

void SecureRadio_WriteTicket(uint8_t body[WIRELESS_PACKET_SIZE], uint32_t ticket)
{
  memset(body, 0, WIRELESS_PACKET_SIZE);
  WriteLittle(body, ticket, 4);
}

uint32_t SecureRadio_ReadTicket(const uint8_t body[WIRELESS_PACKET_SIZE])
{
  return (uint32_t)ReadLittle(body, 4);
}
