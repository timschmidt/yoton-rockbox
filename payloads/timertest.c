/* RTC-block timer T0 probe: ARGS[0] = CTL value to write, ARGS[1] = period.
 * Returns CTL/VAL/+8/+0xc and +0x120..+0x12c snapshots over ~20 ms, the
 * NVIC pending words and CMU_CLKEN0. */
#include <stdint.h>
#define M32(a) (*(volatile uint32_t *)(a))
#define ARGS ((volatile uint32_t *)0x11f800)
struct res { void *addr; unsigned size; };
static struct res r; static uint32_t o[32];
static void dly(int n) { for (volatile int i = 0; i < n * 1000; i++); }
void *entry(void)
{
	int n = 0;
	M32(0xc0001008) |= 1u << 24;
	if (ARGS[2] & 2) {
		M32(0xc0000000) &= ~(1u << 24);		/* timer block reset */
		dly(1);
		M32(0xc0000000) |= 1u << 24;
		M32(0xc0000104) |= 1;			/* clock source enable? */
	}
	if (ARGS[2] & 1) {
		/* vendor KER_INIT: RTC_CTL bit 17, then the RTC update handshake */
		M32(0xc0030000) |= 0x20000;
		M32(0xc0030004) = 0xa596;
		for (int i = 0; i < 100000 && M32(0xc0030004) != 0x5a69; i++);
	}
	M32(0xe000e280) = 0xffffffff;		/* clear NVIC pending */
	M32(0xc0030100) = 0;
	M32(0xc0030104) = ARGS[1];
	M32(0xc0030100) = ARGS[0];
	for (int k = 0; k < 4; k++) {
		dly(20);
		o[n++] = M32(0xc0030100);
		o[n++] = M32(0xc0030104);
		o[n++] = M32(0xc0030108);
		o[n++] = M32(0xc003010c);
	}
	o[n++] = M32(0xc0030120); o[n++] = M32(0xc0030124);
	o[n++] = M32(0xc0030128); o[n++] = M32(0xc003012c);
	o[n++] = M32(0xe000e200); o[n++] = M32(0xe000e204);
	o[n++] = M32(0xc0001008);
	o[n++] = M32(0xc0030000);
	o[n++] = M32(0xc0000104);
	M32(0xc0030100) = 1;			/* stop + ack */
	r.addr = o; r.size = n * 4; return &r;
}
