#include <stdint.h>
#define M32(a) (*(volatile uint32_t *)(a))
struct res { void *addr; unsigned size; };
static struct res r; static uint32_t o[80];
static int dac_ok(void)
{
	M32(0xc0180028) = 0x12345680;
	int ok = M32(0xc0180028) == 0x12345680;
	M32(0xc0180028) = 0;
	return ok;
}
void *entry(void)
{
	int n = 0;
	o[n++] = M32(0xc000100c); o[n++] = M32(0xc0000004);
	o[n++] = dac_ok();
	for (int b = 0; b < 32 && !dac_ok(); b++) {
		M32(0xc000100c) |= 1u << b;
		M32(0xc0000004) |= 1u << b;
		for (volatile int i = 0; i < 100; i++);
		o[n++] = b << 8 | dac_ok();
	}
	o[n++] = M32(0xc000100c); o[n++] = M32(0xc0000004);
	r.addr = o; r.size = n * 4; return &r;
}
