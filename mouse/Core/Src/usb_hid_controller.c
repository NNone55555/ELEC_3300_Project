/* Adapted from receiver/Core/Src/usb_hid_controller.c in this project.
 * Added checked results, persistent class TX storage and buffered mouse input.
 */
#include "usb_hid_controller.h"
#include "mouse_buttons.h"
#include "login_typing.h"
#include "user_profile.h"
#include "usb_device.h"
#include "usbd_hid.h"
#include <limits.h>
#include <string.h>

#define BUTTON_QUEUE_SIZE 32U
#define SIDE_BITS (MOUSE_BUTTON_SIDE1 | MOUSE_BUTTON_SIDE2)

_Static_assert(sizeof(MouseReport) == HID_MOUSE_REPORT_SIZE, "Mouse report layout");
_Static_assert(sizeof(KeyboardReport) == HID_KEYBOARD_REPORT_SIZE, "Keyboard report layout");

static int32_t pending_x;
static int32_t pending_y;
static int32_t pending_wheel;
static uint8_t button_queue[BUTTON_QUEUE_SIZE];
static uint8_t queue_head;
static uint8_t queue_count;
static uint8_t observed_buttons;
static uint8_t reported_buttons;
static bool connected;
static uint32_t active_session;
static uint32_t button_overflows;
static bool keyboard_sync_pending;
static bool macro_release_pending;
static uint8_t processed_sides, inhibited_sides;
static uint8_t side_macros[2] = {MACRO_BACK, MACRO_FORWARD};
static uint32_t macro_revision, applied_macro_revision;

void UsbHid_SetSideMacros(uint8_t side1, uint8_t side2)
{
  if (side1 >= MACRO_COUNT || side2 >= MACRO_COUNT) return;
  uint32_t irq = __get_PRIMASK(); __disable_irq();
  if (side_macros[0] != side1 || side_macros[1] != side2)
  {side_macros[0] = side1; side_macros[1] = side2; ++macro_revision;}
  __set_PRIMASK(irq);
}
void UsbHid_GetSideMacros(uint8_t *side1, uint8_t *side2)
{
  uint32_t irq = __get_PRIMASK(); __disable_irq();
  if (side1 != NULL) *side1 = side_macros[0];
  if (side2 != NULL) *side2 = side_macros[1];
  __set_PRIMASK(irq);
}

static uint8_t MouseButtons(uint8_t physical)
{
  uint8_t mapped = physical & 7U;
  for (unsigned side = 0; side < 2U; ++side)
    if (physical & (MOUSE_BUTTON_SIDE1 << side))
    {
      if (side_macros[side] == MACRO_BACK) mapped |= MOUSE_BUTTON_SIDE1;
      if (side_macros[side] == MACRO_FORWARD) mapped |= MOUSE_BUTTON_SIDE2;
    }
  return mapped;
}

bool Send_Mouse_Action(uint8_t buttons, int8_t x, int8_t y, int8_t wheel)
{
  const MouseReport report = {HID_MOUSE_REPORT_ID, buttons & 0x1FU, x, y, wheel};
  return USBD_HID_SendReport(&hUsbDeviceFS, (const uint8_t *)&report, sizeof(report)) == USBD_OK;
}

bool Send_Keyboard_Action(uint8_t modifiers, const uint8_t keys[6])
{
  if (keys == NULL)
    return false;
  KeyboardReport report = {HID_KEYBOARD_REPORT_ID, modifiers, 0, {0}};
  memcpy(report.keys, keys, sizeof(report.keys));
  return USBD_HID_SendReport(&hUsbDeviceFS, (const uint8_t *)&report, sizeof(report)) == USBD_OK;
}

bool UsbHid_IsConfigured(void)
{
  return hUsbDeviceFS.dev_state == USBD_STATE_CONFIGURED && hUsbDeviceFS.pClassData != NULL;
}

static int32_t AddSaturated(int32_t current, int32_t delta)
{
  int64_t sum = (int64_t)current + delta;
  return sum > INT32_MAX ? INT32_MAX : sum < INT32_MIN ? INT32_MIN : (int32_t)sum;
}

void UsbHid_AddMotion(int16_t x, int16_t y, int32_t wheel)
{
  uint32_t interrupt_state = __get_PRIMASK();
  __disable_irq();
  /* Never replay motion accumulated while disconnected, suspended or reset. */
  if (connected && UsbHid_IsConfigured() && active_session == USBD_HID_GetSession())
  {
    pending_x = AddSaturated(pending_x, x);
    pending_y = AddSaturated(pending_y, y);
    pending_wheel = AddSaturated(pending_wheel, wheel);
  }
  __set_PRIMASK(interrupt_state);
}

static int8_t ReportDelta(int32_t value)
{
  return value > 127 ? 127 : value < -127 ? -127 : (int8_t)value;
}

static void QueueButtons(uint8_t buttons)
{
  if (queue_count < BUTTON_QUEUE_SIZE)
  {
    button_queue[(queue_head + queue_count) % BUTTON_QUEUE_SIZE] = buttons;
    ++queue_count;
  }
  else
  {
    /* On prolonged host stalls, preserve the latest release/held state. */
    button_queue[(queue_head + queue_count - 1U) % BUTTON_QUEUE_SIZE] = buttons;
    ++button_overflows;
  }
}

void UsbHid_ServiceMouse(uint8_t buttons)
{
  buttons &= 0x1FU;
  uint8_t held_sides = buttons & SIDE_BITS;
  uint32_t irq = __get_PRIMASK(); __disable_irq();
  bool configured = UsbHid_IsConfigured();
  uint32_t session = USBD_HID_GetSession();
  if (!configured || !connected || active_session != session || applied_macro_revision != macro_revision)
  {
    bool remapping = applied_macro_revision != macro_revision;
    bool new_session = !configured || !connected || active_session != session;
    connected = configured; active_session = session; applied_macro_revision = macro_revision;
    if (new_session)
      pending_x = pending_y = pending_wheel = 0;
    queue_head = queue_count = 0U; reported_buttons = 0U;
    processed_sides = 0U; macro_release_pending = false; keyboard_sync_pending = true;
    if (remapping) inhibited_sides |= buttons & SIDE_BITS;
    buttons &= (uint8_t)~inhibited_sides;
    observed_buttons = buttons;
    if (configured) QueueButtons(buttons);
  }
  if (!configured)
  {
    Login_OnUsbUnavailable(); __set_PRIMASK(irq); return;
  }
  bool login_owns_keyboard = Login_Service();
  if (login_owns_keyboard)
  {
    inhibited_sides |= held_sides;
    for (uint8_t i = 0; i < queue_count; ++i)
      button_queue[(queue_head + i) % BUTTON_QUEUE_SIZE] &= (uint8_t)~SIDE_BITS;
    observed_buttons &= (uint8_t)~SIDE_BITS;
    /* A host may still hold a Back/Forward mouse button. Queue its release
     * even when no motion or other button change occurs during typing.
     */
    if ((reported_buttons & SIDE_BITS) != 0U && queue_count == 0U)
      QueueButtons(buttons & (uint8_t)~SIDE_BITS);
    macro_release_pending = false; processed_sides = 0U; keyboard_sync_pending = true;
    buttons &= (uint8_t)~SIDE_BITS;
  }
  inhibited_sides &= held_sides;
  buttons &= (uint8_t)~inhibited_sides;
  if (buttons != observed_buttons) {QueueButtons(buttons); observed_buttons = buttons;}
  uint8_t physical = queue_count ? button_queue[queue_head] : reported_buttons;
  const uint8_t no_keys[6] = {0};
  if (!login_owns_keyboard && (keyboard_sync_pending || macro_release_pending))
  {
    if (Send_Keyboard_Action(0, no_keys)) {keyboard_sync_pending = false; macro_release_pending = false;}
    __set_PRIMASK(irq); return;
  }
  if (!login_owns_keyboard)
  {
    uint8_t rising = physical & (uint8_t)~reported_buttons & SIDE_BITS & (uint8_t)~processed_sides;
    for (unsigned side = 0; side < 2U; ++side)
    {
      uint8_t bit = MOUSE_BUTTON_SIDE1 << side;
      uint8_t macro = side_macros[side];
      if ((rising & bit) && macro >= MACRO_COPY && macro <= MACRO_SELECT_ALL)
      {
        static const uint8_t usages[] = {0x06, 0x19, 0x1D, 0x04};
        uint8_t keys[6] = {usages[macro - MACRO_COPY]};
        if (Send_Keyboard_Action(1U, keys)) {processed_sides |= bit; macro_release_pending = true;}
        __set_PRIMASK(irq); return;
      }
    }
  }
  int8_t x = ReportDelta(pending_x), y = ReportDelta(pending_y), wheel = ReportDelta(pending_wheel);
  if ((queue_count || x || y || wheel) && Send_Mouse_Action(MouseButtons(physical), x, y, wheel))
  {
    pending_x -= x; pending_y -= y; pending_wheel -= wheel; reported_buttons = physical;
    if (queue_count) {queue_head = (queue_head + 1U) % BUTTON_QUEUE_SIZE; --queue_count; processed_sides = 0;}
  }
  __set_PRIMASK(irq);
}

uint32_t UsbHid_GetButtonOverflows(void)
{
  return button_overflows;
}
