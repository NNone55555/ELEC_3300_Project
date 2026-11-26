# Fingerprint-Based Personalized Mouse

## Provision and flash

Run `py tools/provision_keys.py` from the repository root before the first
build. It generates separate AES-128 keys for the EEPROM and radio and keeps
existing keys on subsequent runs. Both builds must use the same
`shared/private/radio_key.h`. Only the mouse uses
`mouse/private/profile_key.h`. Keep these ignored files and the resulting
firmware images private. The keys reside in on-chip Flash.

Use page erase when updating firmware. Pages 254 and 255
(`0x0807F000–0x0807FFFF`) on each board store the nonce journal. If the
journal is erased or a board is replaced, provision fresh keys before reuse.
`py tools/provision_keys.py --rotate` replaces both keys. Rebuild and flash
both boards after rotation. Clear the mouse's EEPROM profile area
(bytes 0–223) if the EEPROM key changes and the old profiles can be discarded.
Give each mouse/receiver pair its own radio key.

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

The shared 12-byte application packet still uses little-endian integers,
with an 8-bit sequence and a 16-bit link epoch.

| Bytes | Field |
| --- | --- |
| 0 | Packet kind and version marker |
| 1 | Sequence number |
| 2–3 | Link epoch |
| 4–11 | Mouse: signed 16-bit X and Y, signed 8-bit wheel, buttons, 16-bit user ID |
| 4–11 | Keyboard: modifiers, six HID usages, one reserved byte |
| 4–11 | Sync/ACK: link setup or receiver status, keyboard LEDs, and acknowledged kind |

Every radio payload is 32 bytes: a 12-byte nonce, 12 bytes of AES-128-CCM
ciphertext, and an 8-byte tag. The nonce contains the sender's Flash boot
counter, the message type, and a per-boot packet counter. An authenticated
challenge binds input and feedback to a fresh session, including after a
receiver reboot. The receiver checks the tag and counter before acting on HID
reports. The mouse retries under a fresh nonce with the same application
sequence until the receiver acknowledges submitting the report to USB.

Both boards use 250 kbit/s and nRF24L01+ ACK payloads for receiver feedback.
The receiver's `RF loss` counter tracks timeouts in authenticated traffic,
`USB loss` tracks USB session changes while linked, and `Reject` counts
frames that fail session, replay, or authentication checks. On link loss
or USB reconfiguration, the receiver clears mouse buttons and keyboard keys
before accepting a new session. The mouse discards interrupted login typing
when either transport changes.

The mouse LCD defaults to off. Uncomment `#define LCD_FOR_DEBUG` in
`mouse/Core/Inc/main.h` and rebuild to show the pressed state and effective
assignments for `S1` and `S2`. With one saved profile, the mouse loads its
side-button assignments at boot. With multiple profiles, scan the intended
user's fingerprint to select their mappings. Saving a verified profile applies
its settings immediately if no user is active. Login typing requires a normal
fingerprint match.

## EEPROM profile format

Format 3 holds two users and one copy-on-write spare in three 72-byte slots
starting at 0, 72, and 144. Bytes 216–223 track the one-time plaintext profile
wipe and mark the new format initialized. Bytes 224–255 are preserved.
Existing format 1 and 2 profiles are cleared on first load. Fingerprint
templates remain on the sensor and can be linked during registration.

| Byte offset | Field |
| --- | --- |
| 0–7 | `EP` signature, version, state, 32-bit sequence |
| 8 | Fingerprint ID |
| 9–10 | DPI |
| 11–25 | Computer username and display name, up to 15 characters |
| 26 | Lighting mode (low nibble), speed (high nibble) |
| 27–29 | RGB |
| 30 | Brightness |
| 31 | Side button 1 macro (low nibble), side button 2 macro (high nibble) |
| 32–51 | AES-128-CCM ciphertext for the zero-padded password, up to 19 characters |
| 52–63 | Nonce containing the Flash boot counter and a per-boot write counter |
| 64–71 | Authentication tag |

Bytes 0–31 are stored in clear and authenticated as associated data. Use the
existing UART configuration menu to enroll and set a hidden alphanumeric test
password. EEPROM keys are separate from the radio key. Both use the vendored
[BearSSL AES-CCM implementation](shared/third_party/bearssl/README.md).

The mouse's CubeMX default task uses 640 words for EEPROM and configuration.
The input task uses 2 KiB for USB and the radio. This gives both tasks room
for the AES-CCM call stack while retaining the 16 KiB FreeRTOS heap.
