# Mouse firmware

The current firmware combines PMW3360 movement, the EC10E1220501 wheel, five
buttons and ZW3020 fingerprint recognition. It exposes a USB mouse and keyboard
to the PC. Side button 1 tests the Windows key. A match of fingerprint ID 0 can
type a locally configured password + Enter once per MCU boot; see
[fingerprint login and TOUCHOUT wiring](LOGIN.md). EEPROM profiles, configurable
keyboard macros and nRF communication come later.

## Flash and use

1. Build the mouse Debug target and flash with the existing VS Code ST-Link
   configuration. From `mouse/`, CLI builds use `cmake --preset Debug`, then
   `cmake --build --preset Debug`. If the debugger stops at `main`, press
   Continue/F5. The checked-in code is ready to build without regenerating it.
2. Connect a data cable to **J12, the native USB mini connector**. J11/CH340
   is the serial monitor connector, not the HID port. Keep the board and
   sensors powered as in the previous tests.
3. The LCD should show `USB: mouse + keyboard ready`. Test cursor movement,
   left/right click, dragging, wheel click, scrolling and both side buttons.
   **Side 1 (PC12) is the Windows key**: press and release it to open/close
   Start. It sends Left GUI down while held and releases it when lifted;
   its mouse Back action is suppressed. Side 2 remains HID button 5 (Forward).
4. LCD button values use **1 = pressed, 0 = released**. `Raw pins` shows the
   actual high/low levels as a hex mask in the same button order below.
   All released = `1F`; all pressed = `00`.
5. Scan a stored finger to match. PA0 starts enrollment, cancels enrollment,
   or retries a fingerprint connection error. Follow the two-scan LCD prompts.
   Fingerprint operations do not block mouse input or require a match to move.

| Button | GPIO | HID/raw-mask bit |
| --- | --- | --- |
| Left | PC10 | 0 |
| Right | PC11 | 1 |
| Wheel click | PE5 | 2 |
| Side 1 | PC12 | 3 |
| Side 2 | PD2 | 4 |

The `.ioc` and generated GPIO code configure these as inputs with pull-ups:
wire each switch between its pin and GND. The schematic shows PC8/PC9 and
PC10/PC11/PC12/PD2 also going to microSD, so leave that slot empty. PE5 also
goes to the IR connector; disconnect any IR receiver using that pin.

Native USB uses PA11 D-, PA12 D+, and **PD6 low to enable the board's external
D+ pull-up**. CPU clocks stay at 72 MHz; USB uses PLL / 1.5 = 48 MHz. These
settings are also recorded in `ELEC3300.ioc`. The hardware reference is
`MINI V3 Schematic 20210304.pdf`, pages 1, 3 and 4.

## Code and FreeRTOS

| File | Responsibility |
| --- | --- |
| `Core/Src/mouse_buttons.c` | Five independent 5 ms debouncers and raw readings |
| `Core/Src/encoder.c` | Wheel quadrature decoder, one count per detent |
| `Core/Src/pmw3360.c` | SPI2, SROM initialization and motion bursts; default 800 CPI |
| `Core/Src/zw3020.c`, `fingerprint.c` | UART protocol and enrollment/matching FSM |
| `Core/Src/login_typing.c` | Once-per-boot, ID-bound password/Enter keyboard FSM |
| `Core/Src/usb_hid_controller.c` | Report wrappers, accumulated movement, queued button transitions |
| `USB_DEVICE/App/usbd_hid.c` | Mouse/keyboard descriptor and USB HID class |
| `USB_DEVICE/Target/usbd_conf.c` | ST USB core to F103 peripheral bridge |
| `Core/Src/mouse_app.c` | Task coordination and the sole LCD/serial writer |

| Task | Priority | Stack | Period/work |
| --- | --- | --- | --- |
| `wiredMouse` | High | 1536 bytes | 1 ms: buttons, wheel, PA0 events, USB reports |
| `motion` | Above normal | 1536 bytes | 5 ms: optical movement; retry failed initialization every 2 s |
| `fingerprint` | Normal | 3072 bytes | FSM step about every 100 ms, or on PA0 |
| `defaultTask` | Below normal | 2048 bytes | LCD about every 100 ms; serial once per second |

TIM4 samples the encoder and buttons every 1 ms. USART2 reception is interrupt
buffered. Shared display snapshots use a mutex; sensor/USB transfers happen
outside that mutex. The heap remains 16 KB. USART1 logs at **115200 8N1**,
including each task's minimum unused stack space and button queue overflows.
The LCD shows free heap; check these margins during combined hardware tests.

USB report 1 is a five-button mouse (`ID, buttons, int8 X, int8 Y, int8 wheel`).
Report 2 is a six-key keyboard (`ID, modifiers, reserved, keys[6]`). Movement
larger than one report is retained and sent in chunks. A 32-entry queue keeps
press/release order while the endpoint is busy. Prolonged host stalls can fill
it; the overflow count increments and the latest button state is retained.
USB reset/suspend discards pending motion to avoid cursor jumps on reconnect.

Side 1 tests keyboard HID using the Left GUI modifier (bit 3, `0x08`). Its
press/release reports share the input queue, so a quick tap while USB is busy
is preserved. During fingerprint login it is inhibited until released.
Keyboard state is synchronized again after reset/suspend.
`Send_Keyboard_Action()` returns false if busy/disconnected; retry the report,
and send a zero-key release after a press. These are report-protocol interfaces
for an operating system, not BIOS boot keyboard/mouse interfaces.

The displayed user ID is the last successful match since boot. Templates persist
in the fingerprint module; movement/scroll totals and the displayed user reset
with the MCU. No EEPROM profile is loaded yet.

## Validation and receiver review

`receiver/` contains `usb_hid_controller.c/.h` wrappers with report IDs 1 and 2,
but its included `usb_device.h`, `usbd_hid.h`, USB descriptors and middleware
are absent from this checkout. Its wrappers alone do not establish working
mouse/keyboard HID. The wrappers were adapted here, adding the missing stack,
matching descriptors, checked busy/error results and persistent TX storage.
The receiver files were left unchanged.

Run `powershell -ExecutionPolicy Bypass -File mouse/tests/run_host_tests.ps1`
from the repository root with a native GCC installed. It tests the real ST USB
core and HID class against simulated peripheral callbacks: control requests,
descriptor layouts, keyboard press/release/LEDs, busy retries, movement splitting,
button debounce, queued releases and reset/suspend handling. It does not prove
electrical enumeration or cursor behavior on the board.

After flashing, repeat cursor/drag/scroll/button tests while enrolling and
matching, and unplug/reconnect USB. Check that clicks release and the cursor
does not jump on reconnection. Optical and wheel orientation may need reversing
to suit the sensor/encoder's physical mounting.

The HID class is customized. CubeMX's stock HID class has a different report
descriptor. After regeneration, review `USB_DEVICE/` against Git and keep the
custom class and `cmake/usb_device.cmake`; avoid linking a second stock HID class.
Third-party provenance and licenses are under `third_party/`.
