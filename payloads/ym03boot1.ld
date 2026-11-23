OUTPUT_FORMAT("elf32-littlearm")
ENTRY(_boot1_start)
SECTIONS {
	. = 0x101000;
	.hdr : { KEEP(*(.header)) }
	.text 0x101010 : { KEEP(*(.entry)) *(.text .text.*) *(.rodata .rodata.*) *(.data .data.*) . = ALIGN(4); }
	.bss (NOLOAD) : { *(.bss .bss.* COMMON) }
	__end = ADDR(.text) + SIZEOF(.text);
	ASSERT(__end <= 0x101200, "stage 1 must fit the 512-byte boot record")
	ASSERT(SIZEOF(.bss) == 0, "no .bss")
	/DISCARD/ : { *(.ARM.exidx*) *(.comment) }
}
