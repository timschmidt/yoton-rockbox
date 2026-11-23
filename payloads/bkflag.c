/*
 * Battery-backed boot-flag block (0xc0020000), access protocol from the mask
 * ROM (0x82c). ARGS[0]: 0 = read only, 1 = clear bit 3 (ADFU request) of
 * word 3, 2 = set bit 3. Returns {ctl, regs +0x0c..+0x28 before, after}.
 */
#include <stdint.h>
#define M32(a) (*(volatile uint32_t *)(a))
#define ARGS ((volatile uint32_t *)0x11f800)
#define BK(x) M32(0xc0020000 + (x))
struct res { void *addr; unsigned size; };
static struct res r; static uint32_t o[24];
static void dly(int n) { for (volatile int i = 0; i < n * 1000; i++); }

void *entry(void)
{
	uint32_t op = ARGS[0];
	int n = 0;
	BK(4) &= ~0x200u; dly(5);
	BK(4) &= ~0x8000u; dly(10);
	BK(0) |= 4; dly(10);
	for (int i = 0; i < 4096 && !(BK(0) & 1); i++);
	for (int i = 0; i < 5 && !(BK(0) & 2); i++) { BK(0x0c) = 0x004b4559; dly(10); }
	o[n++] = BK(0);
	for (int i = 0; i < 8; i++) o[n++] = BK(0x0c + 4 * i);
	if (op == 1) BK(0x18) &= ~8u;
	if (op == 2) BK(0x18) |= 8u;
	dly(10);
	for (int i = 0; i < 8; i++) o[n++] = BK(0x0c + 4 * i);
	BK(0) &= ~4u; dly(5);
	BK(4) |= 0x200; dly(5);
	r.addr = o; r.size = n * 4; return &r;
}
