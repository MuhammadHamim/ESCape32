/* ESCape32 linker script for RISC-V (QingKe V4B / CH32V203).
 * Replaces mcu/common.ld, which ends in 'INCLUDE cortex-m-generic.ld' (libopencm3, ARM only).
 * Contract with WCH's startup_ch32v20x_D6.S:
 *   .init first in flash (_start), .vector (mtvec), _eusrstack, _data_lma/_data_vma/_edata,
 *   _sbss/_ebss, __global_pointer$.
 */
ENTRY(_start)

__stack_size = 1K; /* TODO: tune with -fstack-usage; the ASSERT below keeps data+bss+stack inside RAM */

SECTIONS {
	.cfg : { /* Same as common.ld: RAM copy of the config, load image in the 'cfg' flash page */
		_cfg_start = .;
		*(.cfg)
		. = ALIGN(8);
		_cfg_end = .;
	} >ram AT >cfg

	.init : { /* 'j handle_reset': must be the first bytes of flash */
		KEEP(*(SORT_NONE(.init)))
		. = ALIGN(4);
	} >rom

	.vector : { /* mtvec = _vector_base | 3 (table of absolute handler addresses) */
		KEEP(*(.vector))
		. = ALIGN(64);
	} >rom

	.text : {
		*(.text .text.*)
		*(.rodata .rodata.*)
		. = ALIGN(4);
	} >rom

	.data : { /* Load image is the LAST thing in rom, so _eod below is the end of the firmware */
		*(.data .data.*)
		. = ALIGN(8);
		PROVIDE(__global_pointer$ = . + 0x800); /* gp window covers .sdata/.srodata (same as WCH Link.ld) */
		*(.sdata .sdata.*)
		. = ALIGN(8);
		*(.srodata.cst16)
		*(.srodata.cst8)
		*(.srodata.cst4)
		*(.srodata.cst2)
		*(.srodata .srodata.*)
		. = ALIGN(4);
		_edata = .;
	} >ram AT >rom

	.bss (NOLOAD) : {
		. = ALIGN(4);
		_sbss = .;
		*(.sbss .sbss.*)
		*(.bss .bss.*)
		*(COMMON)
		. = ALIGN(4);
		_ebss = .;
	} >ram

	/DISCARD/ : { *(.eh_frame*) }
}

_data_vma = ADDR(.data);
_data_lma = LOADADDR(.data);
_eusrstack = ORIGIN(ram) + LENGTH(ram); /* initial sp: top of RAM */
ASSERT(_ebss + __stack_size <= _eusrstack, "RAM overflow: .cfg + .data + .bss + stack do not fit")

_boot = ORIGIN(boot);
_cfg = ORIGIN(cfg);
_rom = ORIGIN(rom);
_ram = ORIGIN(ram);
_eod = LOADADDR(.data) + SIZEOF(.data);
