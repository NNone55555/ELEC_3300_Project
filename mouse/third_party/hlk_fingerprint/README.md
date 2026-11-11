# HLK-ZW fingerprint protocol reference

The EF-01 packet format, checksums, command parameters and two-scan enrollment
sequence in Core/Src/zw3020.c and Core/Src/fingerprint.c are adapted from Gavin's
MIT-licensed HLK-ZW-Fingerprint-Sensor library. The STM32 driver uses HAL UART.

Upstream source at the inspected commit:
https://github.com/GavinnnTann/HLK-ZW-Fingerprint-Sensor/tree/06c159f1b2b7ff8c93aa6f234f576681a7e1861f

Copyright (c) 2026 Gavin. The original MIT license is included in LICENSE.
The C adaptation adds bounded ACK parsing, validates the ACK address/type,
uses standard Search 0x04 for ZW3020, selects an unoccupied template slot,
and runs enrollment/matching as a finite state machine in a FreeRTOS task.
UART2 reception uses an interrupt-driven ring buffer. LCD/console output is
owned by the application display task.
