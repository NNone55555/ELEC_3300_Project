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
#include "user_profile.h"

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

static void Pump(uint8_t buttons, unsigned count)
{
  for (unsigned i=0; i<count; ++i) {
    unsigned before=interrupt_transfers;
    UsbHid_ServiceMouse(buttons);
    if (before != interrupt_transfers) CompleteInterrupt();
  }
}

static void TestSideMacros(void)
{
  for (unsigned macro=0; macro<MACRO_COUNT; ++macro) {
    UsbHid_SetSideMacros(macro,MACRO_FORWARD); Pump(0,6);
    keyboard_count=0;
    Pump(MOUSE_BUTTON_SIDE1,10);
    if (macro>=MACRO_COPY && macro<=MACRO_SELECT_ALL) {
      static const uint8_t usages[]={6,0x19,0x1D,4};
      assert(keyboard_count==2 && keyboard_history[0][1]==1);
      assert(keyboard_history[0][3]==usages[macro-MACRO_COPY]);
      for(unsigned k=1;k<9;++k)assert(keyboard_history[1][k]==0);
    } else {
      assert(keyboard_count==0 && captured_report[0]==1);
      assert(captured_report[1]==(macro==MACRO_BACK?8:macro==MACRO_FORWARD?16:0));
    }
    unsigned before=keyboard_count; Pump(MOUSE_BUTTON_SIDE1,10);assert(keyboard_count==before);
    Pump(0,5);assert(captured_report[0]==1 && captured_report[1]==0);
  }
  UsbHid_SetSideMacros(MACRO_COPY,MACRO_PASTE);Pump(0,6);keyboard_count=0;
  assert(Send_Mouse_Action(0,0,0,0));
  UsbHid_ServiceMouse(MOUSE_BUTTON_SIDE1|MOUSE_BUTTON_SIDE2);UsbHid_ServiceMouse(0);
  assert(keyboard_count==0);CompleteInterrupt();Pump(0,12);
  assert(keyboard_count==4 && keyboard_history[0][3]==6 && keyboard_history[2][3]==0x19);
  for(unsigned k=1;k<9;++k)assert(keyboard_history[1][k]==0 && keyboard_history[3][k]==0);
  Pump(MOUSE_BUTTON_SIDE1,6);unsigned before=keyboard_count;
  UsbHid_SetSideMacros(MACRO_UNDO,MACRO_DISABLED);Pump(MOUSE_BUTTON_SIDE1,10);
  assert(keyboard_count==before+1); /* neutral sync only; no held-button macro */
  Pump(0,5);Pump(MOUSE_BUTTON_SIDE1,6);assert(keyboard_history[keyboard_count-2][3]==0x1D);
  USBD_LL_Suspend(&hUsbDeviceFS);UsbHid_ServiceMouse(0);USBD_LL_Resume(&hUsbDeviceFS);Pump(0,6);
  for(unsigned k=1;k<9;++k)assert(keyboard_history[keyboard_count-1][k]==0);
  puts("PASS: all seven side mappings, both simultaneous shortcuts, busy quick taps, press/release, hold suppression, remap and suspend release.");
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
  assert(!Login_RequestPassword("Alpha123")); /* USB not ready. */
  Enumerate(); SynchronizeMouse(0);
  if (caps_lock) {
    Request(0x21,0x09,0x0202,0,2);control_rx[0]=2;control_rx[1]=2;
    USBD_LL_DataOutStage(&hUsbDeviceFS,0,control_rx);
  }
  assert(Login_GetState()==LOGIN_ARMED && !Login_RequestPassword("") && !Login_RequestPassword("invalid!"));
  UsbHid_SetSideMacros(MACRO_COPY,MACRO_PASTE);SynchronizeMouse(0);
  Pump(MOUSE_BUTTON_SIDE1,6);keyboard_count=0;
  char caller_password[] = "Alpha123";
  assert(Login_RequestPassword(caller_password));assert(!Login_RequestPassword("Alpha123"));
  memset(caller_password, 'X', sizeof(caller_password) - 1U); /* Private TX copy must survive. */
  UsbHid_AddMotion(20,-30,2);PollLogin(MOUSE_BUTTON_SIDE1,600);
  assert(Login_GetState()==LOGIN_SENT);
  const uint8_t expected[]={4,0x0F,0x13,0x0B,4,0x1E,0x1F,0x20,0x28};
  unsigned characters=0;bool released=true;
  for(unsigned i=0;i<keyboard_count;++i) {
    uint8_t key=keyboard_history[i][3];
    if(key) {
      assert(released && characters<sizeof(expected) && key==expected[characters]);
      bool alpha=characters<5;
      bool shifted=alpha && ((characters==0) != caps_lock);
      assert(keyboard_history[i][1]==(shifted?2:0));
      ++characters;released=false;
    } else {
      for(unsigned k=1;k<9;++k)assert(keyboard_history[i][k]==0);
      released=true;
    }
  }
  assert(characters==sizeof(expected) && released);
  unsigned before=keyboard_count;PollLogin(MOUSE_BUTTON_SIDE1,100);assert(keyboard_count==before);
  PollLogin(0,10);PollLogin(MOUSE_BUTTON_SIDE1,10);
  assert(keyboard_count==before+2 && keyboard_history[before][1]==1 && keyboard_history[before][3]==6);
  PollLogin(0,10);
  USBD_LL_DevDisconnected(&hUsbDeviceFS);UsbHid_ServiceMouse(0);Enumerate();SynchronizeMouse(0);
  assert(Login_GetState()==LOGIN_SENT);
  puts(caps_lock?"PASS: password typing honors host Caps Lock LEDs.":
    "PASS: password + Enter/release, busy exclusion, keyboard macro arbitration, held-side suppression and no reconnect replay.");
}

static void TestLoginAbort(const char *scenario)
{
  Enumerate(); SynchronizeMouse(0);
  assert(Login_RequestPassword("Alpha123"));
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
  assert(Login_GetState() == LOGIN_ABORTED);
  for (unsigned i = 0; i < keyboard_count; ++i)
    for (unsigned byte = 1; byte < 9; ++byte)
      assert(keyboard_history[i][byte] == 0U);
  printf("PASS: interrupted login (%s) releases keys, never replays text and waits for a new profile transition.\n", scenario);
}
static void TestPasswordBoundaries(void)
{
  Enumerate(); SynchronizeMouse(0);
  assert(!Login_RequestPassword(NULL) && !Login_RequestPassword(""));
  assert(!Login_RequestPassword("Symbols!") && !Login_RequestPassword("Space Here"));
  assert(!Login_RequestPassword("Abcdefgh012345678XYZ")); /* 20 */
  const char *const values[] = {"0", "1", "Az09", "Abcdefgh01234567XYZ"};
  for (unsigned test = 0; test < sizeof(values)/sizeof(values[0]); ++test)
  {
    unsigned start = keyboard_count;
    assert(Login_RequestPassword(values[test])); PollLogin(0, 1000);
    assert(Login_GetState() == LOGIN_SENT);
    unsigned pressed = 0; bool released = true;
    for (unsigned i = start; i < keyboard_count; ++i)
    {
      if (keyboard_history[i][3] != 0)
      {
        assert(released); released = false; ++pressed;
        if (test == 0 && pressed == 1) assert(keyboard_history[i][3] == 0x27);
        if (test == 1 && pressed == 1) assert(keyboard_history[i][3] == 0x1E);
        if (pressed == strlen(values[test]) + 1U) assert(keyboard_history[i][3] == 0x28);
      }
      else
      {
        for (unsigned byte = 1; byte < 9; ++byte) assert(keyboard_history[i][byte] == 0);
        released = true;
      }
    }
    assert(released && pressed == strlen(values[test]) + 1U);
  }
  puts("PASS: custom 1/19-character passwords, literal 0/1, invalid character/length rejection and Enter/key releases.");
}

int main(int argc, char **argv)
{
  assert(USBD_Init(&hUsbDeviceFS, &FS_Desc, 0) == USBD_OK);
  assert(USBD_RegisterClass(&hUsbDeviceFS, &USBD_HID) == USBD_OK);
  assert(!Send_Mouse_Action(0, 0, 0, 0));
  if (argc > 1)
  {
    if (strcmp(argv[1], "passwords") == 0)
      TestPasswordBoundaries();
    else if (strcmp(argv[1], "caps") == 0)
      TestLoginSequence(true);
    else
      TestLoginAbort(argv[1]);
    return 0;
  }
  Enumerate();
  TestDescriptors(); TestButtons(); TestMouseTransport();
  TestKeyboardAndControl(); TestSideMacros(); TestReconnect();
  TestLoginSequence(false);
  return 0;
}
