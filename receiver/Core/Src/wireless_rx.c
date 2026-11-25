#include "wireless_rx.h"
#include "nrf_motion.h"
#include "usb_hid_controller.h"
#include "usbd_hid.h"

static WirelessRxStatus status = {.user_id = WIRELESS_NO_USER};
static WirelessPacket pending;
static bool report_pending;
static bool release_mouse = true, release_keyboard = true;
static bool usb_configured;
static uint32_t usb_session, last_packet;
static uint16_t epoch;
static uint8_t accepted_sequence;
static WirelessKind accepted_kind;

static void ResetLink(void)
{
  status.linked = false;
  status.user_id = WIRELESS_NO_USER;
  pending = (WirelessPacket){0};
  report_pending = false;
  release_mouse = release_keyboard = true;
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

static void ReceivePacket(const WirelessPacket *packet, uint32_t now)
{
  if (!usb_configured || release_mouse || release_keyboard)
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
    /* A mouse reboot can reuse an epoch. If data has already been accepted,
     * the old session expires before this SYNC can open a new one. */
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
  WirelessPacket ack = {.kind = WIRELESS_ACK};
  if (usb_configured && !release_mouse && !release_keyboard)
  {
    ack.data.ack.flags = WIRELESS_ACK_READY;
    ack.data.ack.leds = USBD_HID_GetKeyboardLeds();
    if (status.linked)
    {
      ack.data.ack.flags |= WIRELESS_ACK_SYNCED;
      ack.data.ack.accepted_kind = accepted_kind;
      ack.epoch = epoch;
      ack.sequence = accepted_sequence;
    }
  }
  uint8_t wire_bytes[WIRELESS_PACKET_SIZE];
  WirelessPacket_Encode(&ack, wire_bytes);
  /* The FIFO is bounded. If full, the sender's next retry consumes a slot. */
  (void)NrfMotion_QueueAck(wire_bytes);
}

void WirelessRx_Service(void)
{
  uint32_t now = HAL_GetTick();
  uint32_t current_session = USBD_HID_GetSession();
  bool configured = UsbHid_IsConfigured();
  if (configured != usb_configured || current_session != usb_session
      || (status.linked && (uint32_t)(now - last_packet) >= WIRELESS_LINK_TIMEOUT_MS))
    ResetLink();

  usb_configured = configured;
  usb_session = current_session;
  status.usb_ready = configured;
  DeliverHid();
  status.radio_error = false;
  for (unsigned i = 0; i < 3U; ++i)
  {
    uint8_t wire_bytes[WIRELESS_PACKET_SIZE];
    NrfMotionRxResult result = NrfMotion_Read(wire_bytes);
    if (result != NRF_RX_PACKET)
    {
      status.radio_error = result == NRF_RX_ERROR;
      break;
    }
    WirelessPacket packet;
    if (WirelessPacket_Decode(&packet, wire_bytes))
      ReceivePacket(&packet, now);
    DeliverHid();
    QueueFeedback();
  }
}

WirelessRxStatus WirelessRx_GetStatus(void)
{
  return status;
}
