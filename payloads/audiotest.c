/*
 * ATJ2157 DAC bring-up experiment (sequences from vendor AUD_DEV.DRV).
 * ARGS[0] = tone duration in ms, ARGS[1] = variant bits:
 *   bit0: run PA sequence A (DAC+0x2c bits 20-23)
 *   bit1: run PA sequence B (DAC+0x2c bits 16-19)
 *   bit2: 16-bit packed stereo samples (else 32-bit per channel L,R)
 * Plays a 1 kHz square wave by CPU writes to the DAC FIFO (+0x0c), paced
 * by SysTick at 44.1 kHz. Returns register snapshots.
 */
#include <stdint.h>
#define M32(a) (*(volatile uint32_t *)(a))
#define ARGS ((volatile uint32_t *)0x11f800)
#define DAC(x) M32(0xc0180000 + (x))
#define CMU(x) M32(0xc0001000 + (x))
struct res { void *addr; unsigned size; };
static struct res r;
static uint32_t out[24];

static void delay_ms(unsigned ms)
{
	M32(0xe000e014) = 48000 - 1;
	M32(0xe000e018) = 0;
	M32(0xe000e010) = 5;
	while (ms--)
		while (!(M32(0xe000e010) & 0x10000));
}

void *entry(void)
{
	uint32_t ms = ARGS[0], var = ARGS[1];

	/* clocks and reset (vendor 0x22b2 / 0x1000) */
	M32(0xc0000004) &= ~1u;
	for (volatile int i = 0; i < 20; i++);
	M32(0xc0000004) |= 1 | 4;	/* bit2: DAC reset release */
	CMU(0x0c) |= 1 | 4;
	CMU(0x88) &= ~0x80u;
	CMU(0x88) |= 0x400;
	/* 44.1 kHz family, divider index 3 (176/44 = 4) */
	CMU(0x80) = (CMU(0x80) & ~0x3c7u) | (1 << 6) | 3;
	CMU(0x80) |= 0x33000000;

	/* DAC core (0x1000, 0x195c, 0x1b96) */
	DAC(0) &= ~0x1e00u;
	DAC(0) |= 1;
	DAC(0) |= 0x100;
	DAC(4) &= ~0x11u;
	DAC(4) |= 1;
	DAC(0) |= 0x10;
	DAC(0x0c) = 0;
	DAC(4) |= 0x12;
	DAC(0) = (DAC(0) & ~0xcu) | 0x103;

	/* analog (0x1a06) */
	DAC(0x28) = (DAC(0x28) | 0x80 | 0x8000000) & ~(0x7000u | 0x30000u);

	/* PA power-up (0x1092) */
	if (var & 1) {
		DAC(0x2c) |= 0x200000; delay_ms(100);
		DAC(0x2c) = (DAC(0x2c) & ~0xc00000u) | 0x100000 | 0x800000; delay_ms(100);
		DAC(0x2c) &= ~0x200000u;
	}
	if (var & 2) {
		DAC(0x2c) |= 0x20000; delay_ms(200);
		DAC(0x2c) = (DAC(0x2c) & ~0xc0000u) | 0x10000 | 0x80000; delay_ms(100);
		DAC(0x2c) &= ~0x20000u;
	}

	out[0] = CMU(0x0c); out[1] = CMU(0x80); out[2] = CMU(0x88);
	out[3] = DAC(0); out[4] = DAC(4); out[5] = DAC(8); out[6] = DAC(0x10);
	out[7] = DAC(0x14); out[8] = DAC(0x28); out[9] = DAC(0x2c);

	/* 1 kHz square: 22 samples high, 22 low; one frame per 1088 cycles */
	M32(0xe000e014) = 1088 - 1;
	M32(0xe000e018) = 0;
	M32(0xe000e010) = 5;
	uint32_t frames = ms * 44, overruns = 0;
	for (uint32_t n = 0; n < frames; n++) {
		int hi = (n / 22) & 1;
		if (var & 4) {
			DAC(0x0c) = hi ? 0x60006000 : 0xa000a000;
		} else {
			DAC(0x0c) = hi ? 0x60000000 : 0xa0000000;
			DAC(0x0c) = hi ? 0x60000000 : 0xa0000000;
		}
		if (M32(0xe000e010) & 0x10000) overruns++;
		while (!(M32(0xe000e010) & 0x10000));
	}
	M32(0xe000e010) = 0;
	out[10] = overruns;
	out[11] = DAC(4); out[12] = DAC(8); out[13] = DAC(0x10);
	r.addr = out; r.size = 14 * 4;
	return &r;
}
