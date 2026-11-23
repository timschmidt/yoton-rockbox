/* SRAM3 thread stack high-water marks: maps SRAM3 like sram3rd.c, then
 * for each 0x100 bytes of 0x304000..0x307fff reports whether it still
 * holds the 0xdeadbeef fill */
#include <stdint.h>
#define M32(a) (*(volatile uint32_t *)(a))
struct res { void *addr; unsigned size; };
static struct res r; static uint8_t o[64];
void *entry(void)
{
	M32(0xc0001008) |= 0x600;
	M32(0xc0040018) = 0x300000; M32(0xc004001c) = 0x307fff;
	M32(0xc0040308) = 0x300000; M32(0xc004030c) = 0;
	for (int b = 0; b < 64; b++) {
		int clean = 1;
		for (int i = 0; i < 64; i++)
			if (M32(0x304000 + b * 256 + i * 4) != 0xdeadbeef)
				clean = 0;
		o[b] = clean;
	}
	r.addr = o; r.size = sizeof(o); return &r;
}
