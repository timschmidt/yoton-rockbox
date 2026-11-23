#include <stdint.h>
#define M32(a) (*(volatile uint32_t *)(a))
struct res { void *addr; unsigned size; };
static struct res r; static uint32_t o[12];
void *entry(void)
{
	o[0] = M32(0xe000e010); o[1] = M32(0xe000e01c);	/* CTRL, CALIB */
	M32(0xe000e014) = 0xffffff; M32(0xe000e018) = 0;
	M32(0xe000e010) = 5;
	o[2] = M32(0xe000e018);
	for (volatile int i = 0; i < 100000; i++);
	o[3] = M32(0xe000e018);
	M32(0xe000e010) = 1;				/* external ref clock */
	M32(0xe000e018) = 0;
	o[4] = M32(0xe000e018);
	for (volatile int i = 0; i < 100000; i++);
	o[5] = M32(0xe000e018);
	o[6] = M32(0xe000e010);
	M32(0xe000e010) = 0;
	r.addr = o; r.size = 28; return &r;
}
