$ErrorActionPreference = 'Stop'
$mouseRoot = Split-Path -Parent $PSScriptRoot
Push-Location $mouseRoot
try {
    New-Item -ItemType Directory -Force build/host-tests | Out-Null
    foreach ($ledCount in @(8, 1, 256)) {
        & gcc -std=c11 -Wall -Wextra -Werror "-DWS2812B_LED_COUNT=$ledCount" `
            -I tests/ws2812b_support -I Core/Inc `
            tests/ws2812b_test.c Core/Src/ws2812b.c -o build/host-tests/ws2812b_test.exe
        if ($LASTEXITCODE -ne 0) { throw 'WS2812B test compilation failed' }
        & ./build/host-tests/ws2812b_test.exe
        if ($LASTEXITCODE -ne 0) { throw "WS2812B tests failed: $ledCount LEDs" }
    }
    foreach ($demo in @(1, 0)) {
        & gcc -std=c11 -Wall -Wextra -Werror "-DMOUSE_WS2812B_DEMO=$demo" `
            -I tests/ws2812b_support -I Core/Inc `
            tests/ws2812b_task_test.c Core/Src/ws2812b_task.c -o build/host-tests/ws2812b_task_test.exe
        if ($LASTEXITCODE -ne 0) { throw 'LED task test compilation failed' }
        & ./build/host-tests/ws2812b_task_test.exe
        if ($LASTEXITCODE -ne 0) { throw 'LED task tests failed' }
    }
}
finally { Pop-Location }
