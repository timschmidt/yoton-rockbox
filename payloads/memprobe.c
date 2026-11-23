#include <stdint.h>
#define M32(a) (*(volatile uint32_t *)(a))
extern void skip_fault(void);
extern volatile uint32_t fault_count;
static uint32_t vt[64] __attribute__((aligned(256)));
struct res { void *addr; unsigned size; };
static struct res r;
#define OUT ((volatile uint32_t *)0x120000)

static const uint32_t extra[] = {
	0x08000000, 0x10000000, 0x14000000, 0x18000000, 0x1c000000,
	0x20000000, 0x20008000, 0x20100000, 0x22000000, 0x24000000, 0x28000000, 0x30000000,
	0x40000000, 0x50000000, 0x60000000, 0x68000000, 0x70000000,
	0x80000000, 0x90000000, 0xa0000000, 0xb0000000,
};

static void probe(uint32_t a, volatile uint32_t **o)
{
	uint32_t f0 = fault_count, v0, v1, s = 0;
	v0 = M32(a);
	v1 = M32(a + 0x1000);
	for (int i = 0; i < 64; i += 4) s += M32(a + 0x100 + i) * (i + 1);
	uint32_t f = fault_count - f0;
	volatile uint32_t *p = *o;
	p[0] = a; p[1] = f; p[2] = v0; p[3] = v1; p[4] = s;
	*o = p + 5;
}

void *entry(void)
{
	uint32_t old = M32(0xe000ed08);
	for (int i = 0; i < 64; i++) vt[i] = ((uint32_t *)old)[i];
	vt[3] = vt[4] = vt[5] = vt[6] = (uint32_t)skip_fault;
	__asm__ volatile("dsb; isb");
	M32(0xe000ed08) = (uint32_t)vt;
	__asm__ volatile("dsb; isb");

	volatile uint32_t *o = OUT + 4;
	OUT[0] = M32(0xe000ed00);	/* CPUID */
	OUT[1] = old;			/* VTOR */
	OUT[2] = M32(0xe000ed88);	/* CPACR */
	OUT[3] = M32(0xe000ed90);	/* MPU_TYPE */
	for (uint32_t a = 0; a < 0x400000; a += 0x8000) probe(a, &o);
	for (unsigned i = 0; i < sizeof(extra) / 4; i++) probe(extra[i], &o);

	M32(0xe000ed08) = old;
	__asm__ volatile("dsb; isb");
	r.addr = (void *)OUT;
	r.size = (uint8_t *)o - (uint8_t *)OUT;
	return &r;
}
