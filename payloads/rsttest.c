#include <stdint.h>
#define M32(a) (*(volatile uint32_t *)(a))
#define ARGS ((volatile uint32_t *)0x11f800)
struct res { void *addr; unsigned size; };
static struct res r; static uint32_t o[4];
void *entry(void)
{
	o[0] = M32(0xe000ed0c);
	if (ARGS[0] == 1) {
		__asm__ volatile("dsb");
		M32(0xe000ed0c) = 0x05fa0004;		/* SYSRESETREQ */
	} else if (ARGS[0] == 3) {
		__asm__ volatile("cpsid i");
		M32(0xc003001c) = ((M32(0xc003001c) & ~0xeu) + 6) | 0x11;	/* vendor reboot */
		for (volatile int i = 0; i < 20000000; i++);
		__asm__ volatile("cpsie i");
	} else if (ARGS[0] == 2) {
		M32(0xe000ed0c) = 0x05fa0001;		/* VECTRESET (core only) */
	}
	for (volatile int i = 0; i < 2000000; i++);
	o[1] = 0xdeadbeef;				/* still alive: no reset */
	o[2] = M32(0xe000ed0c);
	r.addr = o; r.size = 12; return &r;
}
