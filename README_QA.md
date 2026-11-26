# ELEC3300 Final Demo and Technical Q&A

**Project:** Fingerprint-based personalized wireless mouse for shared workstations  
**Session:** 3-minute presentation, 5-minute live demo, 2 minutes for questions or recovery

## 1. What to show the markers

| Rubric criterion | Marks | Evidence to point to |
| --- | ---: | --- |
| Complexity | 12 | Trace a real event from a sensor through FreeRTOS, packet serialization, authenticated RF and USB HID. Show how the analogue battery input, timing-sensitive LEDs and persistent storage run alongside it. |
| Design justification | 10 | Explain why each peripheral fits its device and workload. Compare nRF24L01+ with stock HC-05 SPP, and explain the benefits and cost of the USB receiver. |
| Originality | 6 | The integration is the contribution. A verified fingerprint selects the physical sensor resolution, lighting and side-button behavior, with optional test credential entry through normal HID. |
| Completeness | 4 | Demonstrate ordinary input, two distinguishable profiles if rehearsed, persistence and predictable recovery. |
| Oral presentation | 8 | Fit one point per slide. Back claims with code, datasheets or an observable demo. Identify which results come from host tests. |

The criteria and weights come from the *ELEC3300 Project Grading Rubric* supplied for this demo. The marks describe the assessment, rather than a self-awarded score.

For a three-minute rehearsal, aim for roughly 14 seconds on the opening, 22 on motivation, 25 on architecture, 33 on interfaces, 27 on radio, 29 on security, 22 on results, and 8 to hand off to the demo.

## 2. System and evidence

Both boards use an STM32F103VET6. The mouse acquires inputs and applies local preferences. Its nRF24L01+ sends encrypted reports to a second nRF24L01+. The receiver STM32 translates validated reports into native USB HID mouse and keyboard input. Plugging the mouse directly into native USB gives it a wired path that takes priority once configured. The USB-to-UART configuration cable is a separate connection.

| Function | What the project supports | Evidence boundary |
| --- | --- | --- |
| Input and identification | Optical motion, wheel, five buttons and ZW3020 fingerprint matching | The user reported sensor tests working during development. Rehearse on the final assembled unit. |
| Profiles | Local registration, edit, verified deletion, recall, DPI, RGB and side macros | Earlier bench work confirmed EEPROM access. Host tests exercised encrypted save/reload, settings and both macro paths. |
| Wireless and USB | Motion, buttons, scroll, keyboard and optional test-password typing | The user reported RF send/receive working and later reported cycling connections. Host regressions exercised recovery, losses, release and deduplication. Final RF stability, range and end-to-end latency have no recorded bench measurement here. |
| Encryption and recovery | BearSSL CCM, separate keys, persistent boot epochs, freshness checks and EEPROM journaling | Host tests compared CCM with OpenSSL and exercised simulated write interruptions, replay and tamper rejection. Physical power-cut and security testing remain separate. |
| Lighting and ADC | Profile LED effects and a three-LED ADC indicator | The current build defaults to potentiometer demo mode. A pack-divider mode exists, with a linear estimate requiring practical calibration. No calibrated state-of-charge or runtime claim. |
| Login | Optional HID key presses after a normal profile transition | Use synthetic credentials and a focused text field or a rehearsed throwaway account. Native OS biometric integration is outside the prototype scope. |
| Debug | Mouse LCD selectable at build time, receiver LCD for link and user ID | The checked mouse source ships with the LCD flag commented. Receiver firmware maintains RF diagnostic counters, with the additional on-screen counter rows commented in the working tree. |

## 3. Pin and peripheral map

**Clocking.** Both cores are configured at 72 MHz with 48 MHz for native USB. On the mouse, APB1 is 36 MHz and APB2 is 72 MHz. SPI clocks below are the configured bus clocks. They are distinct from the RF air rate and USB speed.

| Device or role | Wiring and configuration in this checkout |
| --- | --- |
| nRF24L01+ / E01-ML01DP5, both boards | SPI1 PA5 SCK, PA6 MISO, PA7 MOSI, mode 0. PC5 CE, PC6 CSN, PC4 IRQ. The driver polls status and masks radio interrupt outputs. Mouse SPI1 1.125 MHz, receiver 4.5 MHz. Keep the on-board SPI Flash deselected with PC0 high. Use the board’s 3.3 V NRF header and verify the schematic against silkscreen. |
| PMW3360 optical sensor | SPI2 PB13 SCK, PB14 MISO, PB15 MOSI, PB12 NCS, mode 3, 562.5 kHz. PC7 MOTION is defined, while the motion task polls burst data. PA4 is the configured reset output held high. The driver initializes with the sensor’s software reset register. Check VDD and VDDIO against the actual breakout markings. |
| ZW3020 fingerprint module | USART2 PA2 TX and PA3 RX, 57,600 baud, 8N1. Cross TX/RX and share ground. PE6 TOUCHOUT is available for diagnostics. Match and removal decisions come from module image and search responses. |
| CH340 serial configuration | USART1 PA9 TX and PA10 RX, 115,200 baud, 8N1. In the serial terminal type `config` then Enter. |
| AT24C02 profile EEPROM | I²C1 PB6 SCL, PB7 SDA, 100 kHz. Address `0x50` in 7-bit notation, passed to HAL as `0xA0`. The board provides pull-ups. |
| Clicks and wheel | Left PC10, right PC11, middle PE5, side 1 PC12, side 2 PD2. Active-low switches with pull-ups. Encoder A PC8 and B PC9 with pull-ups. PC8–PC12 and PD2 share SD socket signals, so keep the socket empty with this wiring. On JP2, PC12 is pin 4, PD2 pin 2 and GND pin 10. |
| WS2812B | PA8 TIM1_CH1, DMA1 Channel 2. Disconnect the PA8 buzzer jumper so the pin drives only the intended serial LED signal. Strip power and board must share ground. |
| Battery / demo input | PC2 ADC1_IN12, single-ended with the nominal 3.3 V reference. Potentiometer connects its ends to 3.3 V and GND, with its wiper at PC2. Pack mode uses 100 kΩ above the sense node and 10 kΩ below it. |
| USB HID, both boards | PA11 D− and PA12 D+, native full-speed USB with PD6 board connection control. Use the intended native USB port for HID and the CH340 port for the terminal. |
| Debug LCD | FSMC 16-bit memory-mapped interface, A16 command/data, PE1 reset, active-low backlight on PD12. Mouse LCD and its FSMC initialization are compiled out with `LCD_FOR_DEBUG` commented. Receiver LCD remains available. |
| Timing and service | Mouse TIM4 supplies the 1 ms HAL timebase and input sampling. PA0 retries the fingerprint connection after a module error. The receiver runs a simple polling loop with `WFI`, waking from SysTick or USB interrupts. |

Sources: [mouse CubeMX project](mouse/ELEC3300.ioc), [mouse pin definitions](mouse/Core/Inc/main.h), [receiver CubeMX project](receiver/receiver.ioc), [receiver pin definitions](receiver/Core/Inc/main.h), board schematic `mouse/MINI V3 Schematic 20210304.pdf` sheets 1 and 3 and the JP2 connector sheet.

## 4. Implementation walkthrough

### Mouse task scheduling

| Context | Job and cadence |
| --- | --- |
| TIM4 callback | Samples the encoder, five buttons and PA0 at 1 ms. Keeps the ISR short. |
| `mouseInput`, high, 2 KiB stack | Samples snapshots, computes scroll delta, polls the nRF TX state, queues or sends HID, arbitrates keyboard ownership. Schedules at approximately 1 ms. |
| `motion`, above normal, 1.5 KiB | Reads the PMW burst about every 5 ms, applies CPI revisions and retries failed optical initialization at 2 s intervals. |
| `fingerprint`, normal, 3 KiB | Sole ZW3020 protocol owner. `FpService_Step` runs the recognition or enrollment state machine. The task waits for a PA0 flag or a 100 ms timeout before a step. |
| Default task, below normal, 2.5 KiB | Initializes profiles and menus, services the serial console, runs profile and storage state machines and updates the radio’s active user ID. Loops with a 10 ms delay. If enabled, the LCD refreshes around every 100 ms. |
| `ledStrip`, low, 1 KiB | Runs ADC and WS2812B update state machines every 5 ms. TIM1/DMA generates the wire timing independently of the task. |

The mouse uses CMSIS-RTOS v2 over FreeRTOS with a 16 KiB heap. Scheduled tasks separate input deadlines from module responses and EEPROM write-cycle waits. ISR handoff, short critical sections for shared snapshots and single-owner peripheral tasks avoid long shared bus locks. FreeRTOS helps manage priorities and waits. Worst-case response still depends on hardware transactions, retries and USB readiness. [Mouse task implementation](mouse/Core/Src/mouse_app.c) · [Startup](mouse/Core/Src/main.c) · [LED task](mouse/Core/Src/ws2812b_task.c)

### Motion, buttons and scroll

- **Optical initialization.** `Pmw3360_Init` checks product and inverse IDs `42/BD`, software-resets with `0x5A`, drains startup registers and uploads the bundled SROM. It verifies SROM ID, configures resolution and arms burst mode. The DWT cycle counter supplies the short sensor timing gaps.
- **Motion and DPI.** `Pmw3360_ReadMotion` takes a seven-byte burst using one chip-select assertion. It decodes little-endian signed X/Y deltas, reports quality and suppresses movement when lifted. The firmware calls the sensor setting CPI and labels it DPI in the menu. Values run from 100 to 12,000 in steps of 100. The register value is `CPI / 100 − 1`. `Pmw3360_SetCpi` verifies readback and re-arms burst mode. The profile changes sensor counts per inch, so host acceleration can still affect the on-screen result.
- **Buttons and wheel.** `MouseButtons_Sample1ms` requires five consecutive differing samples before changing a button state. `Encoder_Sample1ms` requires two stable phase samples, classifies legal Gray-code transitions using a 16-entry table, cancels reverse bounce and resynchronizes on two-bit jumps. It emits one step per two valid transitions for the EC10E1220501 wiring. The input task differences successive signed step totals. Motion accumulates in signed counters and is emitted in report-sized chunks. The button FIFO retains press/release transitions during short host stalls.

Sources: [PMW driver](mouse/Core/Src/pmw3360.c) · [buttons](mouse/Core/Src/mouse_buttons.c) · [encoder](mouse/Core/Src/encoder.c) · [input router](mouse/Core/Src/usb_hid_controller.c).

### Fingerprint and profile selection

1. `Zw3020_Connect` uses framed UART commands to check the module and read system parameters and template count. RX bytes are buffered through interrupts. The driver validates the `EF 01` header, address, packet type, length and checksum, with timeouts on communication.
2. Normal recognition gets an image (`0x01`), extracts to a character buffer (`0x02`) and searches (`0x04`). The module returns the matched ID and score. Image processing and template storage reside in the module. The STM32 looks up the ID in EEPROM and applies the verified record. A finger removal state prevents repeated matches from a held finger.
3. UART enrollment searches for duplicates and uses two compatible captures, extracts to separate buffers, then combines them (`0x05`). A new template stays in module RAM until save. The user can link an existing verified module ID if it has no profile. Deletion targets a verified ID and checks its slot with the index table.
4. `ProfileManager_Update` activates verified settings and passes the user ID to the radio, the CPI to the optical task, the lighting settings to the LED driver and mappings to the input router. At boot, a sole saved profile supplies side-button assignments while identity remains inactive. Multiple profiles require a match to select the intended settings. Verified setting previews can be rolled back. Saving the active profile, or a verified profile while no user is active, applies its settings.
5. A normal transition to a different active user with a stored test password may arm `Login_RequestPassword`. `Login_Service` sends neutral, press and release keyboard reports for each US-layout character and Enter. Receiver feedback provides Caps Lock state. A held finger or repeated same-user match does not retype the password within the same normal activation. A transport change aborts the sequence and releases keys. Configuration mode suppresses normal matches and cancels in-flight typing.

Sources: [ZW3020 protocol](mouse/Core/Src/zw3020.c) · [fingerprint state machine](mouse/Core/Src/fingerprint.c) · [fingerprint jobs](mouse/Core/Src/fingerprint_service.c) · [profiles](mouse/Core/Src/profile_manager.c) · [login state machine](mouse/Core/Src/login_typing.c).

### UART menu, EEPROM and power-loss recovery

- `Console_Service` drains interrupt-driven RX and queued TX. `ConfigMenu_Poll` consumes line-oriented choices. The menu supports verified edit, add/link, delete, storage retry and pending-operation recovery. Username/display name uses one field. Password entry is hidden and confirmed. The allowed password is empty or 1–19 ASCII alphanumeric characters. Numbered Keep options prevent stray CR/LF combinations from skipping registration prompts.
- `At24c02_WriteVerified` splits writes at 8-byte page boundaries, ACK-polls for write completion and reads back every chunk. It waits for STOP and idle instead of masking interrupts. With the LCD enabled, the EEPROM driver temporarily gates the FSMC clock around I²C operations to avoid the STM32F103 NADV/PB7 conflict. See [ST ES0340, §2.3.9](https://www.st.com/resource/en/errata_sheet/es0340-stm32f101xcde-stm32f103xcde-device-errata-stmicroelectronics.pdf).
- `ProfileStorage_Save` uses a spare 72-byte slot, writes an invalid state and new body, verifies the body and commits the intended state last. Reload authenticates the records and takes the newest valid sequence per ID. A failed write can leave the old copy available. Creating or deleting a template also uses explicit pending records to recover the separate module operation. A recognized migration wipes old plaintext slots and can resume after an interrupted wipe. Bytes 224–255 are reserved and preserved.

| EEPROM record offset | Bytes | Meaning |
| --- | ---: | --- |
| 0–7 | 8 | `EP`, format version 3, state, 32-bit sequence |
| 8–10 | 3 | One-byte fingerprint ID and 16-bit DPI |
| 11–25 | 15 | Zero-padded username/display name |
| 26–31 | 6 | Lighting mode/speed nibbles, RGB, brightness, macro nibbles |
| 32–51 | 20 | AES-CCM ciphertext for the zero-padded password |
| 52–63 | 12 | Flash boot epoch plus save-attempt counter |
| 64–71 | 8 | Authentication tag |

Slots start at 0, 72 and 144. Three 72-byte slots occupy 216 bytes. Two are user slots and one is reserved for copy-on-write updates. The next eight bytes hold the format/migration marker. Profile bytes 0–31 are authenticated associated data and readable in clear. No fingerprint image or template is stored in the EEPROM. [EEPROM I²C](mouse/Core/Src/at24c02.c) · [authenticated format](mouse/Core/Src/profile_storage.c) · [menu](mouse/Core/Src/config_menu.c) · [UART console](mouse/Core/Src/console_uart.c)

### Radio, acknowledgements and USB HID

`wireless_tx.c` builds an application packet. The receiver runs `WirelessRx_Service`, verifies each encrypted packet, checks its kind and sequence, delivers input to USB and queues encrypted feedback. The mouse sends one application report at a time, with idle mouse heartbeats every 25 ms and retries. Nordic hardware ACK means the radio heard a frame. The project’s authenticated ACK carries the last report accepted for USB and the keyboard LED state. The radio sends that piggyback payload on a subsequent uplink. Only the application ACK advances the report sequence. A software retry gets a fresh crypto counter but retains the report sequence. Repeated input is recognized without submitting another HID event. The link expires after 350 ms without qualifying traffic. The receiver clears buttons and keys and starts a fresh handshake before delivering new-session input.

| Packet | Fields and sizes |
| --- | --- |
| Common application header | 12-byte packet overall. Offset 0 kind/version, offset 1 sequence, offsets 2–3 little-endian link epoch. |
| Mouse `0xD2` | Offsets 4–7 signed 16-bit X/Y, 8 signed wheel, 9 five button bits, 10–11 16-bit user ID. |
| Keyboard `0xD3` | Offset 4 modifier bits, 5–10 six key usages, 11 reserved zero. |
| Sync `0xD1` and ACK `0xD4` | Sync uses sequence zero. ACK reports readiness flags, USB keyboard LEDs and the acknowledged kind in bytes 4–6. |
| RF frame | 32 bytes = 12-byte clear nonce header + 12-byte CCM ciphertext + 8-byte tag. HELLO and CHALLENGE use the protected 12-byte body for tickets. INPUT and FEEDBACK carry the application messages. |

`NrfMotion_Init` configures channel 76, one pipe with a five-byte address, two-byte radio CRC, dynamic payloads and ACK payloads. The selected air rate is **250 kbps**, with up to three hardware retries and a **1.75 ms** retransmit delay configured for a full 32-byte ACK. The receiver clamps USB mouse deltas to ±127 per report and drains the remainder in later reports. Its one non-boot HID interface exposes two top-level collections. Report ID 1 contains buttons, relative X/Y and wheel (5 bytes with ID). Report ID 2 contains modifiers, a reserved byte and six key codes (9 bytes with ID). LED output is handled over the control endpoint. The USB interrupt IN endpoint requests a 1 ms polling interval. This is a descriptor setting, rather than a measurement of click-to-host latency. [Radio driver](shared/nrf_motion.c) · [packet serialization](shared/wireless_packet.c) · [mouse transport](mouse/Core/Src/wireless_tx.c) · [receiver transport](receiver/Core/Src/wireless_rx.c) · [receiver HID descriptor](receiver/USB_DEVICE/App/usbd_hid.c) · [USB-IF HID 1.11](https://www.usb.org/sites/default/files/documents/hid1_11.pdf)

**Side macros.** Back and Forward map into the two extra mouse button bits. Ctrl+C, Ctrl+V, Ctrl+Z and Ctrl+A send a Left Ctrl modifier and a key usage, followed by an all-zero release. Disabled suppresses the button action. The handler keeps fresh edges during remapping, inhibits a held shortcut across transport/session changes and gives login typing temporary keyboard ownership. Test shortcuts in a text editor with known clipboard content. Browser navigation is an appropriate test for Back and Forward.

### AES and security boundary

1. Both domains use BearSSL AES-128-CCM with a 12-byte nonce and an 8-byte tag. Provision two independent random keys. The mouse and receiver share the radio key; the profile key is mouse-only. Local key headers are ignored by Git.
2. Key/nonce reuse is prevented during ordinary resets by a monotonic Flash boot epoch journal in the last two pages, `0x0807F000–0x0807FFFF`, and a per-boot counter. Radio nonce bytes 0–7 contain the boot epoch and two direction/message bits. Bytes 8–11 contain the encrypt counter. An authenticated HELLO/CHALLENGE binds a link to the current boots and tickets. The receiver checks authenticity, freshness and application sequence before acting.
3. EEPROM metadata is bound to the password ciphertext by CCM associated data. Tamper rejection improves integrity, but this is a prototype with keys compiled into Flash, a plaintext credential in RAM during handling and USB keystrokes delivered to the host. There is no production secure element, OS biometric integration or quantitative biometric false-match study. A debugger or compromised host is outside the protected boundary. RF interference can still cause denial of service. Backups or invasive access to the EEPROM and Flash need a separate threat assessment.
4. Keep the Flash journal during firmware updates. Use normal page erase with VS Code F5. A full-chip erase, journal rollback or board replacement requires fresh keys before reuse. Changing the EEPROM key also makes the old encrypted profiles unreadable. [CCM wrapper](shared/aes_ccm.c) · [radio authentication](shared/secure_radio.c) · [Flash journal](shared/boot_epoch.c) · [provisioning](README.md#provision-and-flash)

### WS2812B and battery indication

- **Lighting.** `WS2812B_Update` builds a GRB, MSB-first CCR buffer with one PWM period per data bit. TIM1’s 72 MHz clock and 90 ticks per bit give 800 kHz. The compare values are 26 ticks for a zero and 48 for a one (about 0.36 µs and 0.67 µs high). DMA updates CCR while FreeRTOS continues to handle input. The driver appends low reset slots and checks timer/DMA configuration, timeout and transfer completion. Profile effects are Off, Static, Breathing and Rainbow. The brightness setting is capped at 50% on output. Twenty LEDs are assigned to the profile, with every third position populated in the pattern from the first. The last three of the default 23 are independent battery indicators at 10%.
- **ADC.** `BatteryMonitor_Update` starts a single 12-bit conversion about once per second and polls it on the next LED-task cycle without waiting. At 12 MHz ADC clock, the 239.5-cycle sample setting plus conversion overhead is about 21 µs. With `BATTERY_POT_DEMO=1` the code maps the raw count 0–4095 to 0–100%. With pack mode selected, `Vpack ≈ raw × Vref / 4095 × 11`, using the 100 kΩ / 10 kΩ divider and nominal 3.3 V reference. Firmware maps 8.0–12.4 V to an approximate percentage for eight alkaline cells, clamped to the endpoints, and blanks invalid/absent readings. Fewer than 34% gives one bar, 34–66% two, and 67% or above three. Below 15%, the indicator is red. Alkaline discharge voltage depends on load, temperature and chemistry. The pack estimate and reference need calibration with the final supply and divider.

Sources: [LED driver](mouse/Core/Src/ws2812b.c) · [LED configuration](mouse/Core/Inc/ws2812b.h) · [battery ADC](mouse/Core/Src/battery_monitor.c) · [TIM/DMA/ADC setup](mouse/Core/Src/main.c).

## 5. Why these choices

| Choice | Engineering rationale and trade-off |
| --- | --- |
| SPI for PMW3360 | The sensor’s native interface supports burst reads with precise chip select and clock timing. Polling one burst gives co-timed X/Y and lift/quality status at a known service cadence. A dedicated SPI2 lets the sensor use mode 3 and its own timing without radio ownership conflicts. SPI needs extra pins and driver timing discipline. |
| SPI for nRF24L01+ | The nRF exposes registers and FIFOs over SPI. Full-frame FIFO transfers are natural on a synchronous bus. Independent SPI1 keeps radio polling and FIFO access separate from the optical sensor. CSN and CE have different roles. Check the SPI Flash CS before troubleshooting shared bus traffic. |
| UART for fingerprint and terminal | The fingerprint module already owns the image algorithm and exposes a command/response serial API. A point-to-point UART uses two signal wires and no shared clock. A second UART via CH340 keeps terminal prompts and debug traffic separate from the binary sensor protocol. UART is asynchronous and needs agreed baud/framing and responsive RX handling. |
| I²C for EEPROM | AT24C02 is already on the board. Two shared wires and addressable transactions suit infrequent profile edits and boot lookups. Volatile SRAM loses settings across power cycles. EEPROM has small pages, limited capacity and write cycles. Write-on-save and copy-on-write reduce churn and tolerate interruptions. |
| TIM1 PWM + DMA for WS2812B | WS2812B decodes pulse width on one data line. Timer PWM fixes each bit’s timing, and DMA feeds the sequence of duty values. GPIO bit-banging would tie timing to interrupt or instruction latency, or require blocking other tasks. DMA needs a buffer, timer channel and safe completion/reset handling. |
| ADC + divider | Batteries provide an analogue voltage. A divider scales it to the ADC range at low idle current, and an internal ADC avoids a separate monitor IC. Divider tolerance, ADC reference accuracy and voltage under load limit the estimate. The potentiometer safely exercises the conversion/display path on demand. |
| FreeRTOS on the mouse | Independent tasks prioritize motion and HID while UART recognition, EEPROM and lighting run at slower cadences. Blocking or slow operations stay with their owners. Task stacks and concurrency discipline cost RAM and effort. |
| Separate STM32 USB receiver | The receiver owns the PC connection, HID descriptors, keyboard LED feedback and input release logic. The mouse stays wireless and the OS enumerates a standard mouse and keyboard without a project-specific desktop app. It costs a second board, a USB port and matching firmware/keys. |

### nRF24L01+ versus HC-05 versus Bluetooth HID

| Dimension | Project nRF24L01+ pair | Stock HC-05 Bluetooth Classic SPP | Bluetooth HID-capable alternative |
| --- | --- | --- | --- |
| Host interface | Receiver enumerates as standard USB mouse and keyboard. | Exposes RFCOMM serial data via SPP. Direct pairing yields a serial connection. A PC app or a separate bridge must translate the serial data into input. | With suitable hardware and firmware, implements Classic HID or BLE HID over GATT and can use a compatible host’s Bluetooth support. |
| Setup | Fixed RF address/channel/rate with a pre-provisioned key and an authenticated session handshake. Plug the dongle into USB. | Pair a peer, open an SPP/RFCOMM connection and frame the serial protocol. UART and module firmware/AT settings both matter. | Pair/bond and implement the HID service/profile, report map and security with a supported stack. Host compatibility still matters. |
| Data rate | Nordic supports 250 kbps, 1 Mbps and 2 Mbps. The project uses 250 kbps. A 32-byte payload alone takes about 1.024 ms at that rate, before framing, turnaround, ACKs and any retries. | The vendor quotes up to 3 Mbps EDR modulation. That physical-layer rate is distinct from UART baud, RFCOMM goodput and the rate at which desktop input arrives. | Depends on the PHY and connection settings. PHY rate alone does not demonstrate end-to-end HID responsiveness. |
| Latency and overhead | Firmware controls report framing and retry parameters. A secure envelope occupies 20 of 32 bytes. Also account for hardware framing, the application ACK, task scheduling and USB polling. | Bluetooth uses link scheduling and stack buffering. Bytes pass through UART, Bluetooth layers and a host translator for this use case. Latency varies by hardware, settings and OS. | Elimination of a dedicated dongle can help integration. Actual latency depends on connection intervals, host scheduling, stack and implementation. |
| Retries and custom packets | Enhanced ShockBurst handles radio CRC, ACK and retry. The application adds encrypted feedback, deduplication and link recovery. The 32-byte limit shaped the protocol. | The Bluetooth stack provides lower-layer delivery/retries. The UART-facing application gets a byte stream and must frame messages and define what processed input means. | The stack handles transport. The HID profile defines report exchange and setup. |
| Complexity and security | Straightforward radio peripheral and fixed peers, with more work in session security, replay, key provisioning and the USB dongle. | Convenient embedded serial bridge. Reaching mouse/keyboard functionality adds translation and deployment complexity. | Removes the dedicated receiver on compatible hosts but requires a Bluetooth HID-capable module or SoC and profile firmware. |

**Defensible conclusion.** The nRF design was chosen for control of short packets and hardware ACK/retry, together with ordinary USB HID at the PC. The decisive comparison is with HC-05’s stock SPP interface, which cannot present a native mouse/keyboard collection. None of the configured rates or protocol features establishes a measured latency advantage over Bluetooth. An A/B latency result would need equivalent input workloads and instrumentation from input capture to host arrival. [Nordic nRF24L01+ specification](https://docs-be.nordicsemi.com/bundle/nRF24L01P_PS_v1.0/raw/resource/enus/nRF24L01P_PS_v1.0.pdf) · [HC-05 manufacturer datasheet](https://www.hc01.com/downloads/HC-05%20english%20datasheet.pdf) · [Bluetooth SIG profiles](https://www.bluetooth.com/specifications/specs/) · [Classic HID](https://www.bluetooth.com/specifications/specs/human-interface-device-profile-1-0/) · [BLE HID over GATT](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/hogp_v1.0/out/en/index-en.html)

For scale, a hypothetical 12-byte report on a 9,600-baud 8N1 UART takes 12.5 ms to serialize. At 115,200 baud it takes about 1.04 ms. Those values include start and stop bits, and exclude the Bluetooth link, bridge and host. They illustrate how the UART configuration can affect an SPP design.

## 6. Likely marker questions

| Question | Answer to give |
| --- | --- |
| Where is the originality? | One local match coordinates an optical sensor’s hardware CPI, addressable RGB, mouse/keyboard macros and optional HID test login. Mixed-signal sensing and independent sensor, RF and USB services make it more than a collection of lab demos. |
| Why do you need a second MCU? | The nRF is a radio transceiver. The receiver MCU owns USB enumeration, report formatting, input release and feedback, allowing the host’s existing HID stack to handle the device. |
| Can HC-05 replace the nRF pair? | It could carry custom bytes over Classic SPP, but would require a host translator or another HID bridge. Direct Bluetooth HID calls for suitable hardware, firmware and a supported HID profile. |
| Is 250 kbps your report rate? | It is the nominal RF symbol/data rate in the selected mode. HID cadence also depends on bursts, two ACK layers, contention, task wakeups and USB. Only an instrumented test measures end-to-end delay. |
| Why use a slower RF rate? | It provides a better specified receiver sensitivity on the nRF24L01+ at the cost of longer airtime. The code also gives a full-size ACK adequate turnaround time. Confirm the trade-off on actual hardware. |
| Why both CRC and AES-CCM? | Radio CRC detects accidental link errors. CCM checks that someone with the key produced the frame and protects its contents. Sequence and session state address replay and duplicate HID delivery. |
| What happens when a packet or its ACK is lost? | The hardware retries, then the mouse can retry the same application sequence. The receiver acknowledges its last accepted sequence, so retries do not repeat clicks or key presses. Timeout queues neutral HID reports to avoid a stuck button. |
| What if power fails during a profile update? | The old authenticated slot remains until the verified new record commits. A pending record also documents an unfinished fingerprint store/delete so recovery can reconcile the two devices. |
| Why only two users with a three-slot EEPROM layout? | A 72-byte record fits three times within 256 bytes. The spare enables copy-on-write and recovery rather than overwriting a live user slot. The sensor’s template capacity is a separate limit. |
| Where is the fingerprint stored? | The ZW3020 keeps templates and performs search internally. STM32 receives an ID/score. The local EEPROM stores preferences and the optional encrypted password keyed by ID. |
| Does this replace Windows Hello? | The prototype performs local matching and emits USB keyboard reports for a test credential. Host-native biometrics, secure key storage and an operating-system authentication provider would be a different integration. |
| How does a side button produce Ctrl+V? | A debounced rising edge selects a macro in the active profile. It sends Left Ctrl plus the V usage, then an all-zero release. Back/Forward use mouse report bits. |
| Why does battery percentage fluctuate or differ from a meter? | Under-load terminal voltage varies, and a linear estimate is sensitive to ADC reference and divider tolerance. The pot demo verifies the ADC-to-LED path without making a state-of-charge accuracy claim. |
| What went wrong during integration? | The board shares FSMC/NADV and PB7 behavior on this MCU. I²C accesses gate and restore FSMC. The radio reconnect logic was also hardened with a receiver-side recovery fix and a longer confirmed-traffic timeout. |
| What would you improve next? | Capture timing on the final hardware, measure range and current, characterize ADC voltage against known packs and add stronger physical key protection. |

## 7. Demonstration checklist

**Before the session**

- Build both targets from the same checkout. Check that both use the same radio key. Use normal page programming with F5, preserving the nonce journal. Verify the active `cmake.sourceDirectory` before each flash.
- In wireless mode, connect the receiver’s native USB to the PC. Power the mouse without enumerating its native USB. Attach the mouse’s CH340 serial connection if showing configuration. Check the receiver says `nRF HID: linked`.
- Prepare two verified profiles if both fingers are available. Give them visibly different RGB and CPI, and set an easy-to-observe macro such as Ctrl+A or Ctrl+V. Rehearse returning to the first profile. Prepare a blank text editor and known clipboard content. Keep real credentials out of the demo.
- For ADC mode, confirm whether the build is set to the potentiometer and wire accordingly. Turn the pot through the range before presenting and wait around one second per change. Bring up the menu in an open 115200 8N1 terminal.
- Reserve one noncritical recovery option. If RF is unreliable at the venue, native USB on the mouse demonstrates the same input/profile path. Say which transport you switch to.

| Demo time | Action | Observable evidence |
| --- | --- | --- |
| 0:00–1:00 | Show the two boards, connect/point to the USB receiver and move, click and scroll on the PC. | Normal HID input and receiver link status. |
| 1:00–2:10 | Scan user A, then user B if available. Use the same surface and comparable mouse travel. | Active ID, RGB difference and different cursor displacement at the two CPI values. Treat displacement as qualitative. |
| 2:10–3:00 | Show a shortcut in a prepared editor. If rehearsed, demonstrate the optional test string in a deliberately focused scratch field. | One shortcut per fresh press. A profile transition can produce one test typing sequence. Retouching the same finger does not repeat it. |
| 3:00–4:15 | Open `config`, show the saved mappings, and exit. If rehearsal confirms reconnection time, reboot the mouse and rescan to show persistence. | Saved settings survive power cycling. Avoid live enrollment unless a marker asks and time allows. |
| 4:15–5:00 | Sweep the pot and show the three indicator bars. If time remains, explain link-loss release from the verified source/host test. | ADC input controls the LED bar. State explicitly that this is the demonstration input. |

Keep the final two minutes for questions or a recovery. If asked to change code live, a small mapping setting in the menu, a safe compile-time debug switch, or a profile CPI change is easier to demonstrate than rebuilding security keys or erasing Flash.

## 8. Troubleshooting

| Symptom | First checks and recovery |
| --- | --- |
| Receiver waits for USB | Use its native USB port and a data cable. Check host enumeration. A CH340 COM port is the serial terminal, while mouse/keyboard enumeration occurs through native USB. |
| Receiver waits for mouse or cycles | Use builds from the same checkout, matching radio key, radio rate and channel. Check 3.3 V at both modules under load, antenna connection, CSN/CE and the on-board Flash CS. Avoid running the mouse in wired mode while testing RF. If debugging counters are enabled, distinguish `RF loss`, `USB loss` and `Reject`. |
| Pointer stationary while clicks work | Inspect the PMW wiring, NCS, regulator/logic rails, surface and lens. Re-enable mouse LCD diagnostics if useful. An ID failure points to power, SPI mode or wiring. An SROM/config error points to upload timing/readback or an incompatible module. |
| Finger does not activate the saved user | Exit the terminal configuration menu. Lift fully and place again. Check UART baud, crossed TX/RX, common ground, matching template ID and existence of a saved profile. PA0 retries module connection after an error. |
| Back/Forward instead of Ctrl+C/V | Reopen the saved profile, verify Save, exit the menu, and match the intended user when two profiles exist. One saved profile provides the side mappings at boot. With LCD debug enabled the `S1/S2` line shows the live assignments and pressed bits. Check active-low switch wiring and leave the SD slot empty. Test Ctrl+C with selected text. |
| EEPROM read/write/authentication error | Use Retry EEPROM, inspect address and PB6/PB7, and review the FSMC clock gate on LCD-enabled builds. An authentication error can follow changed keys, corruption or an incompatible firmware image. Preserve the saved key and confirm before resetting profiles. |
| Wrong typed characters or no test login | Confirm active profile, a normal fresh fingerprint transition, USB/link readiness, host focus and US-layout expectations. The Caps Lock state is transported back from the receiver. Keep the credential synthetic. |
| RGB absent or colours swapped | Confirm strip DIN direction, ground, PA8 buzzer jumper removed, TIM1_CH1 DMA1 Channel 2 and the selected profile mode/brightness. The wire order is GRB. A quiet dim strip can reflect profile Off, sparsity or brightness cap. |
| Battery bars appear wrong | Confirm which branch was compiled and that the correct wiring is at PC2. Test pot endpoints or compare measured ADC-node and pack voltages before changing calibration. Never feed raw pack voltage into PC2. |
| Mouse LCD stays dark | The product build deliberately defaults to dark. Uncomment `LCD_FOR_DEBUG` in [main.h](mouse/Core/Inc/main.h) and rebuild when debugging. |
| F5 launches the wrong firmware | Check the local ignored `.vscode/settings.json` and select the intended project. Use the active ST-Link launch and normal page erase. Preserve the last two Flash pages containing the nonce journal. |
