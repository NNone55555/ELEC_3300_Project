# PMW3360 SROM 0x04

The 4094 SROM bytes in Core/Inc/pmw3360_srom.h are extracted without changing
byte values from Sunjun Kim's PMW3360 Arduino library:

https://github.com/SunjunKim/PMW3360/blob/03b8b9a3666b68c7ac78493b0c2ccd95788b504f/src/PMW3360.cpp

Upstream file copyright (c) 2019 Sunjun Kim; its notice specifies GNU LGPL 2.1
or later. The notice is retained in the extracted header, with the license
text in COPYING.LESSER. No Arduino runtime or upstream driver code is linked.
The byte array is const and resides in STM32 flash. It is uploaded to the
sensor's volatile SROM on every MCU startup, then verified against SROM_ID 0x04.
This public image is not a seller-supplied module firmware package.

Binary SROM SHA-256: `3060ef49ba0049c2f5330a4bd882b62dc1025ce14f4c37390017a2191e4049ef`

Changes from upstream: extracted just firmware_data, converted to a fixed-size
uint8_t C array, removed AVR PROGMEM, and added include guards/size/ID constants.
