/* Record key inputs: LRADC1, GPIO in banks 0/1, a few PMU status words,
 * every ARGS[1] ms for ARGS[0] samples (SysTick @ 48 MHz for timing). */
#include <stdint.h>
#define M32(a) (*(volatile uint32_t *)(a))
#define ARGS ((volatile uint32_t *)0x11f800)
#define BUF ((volatile uint16_t *)0x120000)
struct res { void *addr; unsigned size; };
static struct res r;
void *entry(void)
{
	uint32_t n = ARGS[0], ms = ARGS[1];
	volatile uint16_t *o = BUF;
	M32(0xe000e014) = 48000 - 1;
	M32(0xe000e018) = 0;
	M32(0xe000e010) = 5;
	/* GPIO24/25 as inputs (no function, no output) */
	M32(0xc01c0004 + 4 * 24) &= ~0x5f;
	M32(0xc01c0004 + 4 * 25) &= ~0x5f;
	for (uint32_t i = 0; i < n; i++) {
		o[0] = M32(0xc0010050) & 0x3ff;			/* LRADC1 */
		o[1] = M32(0xc0010054) & 0x3ff;			/* LRADC2 (just in case) */
		uint32_t g0 = M32(0xc01c0230), g1 = M32(0xc01c0234);
		o[2] = g0 >> 16; o[3] = g0; o[4] = g1;
		o[5] = M32(0xc0010028) >> 16;			/* PMU status high */
		o[6] = M32(0xc0010014);
		o[7] = M32(0xc0010018);
		o += 8;
		for (uint32_t t = 0; t < ms; t++)
			while (!(M32(0xe000e010) & 0x10000));
	}
	M32(0xe000e010) = 0;
	r.addr = (void *)BUF; r.size = (uint8_t *)o - (uint8_t *)BUF;
	return &r;
}
