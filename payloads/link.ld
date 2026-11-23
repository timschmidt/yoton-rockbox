OUTPUT_FORMAT("elf32-littlearm")
ENTRY(_start)
SECTIONS {
	. = IMAGE_START;
	.text : { KEEP(*(.text._start)) *(.text .text.*) *(.rodata .rodata.*) *(.data .data.*) }
	. = ALIGN(4); __bss_start = .;
	.bss : { *(.bss .bss.* COMMON) }
	. = ALIGN(4); __bss_end = .;
	__image_end = .;
	/DISCARD/ : { *(.ARM.exidx*) *(.comment) }
}
