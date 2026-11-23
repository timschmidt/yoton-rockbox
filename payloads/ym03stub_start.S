	.syntax unified
	.thumb
	.section .header,"a"
	.word 0, 0, 0, 0		@ checksum/marker filled by mkstub.py
	.section .entry,"ax"
	.global _stub_start
	.thumb_func
_stub_start:				@ 0x101010
	ldr	r0, =0x00101800
	mov	sp, r0
	bl	stub_main
1:	b	1b
	.ltorg
