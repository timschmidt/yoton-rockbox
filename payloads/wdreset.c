/* clear the Rockbox/stub RAM markers and reset through the RTC watchdog
 * (as system_reboot does): the NOR stub then boots normally */
#include <stdint.h>
#define M32(a) (*(volatile uint32_t *)(a))
void *entry(void)
{
	M32(0x137ff0) = 0; M32(0x137ff4) = 0; M32(0x137ff8) = 0;
	__asm__ volatile ("cpsid i");
	M32(0xc003001c) = ((M32(0xc003001c) & ~0xeu) + 6) | 0x11;
	for (;;);
}
