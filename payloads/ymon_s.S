@ ymon assembly helpers: entry, protected call, fault recovery.
	.syntax unified
	.thumb
	.section .text._start,"ax"
	.global _start
	.thumb_func
_start:
	@ private stack: the ROM's stack sits in 0x100000-0x101b18, which
	@ payloads/firmware images are allowed to overwrite
	ldr	r0, =0x0011e000
	mov	sp, r0
	ldr	r0, =__bss_start
	ldr	r1, =__bss_end
	movs	r2, #0
1:	cmp	r0, r1
	bhs	2f
	str	r2, [r0], #4
	b	1b
2:	b	ymon_main
	.ltorg

	.text
@ uint32_t exec_protected(uint32_t fn)
@ Calls fn(); if it faults, fault_entry unwinds back here and returns 0.
	.global exec_protected
	.thumb_func
exec_protected:
	push	{r4-r11, lr}
	ldr	r1, =exec_saved_sp
	str	sp, [r1]
	blx	r0
	ldr	r1, =exec_saved_sp
	movs	r2, #0
	str	r2, [r1]
	pop	{r4-r11, pc}

	.global exec_abort
	.thumb_func
exec_abort:
	ldr	r1, =exec_saved_sp
	ldr	sp, [r1]
	movs	r2, #0
	str	r2, [r1]
	movs	r0, #0
	pop	{r4-r11, pc}

@ USB IRQ entry: pass the interrupted context's exception frame to usb_irq.
	.global usb_irq_entry
	.thumb_func
usb_irq_entry:
	tst	lr, #4
	ite	eq
	mrseq	r0, msp
	mrsne	r0, psp
	push	{r4, lr}
	bl	usb_irq
	pop	{r4, pc}

@ Common fault handler. Records fault state; if a protected call is in
@ progress, redirects the exception return to exec_abort.
	.global fault_entry
	.thumb_func
fault_entry:
	tst	lr, #4
	ite	eq
	mrseq	r0, msp
	mrsne	r0, psp
	ldr	r1, =fault_info
	ldr	r2, =0x544c4146		@ "FALT"
	str	r2, [r1, #0]
	ldr	r3, =0xe000ed00
	ldr	r2, [r3, #0x28]		@ CFSR
	str	r2, [r1, #4]
	str	r2, [r3, #0x28]
	ldr	r2, [r3, #0x2c]		@ HFSR
	str	r2, [r1, #8]
	str	r2, [r3, #0x2c]
	ldr	r2, [r3, #0x34]		@ MMFAR
	str	r2, [r1, #12]
	ldr	r2, [r3, #0x38]		@ BFAR
	str	r2, [r1, #16]
	ldr	r2, [r0, #24]		@ stacked PC
	str	r2, [r1, #20]
	ldr	r2, [r0, #20]		@ stacked LR
	str	r2, [r1, #24]
	ldr	r2, [r0, #28]		@ stacked xPSR
	str	r2, [r1, #28]
	ldr	r1, =exec_saved_sp
	ldr	r1, [r1]
	cbz	r1, 1f
	ldr	r2, =exec_abort
	bic	r2, r2, #1
	str	r2, [r0, #24]		@ resume at exec_abort
	mov	r2, #0x01000000		@ xPSR: Thumb, clear IT state
	ldr	r3, [r0, #28]
	ubfx	r3, r3, #0, #9		@ keep exception number
	orr	r2, r2, r3
	str	r2, [r0, #28]
	bx	lr
1:	b	1b
	.ltorg
