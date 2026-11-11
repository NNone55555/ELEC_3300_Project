# ST USB device support

The USB core in `Middlewares/ST/STM32_USB_Device_Library/Core` is copied unchanged
from the installed STM32CubeF1 **V1.8.7** package (USB Device Library **V2.5.3**).
The PCD/PCDEx/LL USB files added to `Drivers/STM32F1xx_HAL_Driver` come from the
same package and retain their original copyright headers.

`USB_DEVICE/App/usbd_hid.c`, `usbd_desc.c` and `USB_DEVICE/Target/usbd_conf.c`
adapt ST's HID class/STM3210E_EVAL HID example. Changes include MINI V3 PD6
connection control, no STOP mode or ISR delays, a report-ID mouse/keyboard
descriptor, five buttons, persistent report storage, explicit busy/error status,
per-report idle handling and keyboard LED control reports.

The USB library/example uses **ST SLA0044**, and the HAL uses **BSD-3-Clause**.
Full terms supplied with the package are retained in `Package_license.md`;
the HAL license is also at `Drivers/STM32F1xx_HAL_Driver/LICENSE.txt`.

Primary upstream references:
- https://github.com/STMicroelectronics/STM32CubeF1/tree/v1.8.7
- https://github.com/STMicroelectronics/stm32-mw-usb-device
- https://www.st.com/resource/en/user_manual/um1734-stm32cube-usb-device-library-stmicroelectronics.pdf

Application report wrapper layouts/IDs originate from this repository's
`receiver/Core/Inc/usb_hid_controller.h`; no receiver firmware files were changed.
