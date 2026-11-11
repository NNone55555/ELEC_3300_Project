# HLK-ZW fingerprint test reference

The EF-01 packet format, checksums, command parameters and two-scan enrollment
sequence in Core/Src/fingerprint_test.c are adapted from Gavin's MIT-licensed
HLK-ZW-Fingerprint-Sensor library. The STM32 test uses HAL UART instead of Arduino.

Upstream source at the inspected commit:
https://github.com/GavinnnTann/HLK-ZW-Fingerprint-Sensor/tree/06c159f1b2b7ff8c93aa6f234f576681a7e1861f

Copyright (c) 2026 Gavin. The original MIT license is included in LICENSE.
The C adaptation adds bounded ACK parsing, validates the ACK address/type,
uses standard Search 0x04 for ZW3020, selects an unoccupied template slot,
and drives the test from PA0 and the existing FreeRTOS default task.
