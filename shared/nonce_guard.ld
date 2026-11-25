/* F103 high-density Flash pages 254 and 255 are owned by boot_epoch.c.
 * Keep this check even if CubeMX regenerates the main linker script. */
ASSERT(LOADADDR(.tdata) + SIZEOF(.tdata) <= 0x0807F000,
       "Firmware overlaps the cryptographic epoch journal")
