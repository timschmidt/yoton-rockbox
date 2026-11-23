#include <stdint.h>
#define M32(a) (*(volatile uint32_t *)(a))
extern void skip_fault(void);
extern volatile uint32_t fault_count;
static uint32_t vt[64] __attribute__((aligned(256)));
struct res { void *addr; unsigned size; };
static struct res r;
#define OUT ((volatile uint32_t *)0x120000)

void *entry(void)
{
	uint32_t old = M32(0xe000ed08);
	for (int i = 0; i < 64; i++) vt[i] = ((uint32_t *)old)[i];
	vt[3] = vt[4] = vt[5] = vt[6] = (uint32_t)skip_fault;
	M32(0xe000ed08) = (uint32_t)vt;
	__asm__ volatile("dsb; isb");
	volatile uint32_t *o = OUT;
	/* page scan 0x138000..0x200000: value + write test (restore) */
	for (uint32_t a = 0x138000; a < 0x200000; a += 0x800) {
		uint32_t v = M32(a + 0x40);
		uint32_t w = 0;
		if (v != 0xe800e800) {
			M32(a + 0x40) = 0x12345678; __asm__ volatile("dsb");
			uint32_t x = M32(a + 0x40);
			M32(a + 0x40) = ~0x12345678u; __asm__ volatile("dsb");
			uint32_t y = M32(a + 0x40);
			M32(a + 0x40) = v;
			w = (x == 0x12345678) | ((y == ~0x12345678u) << 1);
		}
		*o++ = a; *o++ = v; *o++ = w;
	}
	for (uint32_t a = 0x400000; a < 0x10000000; a += 0x400000) {
		*o++ = a; *o++ = M32(a + 0x40); *o++ = 0xffffffff;
	}
	M32(0xe000ed08) = old;
	__asm__ volatile("dsb; isb");
	r.addr = (void *)OUT;
	r.size = (uint8_t *)o - (uint8_t *)OUT;
	return &r;
}
