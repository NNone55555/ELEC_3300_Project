# CubeMX provides the USB core and hardware bridge. Replace only its stock
# mouse-only class; application headers are already first in the generated paths.
if(NOT TARGET USB_Device_Library)
    message(FATAL_ERROR "Enable USB_DEVICE/HID in receiver.ioc before building the receiver")
endif()
get_target_property(receiver_usb_sources USB_Device_Library SOURCES)
list(FILTER receiver_usb_sources EXCLUDE REGEX "/Class/HID/Src/usbd_hid\\.c$")
set_property(TARGET USB_Device_Library PROPERTY SOURCES "${receiver_usb_sources}")
target_sources(USB_Device_Library PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/USB_DEVICE/App/usbd_hid.c"
)
