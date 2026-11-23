/* ARGS[0] = address, ARGS[1] = and-mask, ARGS[2] = or-value. Returns old, new. */
#include <stdint.h>
#define M32(a) (*(volatile uint32_t *)(a))
#define ARGS ((volatile uint32_t *)0x11f800)
struct res { void *addr; unsigned size; };
static struct res r; static uint32_t o[2];
void *entry(void)
{
	volatile uint32_t *p = (volatile uint32_t *)ARGS[0];
	o[0] = *p; *p = (*p & ARGS[1]) | ARGS[2]; o[1] = *p;
	r.addr = o; r.size = 8; return &r;
}
