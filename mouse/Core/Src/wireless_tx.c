#include "wireless_tx.h"
#include "nrf_motion.h"
#include <string.h>

#define INPUT_RETRY_MS 3U
#define SYNC_RETRY_MS 12U
#define HEARTBEAT_MS 25U
/* A full int16 movement can take over 250 one-millisecond USB reports. */
#define INPUT_PROGRESS_TIMEOUT_MS 1000U

static WirelessPacket outbound;
static uint8_t wire_bytes[WIRELESS_PACKET_SIZE];
static bool frame_pending, attempt_pending, enabled;
static uint16_t epoch;
static uint8_t sequence, held_buttons;
static volatile uint16_t active_user = WIRELESS_NO_USER;
static uint16_t sent_user = WIRELESS_NO_USER;
static volatile bool ready;
static volatile uint32_t session;
static volatile uint8_t keyboard_leds;
static uint32_t last_attempt, last_contact, frame_started, last_mouse;

static void QueueFrame(void)
{
  outbound.epoch = epoch;
  outbound.sequence = sequence;
  WirelessPacket_Encode(&outbound, wire_bytes);
  frame_started = HAL_GetTick();
  frame_pending = true;
}

static void BeginSession(uint32_t now)
{
  ready = false;
  keyboard_leds = 0U;
  held_buttons = 0U;
  sent_user = WIRELESS_NO_USER;
  attempt_pending = false;
  if (++epoch == 0U)
    ++epoch;
  sequence = 0U;
  outbound = (WirelessPacket){.kind = WIRELESS_SYNC};
  QueueFrame();
  last_attempt = now - SYNC_RETRY_MS;
}

bool WirelessTx_SendMouse(uint8_t buttons, int16_t x, int16_t y, int8_t wheel)
{
  if (!ready || frame_pending)
    return false;
  outbound = (WirelessPacket){.kind = WIRELESS_MOUSE};
  outbound.data.mouse.buttons = buttons & 0x1FU;
  outbound.data.mouse.x = x;
  outbound.data.mouse.y = y;
  outbound.data.mouse.wheel = wheel;
  outbound.data.mouse.user_id = active_user;
  QueueFrame();
  return true;
}

bool WirelessTx_SendKeyboard(uint8_t modifiers, const uint8_t keys[6])
{
  if (!ready || frame_pending)
    return false;
  outbound = (WirelessPacket){.kind = WIRELESS_KEYBOARD};
  outbound.data.keyboard.modifiers = modifiers;
  memcpy(outbound.data.keyboard.keys, keys, sizeof(outbound.data.keyboard.keys));
  QueueFrame();
  return true;
}

static void HandleAck(uint32_t now)
{
  uint8_t payload[WIRELESS_PACKET_SIZE];
  WirelessPacket reply;
  if (NrfMotion_ReadAck(payload) != NRF_RX_PACKET || !WirelessPacket_Decode(&reply, payload)
      || reply.kind != WIRELESS_ACK)
    return;

  if ((reply.data.ack.flags & WIRELESS_ACK_SYNCED) == 0U || reply.epoch != epoch)
  {
    if (ready)
      BeginSession(now);
    return;
  }

  last_contact = now;
  keyboard_leds = reply.data.ack.leds;
  if (reply.sequence != sequence || reply.data.ack.accepted_kind != outbound.kind)
    return;

  if (outbound.kind == WIRELESS_SYNC)
  {
    ++session;
    last_mouse = now;
    ready = true;
  }
  else if (outbound.kind == WIRELESS_MOUSE)
  {
    held_buttons = outbound.data.mouse.buttons;
    sent_user = outbound.data.mouse.user_id;
    last_mouse = now;
  }
  /* Only application acknowledgement advances the sequence. */
  frame_pending = false;
  ++sequence;
}

void WirelessTx_Service(bool allow_wireless)
{
  NrfMotion_PollTx();
  NrfMotionTxState state = NrfMotion_GetTxState();
  if (!allow_wireless || state == NRF_TX_UNAVAILABLE)
  {
    ready = false;
    enabled = frame_pending = attempt_pending = false;
    return;
  }

  uint32_t now = HAL_GetTick();
  if (!enabled)
  {
    enabled = true;
    BeginSession(now);
  }

  if (attempt_pending && state != NRF_TX_PENDING)
  {
    attempt_pending = false;
    if (state == NRF_TX_ACKED)
      HandleAck(now);
  }
  if (ready && ((uint32_t)(now - last_contact) >= WIRELESS_LINK_TIMEOUT_MS
                || (frame_pending && (uint32_t)(now - frame_started) >= INPUT_PROGRESS_TIMEOUT_MS)))
    BeginSession(now);

  if (ready && !frame_pending
      && ((uint32_t)(now - last_mouse) >= HEARTBEAT_MS || active_user != sent_user))
    (void)WirelessTx_SendMouse(held_buttons, 0, 0, 0);

  uint32_t interval = ready ? INPUT_RETRY_MS : SYNC_RETRY_MS;
  if (frame_pending && state != NRF_TX_PENDING && (uint32_t)(now - last_attempt) >= interval)
  {
    last_attempt = now;
    attempt_pending = NrfMotion_StartTx(wire_bytes);
  }
}

bool WirelessTx_IsReady(void) { return ready; }
uint32_t WirelessTx_GetSession(void) { return session; }
uint8_t WirelessTx_GetKeyboardLeds(void) { return keyboard_leds; }
void WirelessTx_SetUserId(uint16_t user_id) { active_user = user_id; }
