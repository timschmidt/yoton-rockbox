/* read SRAM3 0x305000..0x307fff after a reset: window registers only (no
 * reset pulse, which might clear it); also reports the window registers */
#include <stdint.h>
#define M32(a) (*(volatile uint32_t *)(a))
struct res { void *addr; unsigned size; };
static struct res r; static uint32_t o[4 + 256];
void *entry(void)
{
	o[0] = M32(0xc0001008); o[1] = M32(0xc0040018); o[2] = M32(0xc004001c); o[3] = M32(0xc0040308);
	M32(0xc0001008) |= 0x600;
	M32(0xc0040018) = 0x300000; M32(0xc004001c) = 0x307fff;
	M32(0xc0040308) = 0x300000; M32(0xc004030c) = 0;
	for (int i = 0; i < 256; i++) o[4 + i] = M32(0x305000 + 4 * i);
	r.addr = o; r.size = sizeof(o); return &r;
}
