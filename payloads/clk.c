/* Spin for ARGS[0] wraps of SysTick (2^24 core cycles each). Host times it. */
#include <stdint.h>
#define M32(a) (*(volatile uint32_t *)(a))
#define ARGS ((volatile uint32_t *)0x11f800)
struct res { void *addr; unsigned size; };
static struct res r; static uint32_t o[4];
void *entry(void)
{
	M32(0xe000e014) = 0xffffff;	/* LOAD */
	M32(0xe000e018) = 0;		/* VAL */
	M32(0xe000e010) = 5;		/* CTRL: enable, core clock, no IRQ */
	uint32_t n = ARGS[0], w = 0;
	while (w < n)
		if (M32(0xe000e010) & 0x10000) w++;
	o[0] = w; o[1] = M32(0xc0001000); o[2] = M32(0xc0001004);
	M32(0xe000e010) = 0;
	r.addr = o; r.size = 12; return &r;
}
