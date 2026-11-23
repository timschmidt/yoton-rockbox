/*
 * DAC DMA format probe: after the audio bring-up, DMA a buffer to the DAC
 * FIFO with mode = 0x40000 | r6<<20 | r7<<16 | 0x8b00 (vendor 0x199c) and
 * time the transfer. ARGS[0] = r6 | r7<<4 | fifo_bit7<<8, ARGS[1] = bytes.
 * Returns cycles (SysTick @48 MHz, 24-bit wraps counted).
 */
#include <stdint.h>
#define M32(a) (*(volatile uint32_t *)(a))
#define ARGS ((volatile uint32_t *)0x11f800)
#define DAC(x) M32(0xc0180000 + (x))
#define CMU(x) M32(0xc0001000 + (x))
#define DMA(ch, x) M32(0xc0070100 + 0x100 * (ch) + (x))
#define BUF ((volatile uint16_t *)0x124000)
struct res { void *addr; unsigned size; };
static struct res r; static uint32_t o[12];

void *entry(void)
{
	uint32_t v = ARGS[0], bytes = ARGS[1];
	uint32_t r6 = v & 0xf, r7 = (v >> 4) & 0xf, fb = (v >> 8) & 1;

	/* square wave, 16-bit stereo, 1 kHz */
	for (uint32_t i = 0; i < bytes / 2; i++)
		BUF[i] = ((i / 2 / 22) & 1) ? 0x3000 : 0xd000;

	/* FIFO control as vendor 0x195c with arg fb */
	DAC(4) &= ~0x11u;
	DAC(4) |= 1;
	DAC(0) |= 0x10;
	DAC(4) |= 0x12 | (fb << 7);

	DMA(5, 4) = 0;
	DMA(5, 0) = 0x40000 | (r6 << 20) | (r7 << 16) | 0x8b00;
	DMA(5, 8) = 0x124000;
	DMA(5, 0x10) = 0xc018000c;
	DMA(5, 0x18) = bytes;
	M32(0xe000e014) = 0xffffff; M32(0xe000e018) = 0; M32(0xe000e010) = 5;
	M32(0xc0070000) = (1 << 5) | (1 << 21);	/* clear pending */
	M32(0xc0070004) |= (1 << 5) | (1 << 21);	/* irq enable */
	M32(0xe000e280) = 0xffffffff; M32(0xe000e284) = 0xffffffff;	/* clear NVIC pending */
	DMA(5, 4) = 1;
	uint32_t wraps = 0, t_half = 0, t_full = 0;
	while (wraps < 40) {
		uint32_t now = wraps * 0x1000000 + (0xffffff - M32(0xe000e018));
		if (M32(0xe000e010) & 0x10000) wraps++;
		uint32_t pd = M32(0xc0070000);
		if (!t_half && (pd & (1 << 5))) t_half = now;
		if (!t_full && (pd & (1 << 21))) t_full = now;
		if (t_half && t_full) break;
	}
	M32(0xe000e010) = 0;
	o[0] = t_full; o[1] = t_half; o[2] = M32(0xc0070000); o[3] = M32(0xe000e200); o[4] = M32(0xe000e204);
	M32(0xc0070004) &= ~((1u << 5) | (1u << 21));
	M32(0xc0070000) = (1 << 5) | (1 << 21);
	M32(0xe000e280) = 0xffffffff; M32(0xe000e284) = 0xffffffff;
	DMA(5, 4) = 0;
	r.addr = o; r.size = 20; return &r;
}
