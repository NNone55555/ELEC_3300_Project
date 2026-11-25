# Checked-in ST USB core + application HID class. No Cube installation required.
if(TARGET USB_Device_Library)
    get_target_property(usb_sources USB_Device_Library SOURCES)
    list(FILTER usb_sources EXCLUDE REGEX "/Class/HID/Src/usbd_hid[.]c$")
    set_property(TARGET USB_Device_Library PROPERTY SOURCES "${usb_sources}")
    target_sources(USB_Device_Library PRIVATE USB_DEVICE/App/usbd_hid.c)
    return()
endif()

target_include_directories(stm32cubemx INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/USB_DEVICE/App
    ${CMAKE_CURRENT_SOURCE_DIR}/USB_DEVICE/Target
    ${CMAKE_CURRENT_SOURCE_DIR}/Middlewares/ST/STM32_USB_Device_Library/Core/Inc
)
target_sources(STM32_Drivers PRIVATE
    Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_pcd.c
    Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_pcd_ex.c
    Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_ll_usb.c
)
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_core.c
    Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_ctlreq.c
    Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_ioreq.c
    USB_DEVICE/App/usb_device.c
    USB_DEVICE/App/usbd_desc.c
    USB_DEVICE/App/usbd_hid.c
    USB_DEVICE/Target/usbd_conf.c
)
