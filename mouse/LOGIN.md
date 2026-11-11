# Fingerprint keyboard login prototype

Only a **fresh successful match of template ID 0** can start the configured
password + Enter sequence. Other IDs and touch signals cannot start it.
Mouse buttons, optical movement and scrolling continue during the sequence.

## TOUCHOUT wiring

- Connect module **TOUCHOUT / INT -> PE6**, available at the DHT/DS18B20 data
  connector **U16 pin 2**. Keep the module and MCU on common GND and the tested
  3.3 V supplies. Follow connector labels, not wire colours.
- Leave any DHT11/DS18B20 on that connector disconnected. The MINI V3 schematic
  (page 4) shows a 4.7 kOhm pull-up from PE6 to 3.3 V. Firmware configures PE6
  as an input with no additional internal pull resistor.
- TOUCHOUT is optional. UART polling still handles enrollment/matching with it
  disconnected. The LCD displays **raw** `FP touch=0/1`; an unconnected PE6
  reads high because of the board pull-up. It is never proof of a match.
- The reference library describes touch/wake output as high on detection, but
  the exact ZW3020 INT electrical/polarity specification has not been confirmed
  from a module-specific datasheet. Use only a verified 0..3.3 V output on PE6;
  this diagnostic input does not depend on assuming the output polarity.

References: [board schematic](MINI%20V3%20Schematic%2020210304.pdf),
[reference library touch wiring](https://github.com/GavinnnTann/HLK-ZW-Fingerprint-Sensor#circuit).

## Login behaviour

1. Build/flash and continue execution. The LCD shows `Login: armed` when a
   local credential is configured. USB must show ready before matching; if it
   was not ready, lift and scan again once it is ready.
2. Open the PC's **password** sign-in field (choose Password rather than PIN),
   make it empty and focus it. Use an English/US keyboard layout. Letter case
   accounts for Caps Lock when Windows supplies its keyboard LED state.
3. Scan the finger stored as **ID 0**. Firmware releases keyboard modifiers,
   types the configured text with a release after every character, then sends
   Enter down/up. The LCD shows `Login: sent; reset to rearm`.
4. Rescanning, PA0 fingerprint retries and USB reconnects **do not rearm login**.
   Only an MCU reset or mouse power cycle rearms it. An interrupted transfer or
   a 10 s timeout also consumes the attempt; firmware releases keys on recovery
   and does not resume/replay partial text.

This sends keystrokes, not Windows Hello authentication. Firmware cannot tell
whether the PC is locked, whether the password field has focus, or whether
Windows accepted it. Once per boot limits repeats; the first match after a
reset can still type into another focused application. The Windows-key side
button is inhibited during login and until released, avoiding GUI shortcuts.

## Local configuration and code

The real credential is in **`private/login_credentials.h`**, excluded by the
root `.gitignore`. It is not printed or stored in tracked source. A fresh clone
builds with login disabled: copy `config/login_credentials.example.h` into
`private/login_credentials.h`, set its ID/password locally, and reconfigure
CMake. The password remains recoverable from firmware/MCU flash in this
prototype; Git exclusion is not encryption.

`Core/Src/login_typing.c` owns the one-shot keyboard FSM. The fingerprint task
notifies it only on a new successful database search. The USB task paces the
reports without blocking; mouse reports use spare endpoint opportunities.
Per-user credential lookup belongs to the later EEPROM/profile stage. Until
then, every ID except the configured ID is ignored for login.

Host tests use **dummy credentials only** and cover repeats, mixed case,
Caps Lock, Enter/release, GUI-button arbitration, interruptions and builds
without local credentials. Actual unlock still needs a board/PC test.
