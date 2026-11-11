#include "usb_device.h"
#include "usbd_hid.h"
#include "main.h"

extern USBD_DescriptorsTypeDef FS_Desc;
USBD_HandleTypeDef hUsbDeviceFS;

void MX_USB_DEVICE_Init(void)
{
  /* J12 D+ pull-up is switched by the board's PNP transistor, active low. */
  HAL_GPIO_WritePin(USB_ENABLE_GPIO_Port, USB_ENABLE_Pin, GPIO_PIN_SET);
  HAL_Delay(20);
  if (HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_USB) != 48000000U
      || USBD_Init(&hUsbDeviceFS, &FS_Desc, 0) != USBD_OK
      || USBD_RegisterClass(&hUsbDeviceFS, &USBD_HID) != USBD_OK
      || USBD_Start(&hUsbDeviceFS) != USBD_OK)
  {
    Error_Handler();
  }
}
