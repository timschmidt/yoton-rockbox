	.syntax unified
	.thumb
	.section .header,"a"
	.word 0x32545359, 0, 0, 0	@ "YST2": stage 2 magic
	.section .entry,"ax"
	.global _stub_start
	.thumb_func
_stub_start:				@ 0x101010
	ldr	r0, =0x00101800
	mov	sp, r0
	bl	stub_main
1:	b	1b
	.ltorg
