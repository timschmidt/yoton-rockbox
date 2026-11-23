/* Does the DAC drain its FIFO? Write until bit7 clears, then time how long
 * until bit7 sets again (SysTick @48 MHz). Variant ARGS[0] picks clock setup. */
#include <stdint.h>
#define M32(a) (*(volatile uint32_t *)(a))
#define ARGS ((volatile uint32_t *)0x11f800)
#define DAC(x) M32(0xc0180000 + (x))
#define CMU(x) M32(0xc0001000 + (x))
struct res { void *addr; unsigned size; };
static struct res r; static uint32_t o[16];
void *entry(void)
{
	uint32_t var = ARGS[0];
	/* vendor AUD_DEV init (bank 0x800): analog power + audio PLL */
	M32(0xc0010018) = (M32(0xc0010018) | 0x40) & ~0x20u;
	M32(0xc0000118) = (M32(0xc0000118) & ~0x1fu) | 0x1c;
	M32(0xc000011c) = (M32(0xc000011c) & ~0x1fu) | 0x14;
	for (volatile int i = 0; i < 100000; i++);
	M32(0xc0000004) &= ~5u;
	for (volatile int i = 0; i < 1000; i++);
	M32(0xc0000004) |= 1 | 4;
	CMU(0x0c) |= 1 | 4 | 0x800;
	CMU(0x88) = (CMU(0x88) & ~0xd7u) | 0x400 | (1 << 6) | 3 | (var & 0x100 ? 0x80 : 0);
	CMU(0x80) = 0x33000000 | (1 << 6) | 3 | ((var & 1) ? 0x80 : 0) | ((var & 2) ? 0x100 : 0);
	DAC(0) |= 0x113;
	DAC(4) = (DAC(4) | 1) & ~0x36u;
	o[0] = DAC(8);
	int n = 0;
	while ((DAC(8) & 0x80) && n < 4096) { DAC(0x0c) = 0; DAC(0x0c) = 0; DAC(8) = 0x80; n++; }
	o[1] = n; o[2] = DAC(8);
	M32(0xe000e014) = 0xffffff; M32(0xe000e018) = 0; M32(0xe000e010) = 5;
	uint32_t t = 0;
	while (!(DAC(8) & 0x80) && t < 2000000) t++;
	o[3] = t; o[4] = 0xffffff - M32(0xe000e018); o[5] = DAC(8);
	o[6] = M32(0xc0000118); o[7] = CMU(0x88); o[8] = M32(0xc000011c); o[9] = M32(0xc0010018);
	M32(0xe000e010) = 0;
	r.addr = o; r.size = 40; return &r;
}
