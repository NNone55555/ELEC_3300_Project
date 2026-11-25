# Fingerprint-Based Personalized Mouse

## Wireless mouse and keyboard

Build and flash both projects from the same checkout. Install a 3.3 V nRF24L01+
module in each board's NRF header and connect the receiver's native USB port
to the PC. Power the mouse board without enumerating its native USB port to
use wireless mode. A configured native USB connection on the mouse board
takes priority and keeps its wired mode available.

The receiver LCD shows `nRF HID: linked`, the active fingerprint ID, and packet
and duplicate counts. A duplicate is normal when the first radio ACK was sent
before the receiver finished processing its report. Movement, the wheel, and the five mouse buttons use HID
report ID 1. The keyboard uses report ID 2. Side-button shortcuts and the
profile's one-time login typing produce the same press and release reports as
wired mode. The receiver sends its Caps Lock state back to the mouse.

The shared 12-byte format uses little-endian integers, with an 8-bit sequence
and a 16-bit link epoch in every packet.

| Bytes | Field |
| --- | --- |
| 0 | Packet kind and version marker |
| 1 | Sequence number |
| 2–3 | Link epoch |
| 4–11 | Mouse: signed 16-bit X and Y, signed 8-bit wheel, buttons, 16-bit user ID |
| 4–11 | Keyboard: modifiers, six HID usages, one reserved byte |
| 4–11 | Sync/ACK: link setup or receiver status, keyboard LEDs, and acknowledged kind |

The mouse retries the same report until the receiver acknowledges it after
submitting it to USB. Both boards share the 1 Mbit/s radio configuration
and use nRF24L01+ ACK payloads for receiver feedback. A retry cannot apply the same movement, shortcut, or
keystroke twice. On link loss or USB reconfiguration, the receiver clears
mouse buttons and keyboard keys before accepting a new session. The mouse
discards interrupted login typing when either transport changes. The radio
link carries raw HID input without encryption, so use synthetic credentials.
