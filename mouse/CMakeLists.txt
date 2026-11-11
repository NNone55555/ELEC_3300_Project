cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME ELEC3300)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user sources here
    Core/Src/lcd.c
    Core/Src/encoder.c
    Core/Src/pmw3360.c
    Core/Src/zw3020.c
    Core/Src/fingerprint.c
    Core/Src/mouse_app.c
    Core/Src/mouse_buttons.c
    Core/Src/usb_hid_controller.c
    Core/Src/login_typing.c
    Core/Src/ws2812b.c
    Core/Src/ws2812b_task.c
    Core/Src/user_profile.c
    Core/Src/at24c02.c
    Core/Src/profile_storage.c
    Core/Src/console_uart.c
    Core/Src/fingerprint_service.c
    Core/Src/profile_manager.c
    Core/Src/config_menu.c
)

option(MOUSE_WS2812B_DEMO "Run the independent WS2812B color/animation demonstration" OFF)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    MOUSE_WS2812B_DEMO=$<BOOL:${MOUSE_WS2812B_DEMO}>
)

# Passwords are configured through UART and stored in prototype EEPROM records.
# The old private/login_credentials.h is preserved locally but is not compiled.

# Native STM32F103 USB, kept separate from CubeMX's generated CMake list.
include(cmake/usb_device.cmake)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
    Core/Inc
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)
