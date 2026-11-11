/* Adapted from receiver/Core/Src/usb_hid_controller.c in this project.
 * Added checked results, persistent class TX storage and buffered mouse input.
 */
#include "usb_hid_controller.h"
#include "mouse_buttons.h"
#include "login_typing.h"
#include "usb_device.h"
#include "usbd_hid.h"
#include <limits.h>
#include <string.h>

#define BUTTON_QUEUE_SIZE 32U
#define KEYBOARD_MODIFIER_LEFT_GUI (1U << 3)

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
static bool reported_windows_key;
static bool keyboard_sync_pending;
static bool windows_inhibited;

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
  uint32_t interrupt_state = __get_PRIMASK();
  __disable_irq();
  bool configured = UsbHid_IsConfigured();
  uint32_t session = USBD_HID_GetSession();
  if (!configured || !connected || active_session != session)
  {
    connected = configured;
    active_session = session;
    pending_x = pending_y = pending_wheel = 0;
    queue_head = queue_count = 0U;
    observed_buttons = buttons;
    reported_buttons = 0U;
    reported_windows_key = false;
    keyboard_sync_pending = true;
    if (configured)
      QueueButtons(buttons); /* Resynchronize actual held buttons after enumeration. */
  }
  if (!configured)
  {
    Login_OnUsbUnavailable();
    __set_PRIMASK(interrupt_state);
    return;
  }
  bool login_owns_keyboard = Login_Service();
  if (login_owns_keyboard)
  {
    windows_inhibited = true;
    reported_windows_key = false;
    keyboard_sync_pending = true;
  }
  if (windows_inhibited)
  {
    /* Discard queued GUI edges during login; require a fresh press afterward.
     * Mouse buttons and motion continue while password key reports are paced.
     */
    for (uint8_t i = 0U; i < queue_count; ++i)
      button_queue[(queue_head + i) % BUTTON_QUEUE_SIZE] &= (uint8_t)~MOUSE_BUTTON_SIDE1;
    observed_buttons &= (uint8_t)~MOUSE_BUTTON_SIDE1;
    reported_buttons &= (uint8_t)~MOUSE_BUTTON_SIDE1;
    if (!login_owns_keyboard && (buttons & MOUSE_BUTTON_SIDE1) == 0U)
      windows_inhibited = false;
    buttons &= (uint8_t)~MOUSE_BUTTON_SIDE1;
  }
  if (buttons != observed_buttons)
  {
    QueueButtons(buttons);
    observed_buttons = buttons;
  }
  uint8_t report_buttons = queue_count != 0U ? button_queue[queue_head] : reported_buttons;
  bool windows_key = (report_buttons & MOUSE_BUTTON_SIDE1) != 0U;
  if (!login_owns_keyboard && (keyboard_sync_pending || windows_key != reported_windows_key))
  {
    /* Keep this edge queued until both its keyboard and mouse state are sent.
     * A quick tap during USB BUSY must still deliver Windows down, then up.
     */
    const uint8_t keys[6] = {0};
    if (Send_Keyboard_Action(windows_key ? KEYBOARD_MODIFIER_LEFT_GUI : 0U, keys))
    {
      reported_windows_key = windows_key;
      keyboard_sync_pending = false;
    }
    __set_PRIMASK(interrupt_state);
    return;
  }
  int8_t x = ReportDelta(pending_x);
  int8_t y = ReportDelta(pending_y);
  int8_t wheel = ReportDelta(pending_wheel);
  bool send = queue_count != 0U || x != 0 || y != 0 || wheel != 0;
  /* This nested critical section stays short: one <=5-byte USB PMA copy.
   * It prevents a task switch adding motion or a USB reset halfway through.
   */
  /* Side 1 is the Windows key for this test; suppress its mouse Back button. */
  uint8_t mouse_buttons = report_buttons & (uint8_t)~MOUSE_BUTTON_SIDE1;
  if (send && Send_Mouse_Action(mouse_buttons, x, y, wheel))
  {
    pending_x -= x;
    pending_y -= y;
    pending_wheel -= wheel;
    reported_buttons = report_buttons;
    if (queue_count != 0U)
    {
      queue_head = (queue_head + 1U) % BUTTON_QUEUE_SIZE;
      --queue_count;
    }
  }
  __set_PRIMASK(interrupt_state);
}

uint32_t UsbHid_GetButtonOverflows(void)
{
  return button_overflows;
}
