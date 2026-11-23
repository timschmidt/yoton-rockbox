/*
 * ATJ2157 DAC test #2: faithful copy of the vendor PA power-up routine
 * (AUD_DEV.DRV bank code at 0x4000), then a 1 kHz square wave written with
 * the vendor FIFO protocol (wait DAC+8 bit7, write L and R 32-bit samples
 * to +0x0c, acknowledge with 0x80 to +8).
 * ARGS[0] = tone ms, ARGS[1] = volume (0-255)
 */
#include <stdint.h>
#define M32(a) (*(volatile uint32_t *)(a))
#define ARGS ((volatile uint32_t *)0x11f800)
#define DAC(x) M32(0xc0180000 + (x))
#define CMU(x) M32(0xc0001000 + (x))
struct res { void *addr; unsigned size; };
static struct res r;
static uint32_t out[24];
static uint32_t timeouts;

static void fifo_put(uint32_t l, uint32_t rr, int two)
{
	uint32_t t = 0;
	while (!(DAC(8) & 0x80))
		if (++t > 20000) { timeouts++; break; }
	DAC(0x0c) = l;
	if (two) DAC(0x0c) = rr;
	DAC(8) = 0x80;
}

static void fill(uint32_t v, int n, int two)
{
	for (int i = 0; i < n; i++) fifo_put(v, v, two);
}

void *entry(void)
{
	uint32_t ms = ARGS[0], vol = ARGS[1] & 0xff, mode = ARGS[2];
	timeouts = 0;

	/* analog power + audio PLL (vendor bank 0x800) */
	M32(0xc0010018) = (M32(0xc0010018) | 0x40) & ~0x20u;
	M32(0xc0000118) = (M32(0xc0000118) & ~0x1fu) | 0x1c;
	M32(0xc000011c) = (M32(0xc000011c) & ~0x1fu) | 0x14;
	for (volatile int i = 0; i < 100000; i++);
	/* DAC clock: CMU+0x88 = enable | 44.1k family | divider index 3 */
	CMU(0x0c) |= 1 | 4;
	CMU(0x88) = (CMU(0x88) & ~0xd7u) | 0x400 | (1 << 6) | 3;
	/* reset pulse */
	M32(0xc0000004) &= ~5u;
	for (volatile int i = 0; i < 1000; i++);
	M32(0xc0000004) |= 1 | 4;

	/* vendor PA power-up (bank 0x4000) */
	uint32_t mid = 0x7fffffff, step = 0xffffffffu / 4800, v = 0xffffffffu >> 1;
	DAC(0x28) |= 0x80;
	DAC(0x28) &= ~0x300u;
	DAC(0) |= 1; DAC(0) |= 0x100; DAC(0) |= 0x10;
	DAC(0x28) |= 3; DAC(0x28) |= 0x14; DAC(0x28) |= 0x40;
	DAC(4) = 0;
	DAC(4) = (DAC(4) | 1) & ~0x36u;
	fill(mid, 192, 0);
	DAC(0x2c) |= 0x101; DAC(0x2c) |= 0x1010;
	fill(mid, 192, 0);
	DAC(0x2c) |= 0x202; DAC(0x2c) |= 0x404;
	for (int n = 4800; n > 0; n--) { fifo_put(v, v, 1); v -= step; }
	v += step;
	fill(v, 192, 0);
	DAC(0x2c) |= 0x808;
	fill(v, 192, 0);
	DAC(0x28) |= 0x28;
	fill(v, 192, 0);
	DAC(0x2c) &= ~0x1010u;
	for (int n = 4800; n > 0; n--) { fifo_put(v, v, 1); v += step; }
	v -= step;
	fill(v, 192, 0);
	DAC(0x2c) &= ~0x202u; DAC(0x2c) &= ~0x404u;
	fill(0, 192, 0);
	DAC(0x2c) &= ~0x101u;
	DAC(0x0c) |= 0x800;		/* as the vendor code does */
	DAC(0x28) = (DAC(0x28) & ~0x30000u) | 0x10000;
	DAC(0x28) |= 0x40000;
	DAC(0x28) |= 0x300;
	DAC(4) &= ~1u;

	/* volume, unmuted */
	DAC(0x14) = (DAC(0x14) & ~0xff0ffu & ~0x800u) | 0x33700 | vol;
	DAC(0x18) = (DAC(0x18) & ~0xff0ffu & ~0x800u) | 0x33700 | vol;


	/* part 1: known-audible PIO square wave, 32-bit per channel */
	DAC(4) |= 1;
	for (uint32_t n = 0; n < 44100; n++) {
		uint32_t s = ((n / 50) & 1) ? 0x30000000 : 0xd0000000;
		fifo_put(s, s, 1);
	}
	/* 0.7 s of silence */
	for (uint32_t n = 0; n < 30000; n++)
		fifo_put(0, 0, 1);

	/* part 2: Rockbox driver path: 16-bit stereo via DMA ch5 */
	static int16_t buf[2 * 4410] __attribute__((aligned(4)));
	for (int n = 0; n < 4410; n++) {
		int16_t s = ((n / 50) & 1) ? 0x3000 : -0x3000;
		buf[2 * n] = s; buf[2 * n + 1] = s;
	}
	if (mode & 1)
		DAC(0x0c) |= 0x800;		/* vendor quirk */
	DAC(4) &= ~0x11u;
	DAC(4) |= 1;
	DAC(4) |= 0x12;
	M32(0xc0070004) &= ~(1u << 5);		/* no DMA irq */
	for (uint32_t rep = 0; rep < ms / 100; rep++) {
		M32(0xc0070000) = (1 << 5) | (1 << 21);
		M32(0xc0070600) = 0x00148b00;
		M32(0xc0070608) = (uint32_t)buf;
		M32(0xc0070610) = 0xc018000c;
		M32(0xc0070618) = sizeof(buf);
		M32(0xc0070604) = 1;
		uint32_t t = 0;
		while ((M32(0xc0070604) & 1) && ++t < 10000000);
		if (t >= 10000000) timeouts++;
	}
	out[0] = timeouts;
	out[1] = DAC(0); out[2] = DAC(4); out[3] = DAC(8); out[4] = DAC(0x14);
	out[5] = DAC(0x18); out[6] = DAC(0x28); out[7] = DAC(0x2c);
	r.addr = out; r.size = 8 * 4;
	return &r;
}
