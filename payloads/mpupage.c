/*
 * Validate the demand-paging mechanism planned for the Rockbox main firmware:
 *  1. code in SRAM2 executes from its 1 MB alias addresses
 *  2. MPU deny + per-page allow regions raise MemManage on a non-resident
 *     alias; the handler "maps" the page (moves the allow region) and the
 *     faulting instruction/data access is retried transparently.
 */
#include <stdint.h>
#define M32(a) (*(volatile uint32_t *)(a))
#define MPU_CTRL M32(0xe000ed94)
#define MPU_RNR  M32(0xe000ed98)
#define MPU_RBAR M32(0xe000ed9c)
#define MPU_RASR M32(0xe000eda0)
#define SHCSR    M32(0xe000ed24)
#define CFSR     M32(0xe000ed28)
#define MMFAR    M32(0xe000ed34)

struct res { void *addr; unsigned size; };
static struct res r;
static volatile uint32_t out[24];
static uint32_t vt[64] __attribute__((aligned(256)));
volatile uint32_t mm_faults, mm_last_addr, mm_last_cfsr;
extern void mm_entry(void);

/* position-independent test function: returns 0x5A5A0000 + arg */
__attribute__((naked, noinline, section(".text.pic")))
static uint32_t pic_func(uint32_t a)
{
	__asm__ volatile(
		"ldr r1, 1f\n"
		"add r0, r0, r1\n"
		"bx lr\n"
		".align 2\n"
		"1: .word 0x5a5a0000\n");
}

static void mpu_region(int n, uint32_t base, int size_log2, int ap, int xn)
{
	MPU_RNR = n;
	MPU_RBAR = base & ~31u;
	MPU_RASR = (xn << 28) | (ap << 24) | ((size_log2 - 1) << 1) | 1;
}

/* called from mm_entry with the faulting address; maps that 8 KB page */
void mm_handler(uint32_t *frame)
{
	uint32_t cfsr = CFSR;
	uint32_t addr = (cfsr & 0x80) ? MMFAR : frame[6];	/* MMARVALID ? data : instr */
	mm_faults++;
	mm_last_addr = addr;
	mm_last_cfsr = cfsr;
	CFSR = cfsr & 0xff;				/* clear MMFSR */
	if (mm_faults > 8 || addr < 0x200000 || addr >= 0x300000) {
		/* not ours: disable MPU so the access succeeds, flag it */
		MPU_CTRL = 0;
		mm_faults |= 0x1000;
		return;
	}
	mpu_region(1, addr & ~0x1fffu, 13, 3, 0);	/* 8 KB allow */
	__asm__ volatile("dsb; isb");
}

__asm__(
	".thumb_func\n"
	".global mm_entry\n"
	"mm_entry:\n"
	"  tst lr, #4\n"
	"  ite eq\n"
	"  mrseq r0, msp\n"
	"  mrsne r0, psp\n"
	"  push {r4, lr}\n"
	"  bl mm_handler\n"
	"  pop {r4, pc}\n");

void *entry(void)
{
	volatile uint32_t *o = out;
	uint32_t old_vt = M32(0xe000ed08);
	/* copy the test function into SRAM2 slot 1 (phys 0x202000) */
	const uint16_t *src = (const uint16_t *)((uint32_t)pic_func & ~1u);
	volatile uint16_t *dst = (volatile uint16_t *)0x202000;
	for (int i = 0; i < 8; i++) dst[i] = src[i];
	M32(0x203000) = 0xdada1234;			/* data word in slot 1 */
	__asm__ volatile("dsb; isb");

	/* 1. execute from several aliases of slot 1 */
	static const uint32_t va[4] = { 0x202000, 0x20a000, 0x2c2000, 0x2fa000 };
	for (int i = 0; i < 4; i++)
		*o++ = ((uint32_t (*)(uint32_t))(va[i] | 1))(i);

	/* 2. MPU paging */
	for (int i = 0; i < 64; i++) vt[i] = ((uint32_t *)old_vt)[i];
	vt[4] = (uint32_t)mm_entry;			/* MemManage */
	M32(0xe000ed08) = (uint32_t)vt;
	SHCSR |= 1 << 16;				/* MEMFAULTENA */
	mm_faults = 0;
	mpu_region(0, 0x200000, 20, 0, 1);		/* 1 MB: no access, XN */
	mpu_region(1, 0x20a000, 13, 3, 0);		/* page at VA 0x20a000 resident */
	for (int n = 2; n < 8; n++) { MPU_RNR = n; MPU_RASR = 0; }
	MPU_CTRL = 5;					/* ENABLE | PRIVDEFENA */
	__asm__ volatile("dsb; isb");

	*o++ = ((uint32_t (*)(uint32_t))(0x20a000 | 1))(0x10);	/* resident: no fault */
	*o++ = mm_faults;
	*o++ = ((uint32_t (*)(uint32_t))(0x2c2000 | 1))(0x20);	/* fault -> map -> run */
	*o++ = mm_faults; *o++ = mm_last_addr; *o++ = mm_last_cfsr;
	*o++ = M32(0x2e3000);					/* data fault -> map */
	*o++ = mm_faults; *o++ = mm_last_addr; *o++ = mm_last_cfsr;

	MPU_CTRL = 0;
	__asm__ volatile("dsb; isb");
	M32(0xe000ed08) = old_vt;
	r.addr = (void *)out; r.size = (uint8_t *)o - (uint8_t *)out;
	return &r;
}
