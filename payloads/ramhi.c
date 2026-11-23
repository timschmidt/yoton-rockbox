#include <stdint.h>
#define M32(a) (*(volatile uint32_t *)(a))
#define OUT ((volatile uint32_t *)0x120000)
struct res { void *addr; unsigned size; };
static struct res r;
static const uint32_t A[] = { 0x20000000, 0x20001000, 0x20010000, 0x20100000, 0x21000000, 0x22000000,
	0x24000000, 0x28000000, 0x30000000, 0x60000000, 0x60100000, 0x64000000, 0x68000000, 0x70000000,
	0x80000000, 0x90000000, 0x10000000, 0x08000000, 0x00400000, 0x00800000, 0x01000000 };
void *entry(void)
{
	volatile uint32_t *o = OUT;
	for (unsigned i = 0; i < sizeof(A) / 4; i++) {
		uint32_t a = A[i] + 0x40, v = M32(a);
		M32(a) = 0x5EED0000 | i; __asm__ volatile("dsb");
		uint32_t x = M32(a);
		M32(a) = v;
		*o++ = A[i]; *o++ = v; *o++ = x;
	}
	r.addr = (void *)OUT; r.size = (uint8_t *)o - (uint8_t *)OUT;
	return &r;
}
