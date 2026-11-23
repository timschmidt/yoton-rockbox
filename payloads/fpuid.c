/* FPU presence: CPACR, MVFR0/1 */
#include <stdint.h>
#define M32(a) (*(volatile uint32_t *)(a))
struct res { void *addr; unsigned size; };
static struct res r; static uint32_t o[4];
void *entry(void)
{
	o[0] = M32(0xe000ed88);		/* CPACR */
	o[1] = M32(0xe000ef40);		/* MVFR0 */
	o[2] = M32(0xe000ef44);		/* MVFR1 */
	o[3] = M32(0xe000ed00);		/* CPUID */
	r.addr = o; r.size = 16; return &r;
}
