/* HID class adapted from ST's STM32CubeF1 V1.8.7 usbd_hid.c.
 * Copyright (c) 2015 STMicroelectronics. All rights reserved.
 * Licensed under ST SLA0044; see third_party/st_usb/Package_license.md.
 * Changes: mouse + keyboard report IDs, five buttons, persistent TX storage,
 * explicit BUSY/error results, per-report idle handling and keyboard LED OUT.
 */
#include "usbd_hid.h"
#include "usbd_ctlreq.h"

#define HID_DESCRIPTOR_TYPE 0x21U
#define HID_REPORT_DESCRIPTOR_TYPE 0x22U
#define HID_GET_REPORT 0x01U
#define HID_GET_IDLE 0x02U
#define HID_SET_REPORT 0x09U
#define HID_SET_IDLE 0x0AU

/* One non-boot HID interface, with two top-level application collections.
 * Report 1: ID, five buttons + padding, signed relative X/Y/wheel.
 * Report 2: ID, eight modifiers, reserved byte, six keyboard usages.
 * Report 2 output: ID, keyboard LED bits + padding, sent through EP0.
 */
static uint8_t report_descriptor[] = {
  0x05,0x01, 0x09,0x02, 0xA1,0x01, 0x85,0x01,
  0x09,0x01, 0xA1,0x00,
  0x05,0x09, 0x19,0x01, 0x29,0x05,
  0x15,0x00, 0x25,0x01, 0x75,0x01, 0x95,0x05, 0x81,0x02,
  0x75,0x03, 0x95,0x01, 0x81,0x03,
  0x05,0x01, 0x09,0x30, 0x09,0x31, 0x09,0x38,
  0x15,0x81, 0x25,0x7F, 0x75,0x08, 0x95,0x03, 0x81,0x06,
  0xC0, 0xC0,
  0x05,0x01, 0x09,0x06, 0xA1,0x01, 0x85,0x02,
  0x05,0x07, 0x19,0xE0, 0x29,0xE7,
  0x15,0x00, 0x25,0x01, 0x75,0x01, 0x95,0x08, 0x81,0x02,
  0x75,0x08, 0x95,0x01, 0x81,0x03,
  0x05,0x08, 0x19,0x01, 0x29,0x05,
  0x75,0x01, 0x95,0x05, 0x91,0x02,
  0x75,0x03, 0x95,0x01, 0x91,0x03,
  0x05,0x07, 0x19,0x00, 0x29,0x65,
  0x15,0x00, 0x25,0x65, 0x75,0x08, 0x95,0x06, 0x81,0x00,
  0xC0
};

static uint8_t hid_descriptor[] = {
  9, HID_DESCRIPTOR_TYPE, 0x11,0x01, 0, 1,
  HID_REPORT_DESCRIPTOR_TYPE,
  LOBYTE(sizeof(report_descriptor)), HIBYTE(sizeof(report_descriptor))
};

static uint8_t configuration_descriptor[] = {
  9, USB_DESC_TYPE_CONFIGURATION, 34,0, 1,1,0, 0xC0,50,
  /* No boot subclass/protocol: reports include report IDs. */
  9, USB_DESC_TYPE_INTERFACE, 0,0,1, 0x03,0,0, USBD_IDX_INTERFACE_STR,
  9, HID_DESCRIPTOR_TYPE, 0x11,0x01,0,1,HID_REPORT_DESCRIPTOR_TYPE,
  LOBYTE(sizeof(report_descriptor)), HIBYTE(sizeof(report_descriptor)),
  7, USB_DESC_TYPE_ENDPOINT, HID_EPIN_ADDR,0x03,
  LOBYTE(HID_EPIN_SIZE), HIBYTE(HID_EPIN_SIZE), HID_FS_BINTERVAL
};

typedef struct {
  volatile uint8_t busy;
  uint8_t tx[HID_EPIN_SIZE];
  uint8_t mouse[HID_MOUSE_REPORT_SIZE];
  uint8_t keyboard[HID_KEYBOARD_REPORT_SIZE];
  uint8_t idle[3];
  uint16_t elapsed[3];
  uint8_t led_output[2];
  uint8_t led_pending;
  uint8_t alternate;
  uint16_t status;
} HidContext;

static HidContext hid;
static volatile uint32_t session;

static uint8_t Init(USBD_HandleTypeDef *device, uint8_t configuration)
{
  (void)configuration;
  memset(&hid, 0, sizeof(hid));
  hid.mouse[0] = HID_MOUSE_REPORT_ID;
  hid.keyboard[0] = HID_KEYBOARD_REPORT_ID;
  hid.led_output[0] = HID_KEYBOARD_REPORT_ID;
  if (USBD_LL_OpenEP(device, HID_EPIN_ADDR, USBD_EP_TYPE_INTR, HID_EPIN_SIZE) != USBD_OK)
    return USBD_FAIL;
  device->ep_in[HID_EPIN_ADDR & 0x0FU].is_used = 1U;
  device->pClassData = &hid;
  ++session;
  return USBD_OK;
}

static uint8_t DeInit(USBD_HandleTypeDef *device, uint8_t configuration)
{
  (void)configuration;
  (void)USBD_LL_CloseEP(device, HID_EPIN_ADDR);
  device->ep_in[HID_EPIN_ADDR & 0x0FU].is_used = 0U;
  device->pClassData = NULL;
  hid.busy = 0U;
  return USBD_OK;
}

static uint8_t Reject(USBD_HandleTypeDef *device, USBD_SetupReqTypedef *request)
{
  USBD_CtlError(device, request);
  return USBD_FAIL;
}

static uint8_t Setup(USBD_HandleTypeDef *device, USBD_SetupReqTypedef *request)
{
  if (request->wIndex != 0U)
    return Reject(device, request);
  hid.led_pending = 0U;

  if ((request->bmRequest & USB_REQ_TYPE_MASK) == USB_REQ_TYPE_STANDARD)
  {
    switch (request->bRequest)
    {
      case USB_REQ_GET_DESCRIPTOR:
        if (request->bmRequest != 0x81U)
          break;
        if (request->wValue == (HID_REPORT_DESCRIPTOR_TYPE << 8))
          return USBD_CtlSendData(device, report_descriptor,
                                  MIN(request->wLength, sizeof(report_descriptor)));
        if (request->wValue == (HID_DESCRIPTOR_TYPE << 8))
          return USBD_CtlSendData(device, hid_descriptor,
                                  MIN(request->wLength, sizeof(hid_descriptor)));
        break;

      case USB_REQ_GET_STATUS:
        if (request->bmRequest == 0x81U && request->wValue == 0U
            && device->dev_state == USBD_STATE_CONFIGURED && request->wLength == 2U)
          return USBD_CtlSendData(device, (uint8_t *)&hid.status, 2U);
        break;

      case USB_REQ_GET_INTERFACE:
        if (request->bmRequest == 0x81U && request->wValue == 0U
            && device->dev_state == USBD_STATE_CONFIGURED && request->wLength == 1U)
          return USBD_CtlSendData(device, &hid.alternate, 1U);
        break;

      case USB_REQ_SET_INTERFACE:
        if (request->bmRequest == 0x01U && device->dev_state == USBD_STATE_CONFIGURED
            && request->wValue == 0U && request->wLength == 0U)
          return USBD_OK;
        break;
    }
    return Reject(device, request);
  }

  if ((request->bmRequest & USB_REQ_TYPE_MASK) != USB_REQ_TYPE_CLASS
      || device->dev_state != USBD_STATE_CONFIGURED)
    return Reject(device, request);

  uint8_t id = LOBYTE(request->wValue);
  switch (request->bRequest)
  {
    case HID_SET_IDLE:
      if (request->bmRequest == 0x21U && request->wLength == 0U && id <= HID_KEYBOARD_REPORT_ID)
      {
        for (uint8_t report = 1U; report <= 2U; ++report)
          if (id == 0U || id == report)
          {
            hid.idle[report] = HIBYTE(request->wValue);
            hid.elapsed[report] = 0U;
          }
        return USBD_OK;
      }
      break;

    case HID_GET_IDLE:
      if (request->bmRequest == 0xA1U && HIBYTE(request->wValue) == 0U
          && id >= 1U && id <= 2U && request->wLength == 1U)
        return USBD_CtlSendData(device, &hid.idle[id], 1U);
      break;

    case HID_GET_REPORT:
      if (request->bmRequest != 0xA1U || request->wLength == 0U)
        break;
      if (HIBYTE(request->wValue) == 1U && id == HID_MOUSE_REPORT_ID)
        return USBD_CtlSendData(device, hid.mouse, MIN(request->wLength, sizeof(hid.mouse)));
      if (HIBYTE(request->wValue) == 1U && id == HID_KEYBOARD_REPORT_ID)
        return USBD_CtlSendData(device, hid.keyboard, MIN(request->wLength, sizeof(hid.keyboard)));
      if (HIBYTE(request->wValue) == 2U && id == HID_KEYBOARD_REPORT_ID)
        return USBD_CtlSendData(device, hid.led_output, MIN(request->wLength, sizeof(hid.led_output)));
      break;

    case HID_SET_REPORT:
      if (request->bmRequest == 0x21U && HIBYTE(request->wValue) == 2U && id == HID_KEYBOARD_REPORT_ID
          && request->wLength == sizeof(hid.led_output))
      {
        hid.led_pending = 1U;
        return USBD_CtlPrepareRx(device, hid.led_output, sizeof(hid.led_output));
      }
      break;
  }
  /* A report-ID interface does not advertise BIOS boot protocol support. */
  return Reject(device, request);
}

uint8_t USBD_HID_SendReport(USBD_HandleTypeDef *device, const uint8_t *report,
                            uint16_t size)
{
  if (report == NULL || !((size == HID_MOUSE_REPORT_SIZE && report[0] == 1U)
                         || (size == HID_KEYBOARD_REPORT_SIZE && report[0] == 2U)))
    return USBD_FAIL;

  /* Task and USB SOF callers share the endpoint; copy before exposing BUSY.
   * USB IRQ cannot reset the class or complete a transfer during this section.
   */
  uint32_t interrupt_state = __get_PRIMASK();
  __disable_irq();
  uint8_t result = USBD_FAIL;
  if (device->dev_state == USBD_STATE_CONFIGURED && device->pClassData == &hid)
  {
    result = USBD_BUSY;
    if (hid.busy == 0U)
    {
      memcpy(hid.tx, report, size);
      hid.busy = 1U;
      result = USBD_LL_Transmit(device, HID_EPIN_ADDR, hid.tx, size);
      if (result == USBD_OK)
      {
        uint8_t id = report[0];
        if (id == HID_MOUSE_REPORT_ID)
        {
          if (report != hid.mouse)
            memcpy(hid.mouse, report, size);
          /* Idle/GET_REPORT must not replay relative movement. */
          memset(&hid.mouse[2], 0, 3);
        }
        else if (report != hid.keyboard)
          memcpy(hid.keyboard, report, size);
        hid.elapsed[id] = 0U;
      }
      else
        hid.busy = 0U;
    }
  }
  __set_PRIMASK(interrupt_state);
  return result;
}

static uint8_t DataIn(USBD_HandleTypeDef *device, uint8_t endpoint)
{
  (void)device;
  if (endpoint == (HID_EPIN_ADDR & 0x7FU))
    hid.busy = 0U;
  return USBD_OK;
}

static uint8_t Ep0RxReady(USBD_HandleTypeDef *device)
{
  (void)device;
  if (hid.led_pending != 0U)
  {
    hid.led_pending = 0U;
    /* Accept Num/Caps/Scroll/Compose/Kana state without producing keystrokes. */
    if (hid.led_output[0] != HID_KEYBOARD_REPORT_ID)
    {
      hid.led_output[0] = HID_KEYBOARD_REPORT_ID;
      hid.led_output[1] = 0U;
      return USBD_FAIL;
    }
    hid.led_output[1] &= 0x1FU;
  }
  return USBD_OK;
}

static uint8_t Sof(USBD_HandleTypeDef *device)
{
  for (uint8_t id = 1U; id <= 2U; ++id)
  {
    if (hid.idle[id] == 0U)
      continue;
    uint16_t deadline = 4U * hid.idle[id];
    if (hid.elapsed[id] < deadline)
      ++hid.elapsed[id];
    if (hid.elapsed[id] >= deadline)
    {
      if (id == HID_MOUSE_REPORT_ID)
        (void)USBD_HID_SendReport(device, hid.mouse, sizeof(hid.mouse));
      else
        (void)USBD_HID_SendReport(device, hid.keyboard, sizeof(hid.keyboard));
    }
  }
  return USBD_OK;
}

static uint8_t *ConfigurationDescriptor(uint16_t *size)
{
  *size = sizeof(configuration_descriptor);
  return configuration_descriptor;
}

USBD_ClassTypeDef USBD_HID = {
  .Init = Init,
  .DeInit = DeInit,
  .Setup = Setup,
  .EP0_RxReady = Ep0RxReady,
  .DataIn = DataIn,
  .SOF = Sof,
  .GetFSConfigDescriptor = ConfigurationDescriptor
};

uint32_t USBD_HID_GetSession(void)
{
  return session;
}

uint8_t USBD_HID_GetKeyboardLeds(void)
{
  return hid.led_output[1];
}
