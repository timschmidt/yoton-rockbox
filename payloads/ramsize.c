#include <stdint.h>
#define M32(a) (*(volatile uint32_t *)(a))
#define OUT ((volatile uint32_t *)0x120000)
struct res { void *addr; unsigned size; };
static struct res r;
#define LO 0x180000
#define HI 0x800000
#define STEP 0x4000
void *entry(void)
{
	volatile uint32_t *o = OUT;
	for (uint32_t a = LO; a < HI; a += STEP) M32(a + 0x80) = 0xA5000000 | (a >> 8);
	__asm__ volatile("dsb");
	for (uint32_t a = LO; a < HI; a += STEP) {
		uint32_t v = M32(a + 0x80);
		*o++ = v;
	}
	r.addr = (void *)OUT; r.size = (uint8_t *)o - (uint8_t *)OUT;
	return &r;
}
