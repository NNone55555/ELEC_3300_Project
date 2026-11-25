/* PCD/USBD bridge adapted from ST's STM3210E_EVAL HID example, CubeF1 V1.8.7.
 * Copyright (c) 2015 STMicroelectronics. All rights reserved.
 * ST SLA0044; see third_party/st_usb/Package_license.md.
 * MINI V3 uses PD6 for connection. No STOP mode or blocking USB ISR delays.
 */
#include "usb_device.h"
#include "main.h"
#include "login_typing.h"

PCD_HandleTypeDef hpcd_USB_FS;

static USBD_StatusTypeDef UsbStatus(HAL_StatusTypeDef status)
{
  return status == HAL_OK ? USBD_OK : status == HAL_BUSY ? USBD_BUSY : USBD_FAIL;
}

void HAL_PCD_MspInit(PCD_HandleTypeDef *pcd)
{
  if (pcd->Instance != USB)
    return;
  __HAL_RCC_GPIOA_CLK_ENABLE();
  GPIO_InitTypeDef pins = {0};
  pins.Pin = GPIO_PIN_11 | GPIO_PIN_12;
  pins.Mode = GPIO_MODE_AF_INPUT;
  pins.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &pins);
  __HAL_RCC_USB_CLK_ENABLE();
  HAL_NVIC_SetPriority(USB_LP_CAN1_RX0_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(USB_LP_CAN1_RX0_IRQn);
}

void HAL_PCD_MspDeInit(PCD_HandleTypeDef *pcd)
{
  if (pcd->Instance != USB)
    return;
  HAL_NVIC_DisableIRQ(USB_LP_CAN1_RX0_IRQn);
  __HAL_RCC_USB_CLK_DISABLE();
  HAL_GPIO_DeInit(GPIOA, GPIO_PIN_11 | GPIO_PIN_12);
}

void HAL_PCD_SetupStageCallback(PCD_HandleTypeDef *pcd)
{
  USBD_LL_SetupStage(pcd->pData, (uint8_t *)pcd->Setup);
}

void HAL_PCD_DataOutStageCallback(PCD_HandleTypeDef *pcd, uint8_t endpoint)
{
  USBD_LL_DataOutStage(pcd->pData, endpoint, pcd->OUT_ep[endpoint].xfer_buff);
}

void HAL_PCD_DataInStageCallback(PCD_HandleTypeDef *pcd, uint8_t endpoint)
{
  USBD_LL_DataInStage(pcd->pData, endpoint, pcd->IN_ep[endpoint].xfer_buff);
}

void HAL_PCD_SOFCallback(PCD_HandleTypeDef *pcd)
{
  USBD_LL_SOF(pcd->pData);
}

void HAL_PCD_ResetCallback(PCD_HandleTypeDef *pcd)
{
  Login_OnUsbUnavailable();
  USBD_LL_SetSpeed(pcd->pData, USBD_SPEED_FULL);
  USBD_LL_Reset(pcd->pData);
}

void HAL_PCD_SuspendCallback(PCD_HandleTypeDef *pcd)
{
  Login_OnUsbUnavailable();
  USBD_LL_Suspend(pcd->pData);
}

void HAL_PCD_ResumeCallback(PCD_HandleTypeDef *pcd)
{
  USBD_LL_Resume(pcd->pData);
}

void HAL_PCD_ISOOUTIncompleteCallback(PCD_HandleTypeDef *pcd, uint8_t endpoint)
{
  USBD_LL_IsoOUTIncomplete(pcd->pData, endpoint);
}

void HAL_PCD_ISOINIncompleteCallback(PCD_HandleTypeDef *pcd, uint8_t endpoint)
{
  USBD_LL_IsoINIncomplete(pcd->pData, endpoint);
}

void HAL_PCD_ConnectCallback(PCD_HandleTypeDef *pcd)
{
  USBD_LL_DevConnected(pcd->pData);
}

void HAL_PCD_DisconnectCallback(PCD_HandleTypeDef *pcd)
{
  Login_OnUsbUnavailable();
  USBD_LL_DevDisconnected(pcd->pData);
}

void HAL_PCDEx_SetConnectionState(PCD_HandleTypeDef *pcd, uint8_t connected)
{
  (void)pcd;
  HAL_GPIO_WritePin(USB_ENABLE_GPIO_Port, USB_ENABLE_Pin,
                    connected ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

USBD_StatusTypeDef USBD_LL_Init(USBD_HandleTypeDef *device)
{
  hpcd_USB_FS.Instance = USB;
  hpcd_USB_FS.Init.dev_endpoints = 8;
  hpcd_USB_FS.Init.speed = PCD_SPEED_FULL;
  hpcd_USB_FS.Init.phy_itface = PCD_PHY_EMBEDDED;
  hpcd_USB_FS.Init.low_power_enable = DISABLE;
  hpcd_USB_FS.Init.Sof_enable = ENABLE;
  hpcd_USB_FS.pData = device;
  device->pData = &hpcd_USB_FS;
  if (HAL_PCD_Init(&hpcd_USB_FS) != HAL_OK)
  {
    Error_Handler(); /* This ST USB core does not propagate LL init failures. */
    return USBD_FAIL;
  }
  /* Buffer table 0x00..0x3F; EP0 RX/TX 64 bytes each; EP1 IN 16 bytes.
   * All regions are disjoint and inside the F103's 512-byte USB PMA.
   */
  if (HAL_PCDEx_PMAConfig(&hpcd_USB_FS, 0x00, PCD_SNG_BUF, 0x40) != HAL_OK
      || HAL_PCDEx_PMAConfig(&hpcd_USB_FS, 0x80, PCD_SNG_BUF, 0x80) != HAL_OK
      || HAL_PCDEx_PMAConfig(&hpcd_USB_FS, 0x81, PCD_SNG_BUF, 0xC0) != HAL_OK)
  {
    Error_Handler();
    return USBD_FAIL;
  }
  return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_DeInit(USBD_HandleTypeDef *device)
{ return UsbStatus(HAL_PCD_DeInit(device->pData)); }

USBD_StatusTypeDef USBD_LL_Start(USBD_HandleTypeDef *device)
{
  HAL_StatusTypeDef result = HAL_PCD_Start(device->pData);
  if (result != HAL_OK)
    Error_Handler();
  return UsbStatus(result);
}

USBD_StatusTypeDef USBD_LL_Stop(USBD_HandleTypeDef *device)
{ return UsbStatus(HAL_PCD_Stop(device->pData)); }

USBD_StatusTypeDef USBD_LL_OpenEP(USBD_HandleTypeDef *device, uint8_t address,
                                 uint8_t type, uint16_t size)
{ return UsbStatus(HAL_PCD_EP_Open(device->pData, address, size, type)); }

USBD_StatusTypeDef USBD_LL_CloseEP(USBD_HandleTypeDef *device, uint8_t address)
{ return UsbStatus(HAL_PCD_EP_Close(device->pData, address)); }

USBD_StatusTypeDef USBD_LL_FlushEP(USBD_HandleTypeDef *device, uint8_t address)
{ return UsbStatus(HAL_PCD_EP_Flush(device->pData, address)); }

USBD_StatusTypeDef USBD_LL_StallEP(USBD_HandleTypeDef *device, uint8_t address)
{ return UsbStatus(HAL_PCD_EP_SetStall(device->pData, address)); }

USBD_StatusTypeDef USBD_LL_ClearStallEP(USBD_HandleTypeDef *device, uint8_t address)
{ return UsbStatus(HAL_PCD_EP_ClrStall(device->pData, address)); }

uint8_t USBD_LL_IsStallEP(USBD_HandleTypeDef *device, uint8_t address)
{
  PCD_HandleTypeDef *pcd = device->pData;
  return (address & 0x80U) ? pcd->IN_ep[address & 0x7FU].is_stall
                           : pcd->OUT_ep[address & 0x7FU].is_stall;
}

USBD_StatusTypeDef USBD_LL_SetUSBAddress(USBD_HandleTypeDef *device, uint8_t address)
{ return UsbStatus(HAL_PCD_SetAddress(device->pData, address)); }

USBD_StatusTypeDef USBD_LL_Transmit(USBD_HandleTypeDef *device, uint8_t address,
                                   uint8_t *buffer, uint16_t size)
{ return UsbStatus(HAL_PCD_EP_Transmit(device->pData, address, buffer, size)); }

USBD_StatusTypeDef USBD_LL_PrepareReceive(USBD_HandleTypeDef *device, uint8_t address,
                                        uint8_t *buffer, uint16_t size)
{ return UsbStatus(HAL_PCD_EP_Receive(device->pData, address, buffer, size)); }

uint32_t USBD_LL_GetRxDataSize(USBD_HandleTypeDef *device, uint8_t address)
{ return HAL_PCD_EP_GetRxCount(device->pData, address); }

void USBD_LL_Delay(uint32_t milliseconds)
{ HAL_Delay(milliseconds); }
