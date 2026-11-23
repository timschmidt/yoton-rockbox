	.syntax unified
	.thumb
	.section .text._start,"ax"
	.global _start
_start:	b	entry
	.text
@ Fault handler that skips the faulting instruction (precise faults only).
	.global skip_fault
	.thumb_func
skip_fault:
	tst	lr, #4
	ite	eq
	mrseq	r0, msp
	mrsne	r0, psp
	ldr	r1, [r0, #24]		@ stacked PC
	ldrh	r2, [r1]
	and	r3, r2, #0xf800
	cmp	r3, #0xe800
	ite	hs
	addhs	r1, #4
	addlo	r1, #2
	str	r1, [r0, #24]
	ldr	r2, =fault_count
	ldr	r3, [r2]
	adds	r3, #1
	str	r3, [r2]
	ldr	r2, =0xe000ed28		@ CFSR: clear
	ldr	r3, [r2]
	str	r3, [r2]
	ldr	r2, =0xe000ed2c		@ HFSR: clear
	ldr	r3, [r2]
	str	r3, [r2]
	bx	lr
	.ltorg
	.bss
	.align 2
	.global fault_count
	.weak fault_count
fault_count:
	.space 4
