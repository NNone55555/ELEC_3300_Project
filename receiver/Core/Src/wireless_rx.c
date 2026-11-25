#include "wireless_rx.h"
#include "nrf_motion.h"
#include "usb_hid_controller.h"
#include "usbd_hid.h"
#include "boot_epoch.h"

static WirelessRxStatus status = {.user_id = WIRELESS_NO_USER};
static WirelessPacket pending;
static bool report_pending;
static bool release_mouse = true, release_keyboard = true;
static bool usb_configured;
static uint32_t usb_session, last_packet;
static uint16_t epoch;
static uint8_t accepted_sequence;
static WirelessKind accepted_kind;
static RadioBinding active_binding, candidate;
static RadioReplay replay;

static void ResetLink(void)
{
  status.linked = false;
  status.user_id = WIRELESS_NO_USER;
  pending = (WirelessPacket){0};
  report_pending = false;
  release_mouse = release_keyboard = true;
  active_binding = (RadioBinding){0};
}

static int8_t HidDelta(int16_t value)
{
  return value > 127 ? 127 : value < -127 ? -127 : (int8_t)value;
}

static void DeliverHid(void)
{
  if (!usb_configured)
    return;
  const uint8_t no_keys[6] = {0};
  if (release_keyboard)
  {
    if (Send_Keyboard_Action(0U, no_keys))
      release_keyboard = false;
    return;
  }
  if (release_mouse)
  {
    if (Send_Mouse_Action(0U, 0, 0, 0))
      release_mouse = false;
    return;
  }
  if (!report_pending)
    return;

  if (pending.kind == WIRELESS_KEYBOARD)
  {
    if (!Send_Keyboard_Action(pending.data.keyboard.modifiers, pending.data.keyboard.keys))
      return;
  }
  else
  {
    int8_t x = HidDelta(pending.data.mouse.x);
    int8_t y = HidDelta(pending.data.mouse.y);
    int8_t wheel = HidDelta(pending.data.mouse.wheel);
    if (!Send_Mouse_Action(pending.data.mouse.buttons, x, y, wheel))
      return;
    pending.data.mouse.x -= x;
    pending.data.mouse.y -= y;
    pending.data.mouse.wheel -= wheel;
    if (pending.data.mouse.x || pending.data.mouse.y || pending.data.mouse.wheel)
      return;
    status.user_id = pending.data.mouse.user_id;
  }

  accepted_kind = pending.kind;
  accepted_sequence = pending.sequence;
  ++status.accepted;
  pending = (WirelessPacket){0};
  report_pending = false;
}

static void ReceivePacket(const WirelessPacket *packet, uint32_t now, bool proof)
{
  if (!usb_configured || release_mouse || release_keyboard)
    return;
  if (proof)
  {
    if (packet->kind != WIRELESS_SYNC)
      return;
    if (status.linked)
    {
      ResetLink();
      return; /* The next proof can open the link after neutral reports. */
    }
    active_binding = candidate;
    candidate = (RadioBinding){0};
  }
  else if (!status.linked)
    return;

  if (packet->kind == WIRELESS_SYNC)
  {
    if (status.linked && packet->epoch != epoch)
    {
      /* Release all of the old session's inputs before accepting the new one. */
      ResetLink();
      return;
    }
    if (!status.linked)
    {
      epoch = packet->epoch;
      accepted_sequence = 0U;
      accepted_kind = WIRELESS_SYNC;
      status.linked = true;
      last_packet = now;
    }
    else if (accepted_kind == WIRELESS_SYNC && !report_pending)
    {
      last_packet = now;
    }
    return;
  }
  if (packet->kind == WIRELESS_ACK || !status.linked || packet->epoch != epoch)
    return;

  if ((packet->sequence == accepted_sequence && packet->kind == accepted_kind)
      || (report_pending && packet->sequence == pending.sequence && packet->kind == pending.kind))
  {
    last_packet = now;
    ++status.duplicates;
  }
  else if (!report_pending && packet->sequence == (uint8_t)(accepted_sequence + 1U))
  {
    last_packet = now;
    pending = *packet;
    report_pending = true;
  }
}

static void QueueFeedback(void)
{
  WirelessPacket ack = {
    .kind = WIRELESS_ACK, .sequence = accepted_sequence, .epoch = epoch,
    .data.ack = {WIRELESS_ACK_READY | WIRELESS_ACK_SYNCED, USBD_HID_GetKeyboardLeds(), accepted_kind}
  };
  uint8_t body[WIRELESS_PACKET_SIZE], wire[SECURE_RADIO_BYTES];
  WirelessPacket_Encode(&ack, body);
  /* The FIFO is bounded. If full, the sender's next retry consumes a slot. */
  if (SecureRadio_Seal(RADIO_FEEDBACK, &active_binding, body, wire))
    (void)NrfMotion_QueueAck(wire);
}

static void ReceiveSecure(const uint8_t wire[SECURE_RADIO_BYTES], uint32_t now)
{
  uint8_t body[WIRELESS_PACKET_SIZE];
  if (SecureRadio_Message(wire) == RADIO_HELLO)
  {
    if (!usb_configured || release_mouse || release_keyboard)
      return;
    if (!SecureRadio_Open(RADIO_HELLO, NULL, &replay, wire, body))
    {
      ++status.rejected;
      return;
    }
    uint32_t hello = SecureRadio_ReadTicket(body);
    uint64_t mouse_boot = SecureRadio_PeerBoot(wire);
    if (hello == 0U)
      return;
    ++status.hellos;
    if (candidate.mouse_boot != mouse_boot || candidate.hello != hello)
      candidate = (RadioBinding){mouse_boot, BootEpoch_Value(), hello, SecureRadio_NextCounter()};
    if (candidate.challenge != 0U)
    {
      uint8_t reply[SECURE_RADIO_BYTES];
      SecureRadio_WriteTicket(body, candidate.challenge);
      if (SecureRadio_Seal(RADIO_CHALLENGE, &candidate, body, reply))
        (void)NrfMotion_QueueAck(reply);
    }
    return;
  }

  bool proof = candidate.challenge != 0U && SecureRadio_Open(RADIO_INPUT, &candidate, &replay, wire, body);
  if (!proof && (active_binding.challenge == 0U
                 || !SecureRadio_Open(RADIO_INPUT, &active_binding, &replay, wire, body)))
  {
    ++status.rejected;
    return;
  }
  uint32_t reply_challenge = proof ? candidate.challenge : active_binding.challenge;
  WirelessPacket packet;
  if (!WirelessPacket_Decode(&packet, body))
    return;
  ReceivePacket(&packet, now, proof);
  DeliverHid();
  /* Feedback for a new handshake is useful only after it has taken over. */
  if (status.linked && active_binding.challenge == reply_challenge)
    QueueFeedback();
}

void WirelessRx_Service(void)
{
  uint32_t now = HAL_GetTick();
  uint32_t current_session = USBD_HID_GetSession();
  bool configured = UsbHid_IsConfigured();
  if (configured != usb_configured || current_session != usb_session)
  {
    if (status.linked)
      ++status.usb_disconnects;
    ResetLink();
    candidate = (RadioBinding){0};
  }
  else if (status.linked && (uint32_t)(now - last_packet) >= WIRELESS_LINK_TIMEOUT_MS)
  {
    ++status.link_timeouts;
    ResetLink(); /* A fresh handshake can already be in progress. */
  }

  usb_configured = configured;
  usb_session = current_session;
  status.usb_ready = configured;
  DeliverHid();
  status.radio_error = BootEpoch_Value() == 0;
  if (status.radio_error)
    return;
  for (unsigned i = 0; i < 3U; ++i)
  {
    uint8_t wire_bytes[SECURE_RADIO_BYTES];
    NrfMotionRxResult result = NrfMotion_Read(wire_bytes);
    if (result != NRF_RX_PACKET)
    {
      status.radio_error = result == NRF_RX_ERROR;
      break;
    }
    ReceiveSecure(wire_bytes, now);
  }
}

WirelessRxStatus WirelessRx_GetStatus(void)
{
  return status;
}
