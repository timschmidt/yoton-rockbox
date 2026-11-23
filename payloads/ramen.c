#include <stdint.h>
#define M32(a) (*(volatile uint32_t *)(a))
#define ARGS ((volatile uint32_t *)0x11f800)
#define OUT ((volatile uint32_t *)0x120000)
struct res { void *addr; unsigned size; };
static struct res r;
static int test(uint32_t a)
{
	uint32_t v = M32(a);
	M32(a) = 0x13572468; __asm__ volatile("dsb");
	int ok = M32(a) == 0x13572468;
	M32(a) = v;
	return ok;
}
void *entry(void)
{
	volatile uint32_t *o = OUT;
	uint32_t cmu0 = M32(0xc0001008), cmu1 = M32(0xc000100c);
	uint32_t rmu0 = M32(0xc0000000), rmu1 = M32(0xc0000004);
	*o++ = test(0x200040) | test(0x300040) << 1 | test(0x400040) << 2;
	for (int b = 0; b < 64; b++) {
		uint32_t m = 1u << (b & 31);
		if (b < 32) {
			if (cmu0 & m) continue;
			if (b == 6) continue;
			M32(0xc0001008) |= m; M32(0xc0000000) |= m;
		} else {
			if (cmu1 & m) continue;
			M32(0xc000100c) |= m; M32(0xc0000004) |= m;
		}
		for (volatile int i = 0; i < 1000; i++);
		uint32_t t = test(0x200040) | test(0x300040) << 1 | test(0x400040) << 2 | test(0x210040) << 3;
		*o++ = b << 16 | t;
		*o++ = M32(0x200040);
	}
	*o++ = M32(0xc0001008); *o++ = M32(0xc000100c); *o++ = M32(0xc0000000); *o++ = M32(0xc0000004);
	r.addr = (void *)OUT; r.size = (uint8_t *)o - (uint8_t *)OUT;
	return &r;
}
