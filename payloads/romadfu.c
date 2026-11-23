/* Enter the boot ROM's ADFU loop exactly as its boot flow ends (0x2514):
 * cleanup 0x11d4, 0x1258, then 0x1144 (never returns). */
#include <stdint.h>
#define ROMFN(addr, ret, name, args) static ret (* const name) args = (ret (*) args)((addr) | 1)
ROMFN(0x11d4, void, rom_cleanup1, (void));
ROMFN(0x1258, void, rom_cleanup2, (void));
ROMFN(0x1144, void, rom_adfu, (void));
void *entry(void)
{
	__asm__ volatile("cpsid i");
	*(volatile uint32_t *)0xe000e180 = 0xffffffff;	/* NVIC off */
	*(volatile uint32_t *)0xe000ed08 = 0;		/* VTOR: ROM */
	__asm__ volatile("dsb; isb; cpsie i");
	rom_cleanup1();
	rom_cleanup2();
	rom_adfu();
	for (;;);
	return 0;
}
