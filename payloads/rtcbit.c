/* ARGS[0]: new RTC_CTL bit-17 state (0/1), committed with the RTC update
 * handshake. Returns RTC_CTL before/after. */
#include <stdint.h>
#define M32(a) (*(volatile uint32_t *)(a))
#define ARGS ((volatile uint32_t *)0x11f800)
struct res { void *addr; unsigned size; };
static struct res r; static uint32_t o[4];
void *entry(void)
{
	o[0] = M32(0xc0030000);
	if (ARGS[0]) M32(0xc0030000) |= 0x20000; else M32(0xc0030000) &= ~0x20000u;
	M32(0xc0030004) = 0xa596;
	int i;
	for (i = 0; i < 100000 && M32(0xc0030004) != 0x5a69; i++);
	o[1] = M32(0xc0030000); o[2] = i;
	r.addr = o; r.size = 12; return &r;
}
