/* Enter the NOR boot stub the way the mask ROM would: SPI0 set up for the
 * NOR, interrupts off, VTOR back on the ROM. The 1 KiB record is uploaded to
 * 0x122000 and copied to 0x101000 here (the ROM's USB state lives low). */
#include "spinor.h"
static int unused(void) __attribute__((unused));
static int unused(void) { return nor_read(0, 0, 0); }
void *entry(void)
{
	nor_setup();
	{ static const uint8_t c9f[] = { 0x9f }; uint8_t id[4]; spi_cmd(c9f, 1, id, 3, 0); }
	__asm__ volatile("cpsid i");
	M32(0xe000e180) = 0xffffffff;
	M32(0xe000ed08) = 0;
	for (int i = 0; i < 256; i++)
		((volatile uint32_t *)0x101000)[i] = ((volatile uint32_t *)0x122000)[i];
	__asm__ volatile(
		"dsb; isb\n"
		"ldr r0, =0x101800\n"
		"mov sp, r0\n"
		"cpsie i\n"
		"ldr r0, =0x101011\n"
		"bx r0\n"
		".ltorg\n");
	return 0;
}
