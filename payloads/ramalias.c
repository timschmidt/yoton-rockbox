#include <stdint.h>
#define M32(a) (*(volatile uint32_t *)(a))
#define OUT ((volatile uint32_t *)0x120000)
struct res { void *addr; unsigned size; };
static struct res r;
static const uint32_t A[] = { 0x158100, 0x200100, 0x300100, 0x130100, 0x138100, 0x150100, 0x140100 };
void *entry(void)
{
	volatile uint32_t *o = OUT;
	int n = sizeof(A) / 4;
	uint32_t save[8];
	for (int i = 0; i < n; i++) save[i] = M32(A[i]);
	for (int i = 0; i < n; i++) M32(A[i]) = 0xC0DE0000 + i;
	for (int i = 0; i < n; i++) *o++ = M32(A[i]);
	for (int i = 0; i < n; i++) M32(A[i]) = save[i];
	r.addr = (void *)OUT; r.size = (uint8_t *)o - (uint8_t *)OUT;
	return &r;
}
