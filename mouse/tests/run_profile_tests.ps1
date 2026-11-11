$ErrorActionPreference = 'Stop'
$mouseRoot = Split-Path -Parent $PSScriptRoot
Push-Location $mouseRoot
try {
    New-Item -ItemType Directory -Force build/host-tests | Out-Null
    & gcc -std=c11 -Wall -Wextra -Werror -I tests/support -I Core/Inc `
        tests/profile_config_test.c Core/Src/user_profile.c Core/Src/at24c02.c `
        Core/Src/profile_storage.c Core/Src/console_uart.c Core/Src/config_menu.c `
        Core/Src/profile_manager.c -o build/host-tests/profile_config_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Profile test compilation failed' }
    & ./build/host-tests/profile_config_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Profile tests failed' }
    & gcc -std=c11 -Wall -Wextra -Werror -I tests/support -I Core/Inc `
        tests/fingerprint_service_test.c Core/Src/fingerprint.c Core/Src/zw3020.c `
        Core/Src/fingerprint_service.c -o build/host-tests/fingerprint_service_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Fingerprint test compilation failed' }
    & ./build/host-tests/fingerprint_service_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Fingerprint service tests failed' }
    & gcc -std=c11 -Wall -Wextra -Werror -I tests/optical_support -I Core/Inc `
        tests/optical_sensor_test.c -o build/host-tests/optical_sensor_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Optical test compilation failed' }
    & ./build/host-tests/optical_sensor_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Optical sensor tests failed' }
}
finally { Pop-Location }
