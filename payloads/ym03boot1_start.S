	.syntax unified
	.thumb
	.section .header,"a"
	.word 0, 0, 0, 0		@ checksum/type filled by the installer
	.section .entry,"ax"
	.global _boot1_start
	.thumb_func
_boot1_start:				@ 0x101010
	ldr	r0, =0x00101800
	mov	sp, r0
	bl	boot1_main
1:	b	1b
	.ltorg
