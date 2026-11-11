/* Exercises the actual ST USB core, application HID class, controller and
 * button driver. The low-level USB peripheral and GPIOs are simulated.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "usb_device.h"
#include "usbd_hid.h"
#include "usb_hid_controller.h"
#include "mouse_buttons.h"
#include "login_typing.h"
#include "main.h"

GPIO_TypeDef test_gpio[5];
uint32_t test_primask;
USBD_HandleTypeDef hUsbDeviceFS;
extern USBD_DescriptorsTypeDef FS_Desc;
static uint8_t *control_tx;
static uint8_t *control_rx;
static uint16_t control_length;
static uint16_t receive_length;
static uint8_t *interrupt_tx;
static uint8_t captured_report[16];
static uint16_t captured_size;
static unsigned interrupt_transfers;
static unsigned stalls;
static uint8_t address_set;
static USBD_StatusTypeDef transmit_result;
static uint32_t test_tick;
static uint8_t keyboard_history[128][9];
static unsigned keyboard_count;
uint32_t HAL_GetTick(void) { return test_tick; }

USBD_StatusTypeDef USBD_LL_Init(USBD_HandleTypeDef *d) { (void)d; return USBD_OK; }
USBD_StatusTypeDef USBD_LL_DeInit(USBD_HandleTypeDef *d) { (void)d; return USBD_OK; }
USBD_StatusTypeDef USBD_LL_Start(USBD_HandleTypeDef *d) { (void)d; return USBD_OK; }
USBD_StatusTypeDef USBD_LL_Stop(USBD_HandleTypeDef *d) { (void)d; return USBD_OK; }
USBD_StatusTypeDef USBD_LL_OpenEP(USBD_HandleTypeDef *d, uint8_t ep, uint8_t type, uint16_t size)
{
  (void)d;
  assert((ep == 0x81U && type == USBD_EP_TYPE_INTR && size == 16U)
         || ((ep == 0x00U || ep == 0x80U) && type == USBD_EP_TYPE_CTRL && size == 64U));
  return USBD_OK;
}
USBD_StatusTypeDef USBD_LL_CloseEP(USBD_HandleTypeDef *d, uint8_t ep)
{ (void)d; assert(ep == 0x81U); return USBD_OK; }
USBD_StatusTypeDef USBD_LL_FlushEP(USBD_HandleTypeDef *d, uint8_t ep)
{ (void)d; (void)ep; return USBD_OK; }
USBD_StatusTypeDef USBD_LL_StallEP(USBD_HandleTypeDef *d, uint8_t ep)
{ (void)d; (void)ep; ++stalls; return USBD_OK; }
USBD_StatusTypeDef USBD_LL_ClearStallEP(USBD_HandleTypeDef *d, uint8_t ep)
{ (void)d; (void)ep; return USBD_OK; }
uint8_t USBD_LL_IsStallEP(USBD_HandleTypeDef *d, uint8_t ep)
{ (void)d; (void)ep; return 0; }
USBD_StatusTypeDef USBD_LL_SetUSBAddress(USBD_HandleTypeDef *d, uint8_t address)
{ (void)d; address_set = address; return USBD_OK; }
USBD_StatusTypeDef USBD_LL_Transmit(USBD_HandleTypeDef *d, uint8_t ep, uint8_t *data, uint16_t size)
{
  (void)d;
  if ((ep & 0x7FU) == 0U)
  {
    control_tx = data;
    control_length = size;
    return USBD_OK;
  }
  assert(ep == 0x81U && size <= sizeof(captured_report) && test_primask == 1U);
  if (transmit_result != USBD_OK)
    return transmit_result;
  interrupt_tx = data;
  memcpy(captured_report, data, size);
  captured_size = size;
  ++interrupt_transfers;
  if (size == sizeof(KeyboardReport))
  {
    assert(keyboard_count < 128U);
    memcpy(keyboard_history[keyboard_count++], data, size);
  }
  return USBD_OK;
}
USBD_StatusTypeDef USBD_LL_PrepareReceive(USBD_HandleTypeDef *d, uint8_t ep,
                                         uint8_t *data, uint16_t size)
{
  (void)d; assert(ep == 0x00U);
  control_rx = data;
  receive_length = size;
  return USBD_OK;
}
uint32_t USBD_LL_GetRxDataSize(USBD_HandleTypeDef *d, uint8_t ep)
{ (void)d; (void)ep; return receive_length; }
void USBD_LL_Delay(uint32_t ms) { (void)ms; }

static void Request(uint8_t type, uint8_t request, uint16_t value, uint16_t index, uint16_t size)
{
  uint8_t setup[8] = {type, request, LOBYTE(value), HIBYTE(value),
                      LOBYTE(index), HIBYTE(index), LOBYTE(size), HIBYTE(size)};
  control_tx = control_rx = NULL;
  control_length = receive_length = 0U;
  assert(USBD_LL_SetupStage(&hUsbDeviceFS, setup) == USBD_OK);
  assert(test_primask == 0U);
}

static void CompleteInterrupt(void)
{
  assert(USBD_LL_DataInStage(&hUsbDeviceFS, 1U, interrupt_tx) == USBD_OK);
}

static void SynchronizeMouse(uint8_t buttons)
{
  /* A new USB session sends keyboard state before mouse state. */
  for (unsigned i = 0; i < 4; ++i)
  {
    unsigned before = interrupt_transfers;
    UsbHid_ServiceMouse(buttons);
    if (interrupt_transfers != before)
      CompleteInterrupt();
  }
}

static void Enumerate(void)
{
  assert(USBD_LL_SetSpeed(&hUsbDeviceFS, USBD_SPEED_FULL) == USBD_OK);
  assert(USBD_LL_Reset(&hUsbDeviceFS) == USBD_OK);
  Request(0x00, USB_REQ_SET_ADDRESS, 5U, 0U, 0U);
  assert(address_set == 5U && hUsbDeviceFS.dev_state == USBD_STATE_ADDRESSED);
  Request(0x00, USB_REQ_SET_CONFIGURATION, 1U, 0U, 0U);
  assert(UsbHid_IsConfigured());
}

static void TestDescriptors(void)
{
  Request(0x80, USB_REQ_GET_DESCRIPTOR, USB_DESC_TYPE_DEVICE << 8, 0, 18);
  assert(control_length == 18 && control_tx[7] == 64 && control_tx[17] == 1);
  Request(0x80, USB_REQ_GET_DESCRIPTOR, USB_DESC_TYPE_CONFIGURATION << 8, 0, 255);
  assert(control_length == 34 && control_tx[4] == 1);
  assert(control_tx[14] == 3 && control_tx[15] == 0 && control_tx[16] == 0);
  assert(control_tx[29] == 0x81 && control_tx[31] == 16 && control_tx[33] == 1);
  uint16_t descriptor_length = control_tx[25] | ((uint16_t)control_tx[26] << 8);
  Request(0x81, USB_REQ_GET_DESCRIPTOR, 0x2200, 0, 255);
  assert(control_length == descriptor_length);
  unsigned input_bits[3] = {0}, output_bits[3] = {0};
  unsigned report_id = 0, report_size = 0, report_count = 0, page = 0;
  unsigned depth = 0, applications = 0, relative_axes = 0, buttons = 0;
  int logical_min = 0, logical_max = 0;
  for (unsigned i = 0; i < control_length;)
  {
    uint8_t prefix = control_tx[i++];
    unsigned bytes = prefix & 3U;
    if (bytes == 3U) bytes = 4U;
    assert(i + bytes <= control_length);
    unsigned value = 0;
    for (unsigned byte = 0; byte < bytes; ++byte)
      value |= (unsigned)control_tx[i++] << (8U * byte);
    unsigned tag = prefix >> 4, type = (prefix >> 2) & 3U;
    if (type == 1U)
    {
      if (tag == 0U) page = value;
      if (tag == 1U) logical_min = (int8_t)value;
      if (tag == 2U) logical_max = (int8_t)value;
      if (tag == 7U) report_size = value;
      if (tag == 8U) report_id = value;
      if (tag == 9U) report_count = value;
    }
    if (type == 0U && (tag == 8U || tag == 9U))
    {
      assert(report_id == 1U || report_id == 2U);
      if (tag == 8U) input_bits[report_id] += report_size * report_count;
      else output_bits[report_id] += report_size * report_count;
      if (report_id == 1U && tag == 8U && page == 9U && (value & 1U) == 0U)
        buttons += report_count;
      if (report_id == 1U && tag == 8U && page == 1U && value == 6U)
      {
        assert(logical_min == -127 && logical_max == 127);
        relative_axes += report_count;
      }
    }
    if (type == 0U && tag == 10U)
    {
      if (depth == 0U) { assert(value == 1U); ++applications; }
      ++depth;
    }
    if (type == 0U && tag == 12U) { assert(depth != 0U); --depth; }
  }
  assert(depth == 0U && applications == 2U && buttons == 5U && relative_axes == 3U);
  assert(input_bits[1] == 32U && input_bits[2] == 64U && output_bits[2] == 8U);
  puts("PASS: descriptors expose five-button mouse + keyboard, correct report sizes/IDs, 1 ms endpoint.");
}

static void TestButtons(void)
{
  for (unsigned port = 0; port < 5; ++port) test_gpio[port].levels = 0xFFFF;
  MouseButtons_Init();
  assert(MouseButtons_GetSnapshot().pin_levels == 0x1F && MouseButtons_GetSnapshot().pressed == 0);
  GPIOC->levels &= ~BTN_LEFT_Pin;
  for (unsigned i = 0; i < 4; ++i) MouseButtons_Sample1ms();
  assert(MouseButtons_GetSnapshot().pressed == 0 && MouseButtons_GetSnapshot().pin_levels == 0x1E);
  MouseButtons_Sample1ms();
  assert(MouseButtons_GetSnapshot().pressed == MOUSE_BUTTON_LEFT);
  GPIOC->levels |= BTN_LEFT_Pin; MouseButtons_Sample1ms();
  GPIOC->levels &= ~BTN_LEFT_Pin; MouseButtons_Sample1ms();
  assert(MouseButtons_GetSnapshot().pressed == MOUSE_BUTTON_LEFT);
  GPIOC->levels |= BTN_LEFT_Pin;
  for (unsigned i = 0; i < 5; ++i) MouseButtons_Sample1ms();
  assert(MouseButtons_GetSnapshot().pressed == 0);
  GPIOC->levels &= ~(BTN_LEFT_Pin | BTN_RIGHT_Pin | BTN_SIDE1_Pin);
  GPIOD->levels &= ~BTN_SIDE2_Pin;
  GPIOE->levels &= ~BTN_MIDDLE_Pin;
  for (unsigned i = 0; i < 5; ++i) MouseButtons_Sample1ms();
  assert(MouseButtons_GetSnapshot().pressed == 0x1F && MouseButtons_GetSnapshot().pin_levels == 0);
  assert(test_primask == 0);
  puts("PASS: configured GPIO mapping, active-low buttons, raw levels, independent debounce and bounce rejection.");
}

static void TestMouseTransport(void)
{
  SynchronizeMouse(0);
  assert(captured_size == sizeof(MouseReport) && captured_report[0] == 1);
  UsbHid_AddMotion(320, -1000, 300);
  int x = 0, y = 0, wheel = 0;
  for (unsigned i = 0; i < 10; ++i)
  {
    unsigned before = interrupt_transfers;
    UsbHid_ServiceMouse(0);
    if (interrupt_transfers != before)
    {
      x += (int8_t)captured_report[2];
      y += (int8_t)captured_report[3];
      wheel += (int8_t)captured_report[4];
      CompleteInterrupt();
    }
  }
  assert(x == 320 && y == -1000 && wheel == 300);

  /* Queue press + release while a prior report occupies the endpoint. */
  assert(Send_Mouse_Action(0, 3, 4, 0));
  uint8_t copy[5]; memcpy(copy, interrupt_tx, 5);
  UsbHid_ServiceMouse(MOUSE_BUTTON_LEFT);
  UsbHid_ServiceMouse(0);
  assert(memcmp(interrupt_tx, copy, 5) == 0);
  CompleteInterrupt(); UsbHid_ServiceMouse(0);
  assert(captured_report[1] == MOUSE_BUTTON_LEFT);
  CompleteInterrupt(); UsbHid_ServiceMouse(0);
  assert(captured_report[1] == 0); CompleteInterrupt();

  /* Low-level failure keeps pending deltas for retry. */
  UsbHid_AddMotion(10, -20, 1);
  transmit_result = USBD_FAIL;
  unsigned before = interrupt_transfers; UsbHid_ServiceMouse(0);
  assert(interrupt_transfers == before);
  transmit_result = USBD_OK; UsbHid_ServiceMouse(0);
  assert(captured_report[2] == 10 && (int8_t)captured_report[3] == -20 && captured_report[4] == 1);
  CompleteInterrupt();

  /* A long host stall overflows the finite edge queue, but final release survives. */
  assert(Send_Mouse_Action(0, 0, 0, 0));
  for (unsigned i = 0; i < 70; ++i) UsbHid_ServiceMouse((i & 1U) ? 0 : MOUSE_BUTTON_LEFT);
  assert(UsbHid_GetButtonOverflows() != 0);
  CompleteInterrupt();
  for (unsigned i = 0; i < 35; ++i)
  {
    before = interrupt_transfers; UsbHid_ServiceMouse(0);
    if (before != interrupt_transfers) CompleteInterrupt();
  }
  assert(captured_report[1] == 0);
  assert(test_primask == 0);
  puts("PASS: motion/scroll splitting without lost counts, busy retry, persistent TX, queued click/release, overflow recovery.");
}

static void TestKeyboardAndControl(void)
{
  uint8_t keys[6] = {4};
  assert(!Send_Keyboard_Action(0, NULL));
  assert(Send_Keyboard_Action(2, keys));
  assert(captured_size == 9 && captured_report[0] == 2 && captured_report[1] == 2 && captured_report[3] == 4);
  memset(keys, 0, sizeof(keys));
  assert(interrupt_tx[3] == 4 && !Send_Keyboard_Action(0, keys));
  Request(0xA1, 0x01, 0x0102, 0, 9); /* GET_REPORT keyboard. */
  assert(control_length == 9 && control_tx[3] == 4);
  CompleteInterrupt(); assert(Send_Keyboard_Action(0, keys));
  for (unsigned i = 1; i < 9; ++i) assert(captured_report[i] == 0);
  CompleteInterrupt();

  Request(0x21, 0x09, 0x0202, 0, 2); /* SET_REPORT keyboard LEDs. */
  assert(control_rx != NULL && receive_length == 2);
  control_rx[0] = 2; control_rx[1] = 3;
  USBD_LL_DataOutStage(&hUsbDeviceFS, 0, control_rx);
  Request(0xA1, 0x01, 0x0202, 0, 2);
  assert(control_length == 2 && control_tx[0] == 2 && control_tx[1] == 3);

  assert(Send_Mouse_Action(MOUSE_BUTTON_RIGHT, 7, -5, 1)); CompleteInterrupt();
  Request(0x21, 0x0A, 0x0101, 0, 0); /* SET_IDLE mouse, 4 ms. */
  Request(0xA1, 0x02, 1, 0, 1);
  assert(control_length == 1 && control_tx[0] == 1);
  unsigned before = interrupt_transfers;
  for (unsigned i = 0; i < 4; ++i) USBD_LL_SOF(&hUsbDeviceFS);
  assert(interrupt_transfers == before + 1 && captured_report[1] == MOUSE_BUTTON_RIGHT);
  assert(captured_report[2] == 0 && captured_report[3] == 0 && captured_report[4] == 0);
  CompleteInterrupt(); Request(0x21, 0x0A, 0, 0, 0);

  before = stalls; Request(0x21, 0x09, 0x0202, 0, 255); assert(stalls > before);
  before = stalls; Request(0x81, USB_REQ_GET_DESCRIPTOR, 0x2200, 1, 255); assert(stalls > before);
  before = stalls; Request(0x21, 0x0B, 0, 0, 0); assert(stalls > before); /* No boot protocol. */
  assert(USBD_HID_SendReport(&hUsbDeviceFS, NULL, 5) == USBD_FAIL);
  const uint8_t bad_report[5] = {2,0,0,0,0};
  assert(USBD_HID_SendReport(&hUsbDeviceFS, bad_report, 5) == USBD_FAIL);
  puts("PASS: keyboard modifiers/keys/release, busy detection, LED control, GET_REPORT/IDLE, zero-motion idle reports, invalid requests.");
}

static void AssertWindowsReport(bool pressed)
{
  assert(captured_size == sizeof(KeyboardReport) && captured_report[0] == 2);
  assert(captured_report[1] == (pressed ? 0x08 : 0x00));
  for (unsigned i = 2; i < sizeof(KeyboardReport); ++i)
    assert(captured_report[i] == 0);
}

static void TestSide1WindowsKey(void)
{
  /* Press/release both happen while the previous mouse report is still busy. */
  assert(Send_Mouse_Action(0, 0, 0, 0));
  unsigned before = interrupt_transfers;
  UsbHid_ServiceMouse(MOUSE_BUTTON_SIDE1);
  UsbHid_ServiceMouse(0);
  assert(interrupt_transfers == before);
  CompleteInterrupt();
  UsbHid_ServiceMouse(0); AssertWindowsReport(true); CompleteInterrupt();
  UsbHid_ServiceMouse(0);
  assert(captured_report[0] == 1 && captured_report[1] == 0); /* No Back click. */
  CompleteInterrupt();
  UsbHid_ServiceMouse(0); AssertWindowsReport(false); CompleteInterrupt();
  UsbHid_ServiceMouse(0);
  assert(captured_report[0] == 1 && captured_report[1] == 0);
  CompleteInterrupt();

  /* Holding the switch does not repeatedly press the key. Other buttons work. */
  UsbHid_ServiceMouse(MOUSE_BUTTON_SIDE1); AssertWindowsReport(true); CompleteInterrupt();
  UsbHid_ServiceMouse(MOUSE_BUTTON_SIDE1); CompleteInterrupt();
  before = interrupt_transfers;
  for (unsigned i = 0; i < 5; ++i) UsbHid_ServiceMouse(MOUSE_BUTTON_SIDE1);
  assert(interrupt_transfers == before);
  UsbHid_ServiceMouse(MOUSE_BUTTON_SIDE1 | MOUSE_BUTTON_SIDE2);
  assert(captured_report[0] == 1 && captured_report[1] == MOUSE_BUTTON_SIDE2);
  CompleteInterrupt();
  UsbHid_ServiceMouse(0); AssertWindowsReport(false); CompleteInterrupt();
  UsbHid_ServiceMouse(0); CompleteInterrupt();

  /* Releasing while suspended must not leave Windows held after resume. */
  UsbHid_ServiceMouse(MOUSE_BUTTON_SIDE1); AssertWindowsReport(true); CompleteInterrupt();
  UsbHid_ServiceMouse(MOUSE_BUTTON_SIDE1); CompleteInterrupt();
  USBD_LL_Suspend(&hUsbDeviceFS); UsbHid_ServiceMouse(0);
  USBD_LL_Resume(&hUsbDeviceFS);
  UsbHid_ServiceMouse(0); AssertWindowsReport(false); CompleteInterrupt();
  UsbHid_ServiceMouse(0); CompleteInterrupt();
  puts("PASS: Side 1 Windows press/hold/release, busy quick taps, Back suppression, Side 2 and suspend release recovery.");
}

static void TestReconnect(void)
{
  UsbHid_AddMotion(60, -50, 4);
  uint32_t old_session = USBD_HID_GetSession();
  Enumerate(); assert(USBD_HID_GetSession() != old_session);
  SynchronizeMouse(MOUSE_BUTTON_MIDDLE);
  assert(captured_report[1] == MOUSE_BUTTON_MIDDLE && captured_report[2] == 0 && captured_report[4] == 0);
  USBD_LL_Suspend(&hUsbDeviceFS);
  assert(!UsbHid_IsConfigured() && !Send_Mouse_Action(0, 1, 1, 0));
  UsbHid_AddMotion(100, 100, 20); UsbHid_ServiceMouse(0);
  USBD_LL_Resume(&hUsbDeviceFS); SynchronizeMouse(0);
  assert(captured_report[1] == 0 && captured_report[2] == 0 && captured_report[3] == 0);
  USBD_LL_DevDisconnected(&hUsbDeviceFS);
  assert(!UsbHid_IsConfigured()); UsbHid_ServiceMouse(0);
  assert(test_primask == 0U);
  puts("PASS: reset/re-enumeration and suspend/resume resynchronize held buttons without replaying offline motion.");
}

static void PollLogin(uint8_t buttons, unsigned milliseconds)
{
  for (unsigned i = 0; i < milliseconds; ++i)
  {
    ++test_tick;
    unsigned before = interrupt_transfers;
    UsbHid_ServiceMouse(buttons);
    if (interrupt_transfers != before)
      CompleteInterrupt();
  }
}

static void TestLoginSequence(bool caps_lock)
{
  assert(!Login_OnFingerprintMatch(0)); /* USB not ready: require a fresh scan. */
  Enumerate(); SynchronizeMouse(0);
  if (caps_lock)
  {
    Request(0x21, 0x09, 0x0202, 0, 2);
    control_rx[0] = 2; control_rx[1] = 2;
    USBD_LL_DataOutStage(&hUsbDeviceFS, 0, control_rx);
  }
  assert(Login_GetState() == LOGIN_ARMED && !Login_OnFingerprintMatch(1));
  /* Windows is held before matching: login must release it and inhibit it. */
  SynchronizeMouse(MOUSE_BUTTON_SIDE1);
  keyboard_count = 0U;
  assert(Login_OnFingerprintMatch(0));
  assert(!Login_OnFingerprintMatch(0));
  UsbHid_AddMotion(20, -30, 2);
  PollLogin(MOUSE_BUTTON_SIDE1, 600);
  assert(Login_GetState() == LOGIN_SENT && !Login_OnFingerprintMatch(0)
         && !Login_OnFingerprintMatch(1));
  const uint8_t expected_keys[] = {0x1E, 0x1E, 0x16, 0x16, 0x1F, 0x28};
  unsigned characters = 0;
  bool released = true;
  for (unsigned i = 0; i < keyboard_count; ++i)
  {
    assert((keyboard_history[i][1] & 0x08U) == 0U);
    uint8_t key = keyboard_history[i][3];
    if (key != 0U)
    {
      assert(released && characters < sizeof(expected_keys));
      assert(key == expected_keys[characters]);
      uint8_t shift = characters == 2U ? 2U : 0U;
      if (caps_lock && (characters == 2U || characters == 3U)) shift ^= 2U;
      assert(keyboard_history[i][1] == shift);
      ++characters;
      released = false;
    }
    else
    {
      assert(keyboard_history[i][1] == 0U);
      released = true;
    }
  }
  assert(characters == sizeof(expected_keys) && released);
  unsigned before = keyboard_count;
  PollLogin(MOUSE_BUTTON_SIDE1, 100);
  assert(keyboard_count == before); /* A held side button cannot reopen Start. */
  PollLogin(0, 10); PollLogin(MOUSE_BUTTON_SIDE1, 10);
  assert(keyboard_history[keyboard_count - 1U][1] == 8U);
  PollLogin(0, 10);
  USBD_LL_DevDisconnected(&hUsbDeviceFS); UsbHid_ServiceMouse(0);
  Enumerate(); SynchronizeMouse(0);
  assert(Login_GetState() == LOGIN_SENT && !Login_OnFingerprintMatch(0));
  puts(caps_lock ? "PASS: login letter case honors host Caps Lock LEDs."
                 : "PASS: ID-bound one-shot login, repeated digits, mixed case, Enter/release, Windows-key arbitration and reconnect stays spent.");
}

static void TestLoginAbort(const char *scenario)
{
  Enumerate(); SynchronizeMouse(0);
  assert(Login_OnFingerprintMatch(0));
  PollLogin(0, 17); /* Prefix release and first character down. */
  assert(Login_GetState() == LOGIN_RELEASE_KEY);
  keyboard_count = 0U;
  if (strcmp(scenario, "reset") == 0)
    Enumerate();
  else if (strcmp(scenario, "suspend") == 0)
  {
    USBD_LL_Suspend(&hUsbDeviceFS); UsbHid_ServiceMouse(0);
    USBD_LL_Resume(&hUsbDeviceFS);
  }
  else
  {
    transmit_result = USBD_BUSY;
    test_tick += 10001U;
    UsbHid_ServiceMouse(0);
    assert(Login_GetState() == LOGIN_ABORT_RELEASE);
    transmit_result = USBD_OK;
  }
  PollLogin(0, 100);
  assert(Login_GetState() == LOGIN_ABORTED && !Login_OnFingerprintMatch(0));
  for (unsigned i = 0; i < keyboard_count; ++i)
    for (unsigned byte = 1; byte < 9; ++byte)
      assert(keyboard_history[i][byte] == 0U);
  printf("PASS: interrupted login (%s) releases keys, never replays text and requires MCU reset.\n", scenario);
}

int main(int argc, char **argv)
{
  assert(USBD_Init(&hUsbDeviceFS, &FS_Desc, 0) == USBD_OK);
  assert(USBD_RegisterClass(&hUsbDeviceFS, &USBD_HID) == USBD_OK);
  assert(!Send_Mouse_Action(0, 0, 0, 0));
  if (argc > 1)
  {
    if (strcmp(argv[1], "disabled") == 0)
    {
      Enumerate(); SynchronizeMouse(0);
      assert(Login_GetState() == LOGIN_DISABLED && !Login_OnFingerprintMatch(0));
      puts("PASS: build without local credentials disables login.");
    }
    else if (strcmp(argv[1], "caps") == 0)
      TestLoginSequence(true);
    else
      TestLoginAbort(argv[1]);
    return 0;
  }
  Enumerate();
  TestDescriptors(); TestButtons(); TestMouseTransport();
  TestKeyboardAndControl(); TestSide1WindowsKey(); TestReconnect();
  TestLoginSequence(false);
  return 0;
}
