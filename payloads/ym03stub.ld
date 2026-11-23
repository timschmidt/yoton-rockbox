OUTPUT_FORMAT("elf32-littlearm")
ENTRY(_stub_start)
SECTIONS {
	. = 0x101000;
	.hdr : { KEEP(*(.header)) }
	.text 0x101010 : { KEEP(*(.entry)) *(.text .text.*) *(.rodata .rodata.*) *(.data .data.*) . = ALIGN(4); }
	/* copied to 0x137800 at run time and executed there */
	.tramp 0x137800 : AT(ADDR(.text) + SIZEOF(.text)) {
		__tramp_start = .; KEEP(*(.tramp)) . = ALIGN(4); __tramp_end = .;
	}
	__tramp_load = LOADADDR(.tramp);
	__end = LOADADDR(.tramp) + SIZEOF(.tramp);
	.bss (NOLOAD) : { *(.bss .bss.* COMMON) }
	ASSERT(__end <= 0x101400, "stub too big for the 1 KiB boot record")
	ASSERT(SIZEOF(.bss) == 0, "stub must not use .bss")
	/DISCARD/ : { *(.ARM.exidx*) *(.comment) }
}
