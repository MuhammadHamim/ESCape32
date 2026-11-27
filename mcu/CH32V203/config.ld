/* CH32V203C8U6: 64K flash @ 0x08000000, 20K SRAM @ 0x20000000.
 *
 * BRING-UP LAYOUT (no bootloader yet): after reset the CPU runs the first instruction at 0x08000000,
 * so the application ('rom') must start there. The 2K 'cfg' page sits at the END of flash.
 * 'boot' has zero length: it only anchors _boot = flash base (used for page-number math in util.c).
 * When a RISC-V bootloader exists, switch back to the boot | cfg | rom order used by the ARM ports.
 */
MEMORY {
	boot (rx) : ORIGIN = 0x8000000, LENGTH = 0
	rom (rx) : ORIGIN = 0x8000000, LENGTH = FLASH_SIZE - 2K
	cfg (rx) : ORIGIN = 0x8000000 + FLASH_SIZE - 2K, LENGTH = 2K
	ram (rwx) : ORIGIN = 0x20000000, LENGTH = 20K
}
