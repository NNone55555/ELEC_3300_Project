# Use the generated ST USB core with the mouse's own class and board support.
if(NOT TARGET USB_Device_Library)
    message(FATAL_ERROR "Enable USB_DEVICE/HID in ELEC3300.ioc before generating the mouse project")
endif()

get_target_property(app_sources ${CMAKE_PROJECT_NAME} SOURCES)
list(FILTER app_sources EXCLUDE REGEX "(^|/)USB_DEVICE/(App|Target)/")
set_property(TARGET ${CMAKE_PROJECT_NAME} PROPERTY SOURCES "${app_sources}")

get_target_property(usb_sources USB_Device_Library SOURCES)
list(FILTER usb_sources EXCLUDE REGEX "/Class/HID/Src/usbd_hid[.]c$")
set_property(TARGET USB_Device_Library PROPERTY SOURCES "${usb_sources}")

# Remove the stock headers from the include path as well as the stock sources.
get_target_property(cube_include_dirs stm32cubemx INTERFACE_INCLUDE_DIRECTORIES)
list(FILTER cube_include_dirs EXCLUDE REGEX "/USB_DEVICE/(App|Target)$|/Class/HID/Inc$")
set_property(TARGET stm32cubemx PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${cube_include_dirs}")
target_include_directories(stm32cubemx INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/usb")

target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    usb/usb_device.c
    usb/usbd_desc.c
    usb/usbd_hid.c
    usb/usbd_conf.c
)
