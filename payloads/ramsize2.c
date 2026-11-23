#include <stdint.h>
#define M32(a) (*(volatile uint32_t *)(a))
#define ARGS0 (((volatile uint32_t *)0x11f800)[0])
#define ARGS1 (((volatile uint32_t *)0x11f800)[1])
#define ARGS2 (((volatile uint32_t *)0x11f800)[2])
#define OUT ((volatile uint32_t *)0x120000)
struct res { void *addr; unsigned size; };
static struct res r;
#define LO ARGS0
#define HI ARGS1
#define STEP ARGS2
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
