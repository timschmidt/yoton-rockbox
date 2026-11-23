/* Do SRAM2/SRAM3 answer at 0x200000/0x300000? Probe, then (ARGS[0]) rewrite
 * the 0xc0040000 window registers to the USB-recovery layout and probe again. */
#include <stdint.h>
#define M32(a) (*(volatile uint32_t *)(a))
#define ARGS ((volatile uint32_t *)0x11f800)
struct res { void *addr; unsigned size; };
static struct res r; static uint32_t o[24];
static uint32_t probe(uint32_t a)
{
	uint32_t old = M32(a);
	M32(a) = 0x12345678;
	uint32_t ok = M32(a) == 0x12345678;
	M32(a) = old;
	return ok;
}
void *entry(void)
{
	int n = 0;
	o[n++] = probe(0x200000); o[n++] = probe(0x206000); o[n++] = probe(0x300000); o[n++] = probe(0x304000);
	for (int i = 0; i < 8; i++) o[n++] = M32(0xc0040000 + 4 * i);
	if (ARGS[0] == 5) {			/* stock firmware sequence */
		M32(0xc0001008) |= 0x600;
		M32(0xc0000000) &= ~0x600u; M32(0xc0000000) |= 0x600;
		M32(0xc0040010) = 0x200000; M32(0xc0040014) = 0x207fff;
		M32(0xc0040018) = 0x300000; M32(0xc004001c) = 0x307fff;
		M32(0xc0040300) = 0x200000; M32(0xc0040304) = 0;
		M32(0xc0040308) = 0x300000; M32(0xc004030c) = 0;
		__asm__ volatile("dsb; isb");
	}
	if (ARGS[0] == 2) {			/* bit 16 write-1-to-clear? */
		M32(0xc0040000) = 0x10000;
	}
	if (ARGS[0] == 3) {			/* +0x08 first, then +0x00 */
		M32(0xc0040008) = 0; M32(0xc0040000) = 0;
	}
	if (ARGS[0] == 4) {			/* clear low bits too */
		M32(0xc0040000) &= ~0x1ffffu; M32(0xc0040004) = 0xff;
	}
	if (ARGS[0] == 1) {
		M32(0xc0040000) = 0; M32(0xc0040008) = 0;
		M32(0xc0040010) = 0x00200000; M32(0xc0040014) = 0x00200000;
		M32(0xc0040018) = 0x00300000; M32(0xc004001c) = 0x00300000;
		__asm__ volatile("dsb; isb");
	}
	for (int i = 0; i < 8; i++) o[n++] = M32(0xc0040000 + 4 * i);
	o[n++] = probe(0x200000); o[n++] = probe(0x206000); o[n++] = probe(0x300000); o[n++] = probe(0x304000);
	r.addr = o; r.size = n * 4; return &r;
}
