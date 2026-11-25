/* USB device descriptor layout follows ST's CubeF1 HID example.
 * STMicroelectronics, copyright (c) 2015; ST SLA0044.
 * See third_party/st_usb/Package_license.md.
 */
#include "usbd_core.h"
#include "usbd_ctlreq.h"

/* ST example VID/PID retained for this STM32 classroom prototype. */
static uint8_t device_descriptor[] = {
  18, USB_DESC_TYPE_DEVICE, 0x00,0x02, 0,0,0, USB_MAX_EP0_SIZE,
  0x83,0x04, 0x10,0x57, 0x00,0x03,
  USBD_IDX_MFC_STR, USBD_IDX_PRODUCT_STR, USBD_IDX_SERIAL_STR, 1
};
static uint8_t language_descriptor[] = {4, USB_DESC_TYPE_STRING, 0x09,0x04};
static uint8_t string_descriptor[USBD_MAX_STR_DESC_SIZ];
static uint8_t serial_descriptor[50] = {50, USB_DESC_TYPE_STRING};

static uint8_t *Device(USBD_SpeedTypeDef speed, uint16_t *size)
{
  (void)speed;
  *size = sizeof(device_descriptor);
  return device_descriptor;
}

static uint8_t *Language(USBD_SpeedTypeDef speed, uint16_t *size)
{
  (void)speed;
  *size = sizeof(language_descriptor);
  return language_descriptor;
}

static uint8_t *String(const char *text, uint16_t *size)
{
  USBD_GetString((uint8_t *)text, string_descriptor, size);
  return string_descriptor;
}

static uint8_t *Manufacturer(USBD_SpeedTypeDef speed, uint16_t *size)
{
  (void)speed;
  return String("ELEC3300", size);
}

static uint8_t *Product(USBD_SpeedTypeDef speed, uint16_t *size)
{
  (void)speed;
  return String("ELEC3300 Mouse + Keyboard", size);
}

static uint8_t *Serial(USBD_SpeedTypeDef speed, uint16_t *size)
{
  (void)speed;
  const uint32_t unique_id[3] = {HAL_GetUIDw0(), HAL_GetUIDw1(), HAL_GetUIDw2()};
  const char hex[] = "0123456789ABCDEF";
  for (uint8_t word = 0U; word < 3U; ++word)
    for (uint8_t digit = 0U; digit < 8U; ++digit)
      serial_descriptor[2U + 2U * (word * 8U + digit)] =
        hex[(unique_id[word] >> (28U - 4U * digit)) & 0x0FU];
  *size = sizeof(serial_descriptor);
  return serial_descriptor;
}

static uint8_t *Configuration(USBD_SpeedTypeDef speed, uint16_t *size)
{
  (void)speed;
  return String("Mouse and keyboard", size);
}

static uint8_t *Interface(USBD_SpeedTypeDef speed, uint16_t *size)
{
  (void)speed;
  return String("HID mouse and keyboard", size);
}

USBD_DescriptorsTypeDef FS_Desc = {
  Device, Language, Manufacturer, Product, Serial, Configuration, Interface
};
