# Requires a native GCC (for example MSYS2), not arm-none-eabi-gcc.
$ErrorActionPreference = 'Stop'
$mouseRoot = Split-Path -Parent $PSScriptRoot
Push-Location $mouseRoot
try {
    New-Item -ItemType Directory -Force build/host-tests | Out-Null
    $testSources = @(
        'tests/wired_input_test.c', 'Core/Src/mouse_buttons.c',
        'Core/Src/usb_hid_controller.c', 'USB_DEVICE/App/usbd_hid.c',
        'Core/Src/login_typing.c',
        'USB_DEVICE/App/usbd_desc.c',
        'Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_core.c',
        'Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_ctlreq.c',
        'Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_ioreq.c'
    )
    # The unmodified ST control-request source has an unused request parameter.
    & gcc -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter -DMOUSE_HAS_LOGIN_CREDENTIALS=1 -I tests/support -I Core/Inc `
        -I USB_DEVICE/App -I USB_DEVICE/Target `
        -I Middlewares/ST/STM32_USB_Device_Library/Core/Inc `
        @testSources -o build/host-tests/wired_input_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Host test compilation failed' }
    & ./build/host-tests/wired_input_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Host tests failed' }
    foreach ($scenario in @('reset', 'suspend', 'timeout', 'caps')) {
        & ./build/host-tests/wired_input_test.exe $scenario
        if ($LASTEXITCODE -ne 0) { throw "Login test failed: $scenario" }
    }
    & gcc -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter -I tests/support -I Core/Inc `
        -I USB_DEVICE/App -I USB_DEVICE/Target `
        -I Middlewares/ST/STM32_USB_Device_Library/Core/Inc `
        @testSources -o build/host-tests/no_login_config_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'No-credential test compilation failed' }
    & ./build/host-tests/no_login_config_test.exe disabled
    if ($LASTEXITCODE -ne 0) { throw 'No-credential test failed' }
}
finally { Pop-Location }
