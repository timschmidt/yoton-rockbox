/* microSD bring-up on the ATJ2157 SD0 controller (Yoton YM03 pins 27-33). */
#include <stdint.h>
#define M32(a) (*(volatile uint32_t *)(a))
#define CMU(x)  M32(0xc0001000 + (x))
#define RMU0    M32(0xc0000000)
#define MFP(p)  M32(0xc01c0004 + 4 * (p))
#define SD(x)   M32(0xc0160000 + (x))
#define DMA(ch, x) M32(0xc0070100 + 0x100 * (ch) + (x))
#define DMAG(x) M32(0xc0070000 + (x))
#define ARGS ((volatile uint32_t *)0x11f800)
#define TIMING 0x8f0000

struct res { void *addr; unsigned size; };
static struct res r;
static struct { uint32_t log[48]; uint8_t blk[512]; } out __attribute__((aligned(4)));
static int nlog;
static void L(uint32_t v) { if (nlog < 48) out.log[nlog++] = v; }
static void delay(unsigned n) { for (volatile unsigned i = 0; i < n * 400; i++); }

static int wait_ctl(void)
{
	for (int i = 0; i < 1000000; i++)
		if (!(SD(4) & 0x80)) return 0;
	return -1;
}

/* type: 0 none, 1 R1, 2 R2, 3 R3/R6/R1b */
static int cmd(int idx, uint32_t arg, int type)
{
	SD(0x10) = arg;
	SD(0x0c) = idx;
	SD(4) = TIMING | type | 0x80;
	int e = wait_ctl();
	uint32_t st = SD(8);
	L(idx << 24 | (e & 0xff) << 16 | (st & 0xffff));
	L(SD(0x14)); L(SD(0x18));
	if (e) return -1;
	if (type >= 1 && type <= 3 && (st & 0x8017) && !(type == 3 && !(st & 0x8000) && (st & 0x17) == 0x10)) return -2;
	return 0;
}
static uint32_t r1(void) { return (SD(0x18) & 0xff) << 24 | SD(0x14) >> 8; }

static int read_block(uint32_t lba, void *buf, int hc)
{
	SD(0) |= 0x40;
	DMAG(0) = (1 << 4) | (1 << 20);		/* clear pending ch4 */
	DMA(4, 0) = 0x85;			/* src = SD0 FIFO */
	DMA(4, 8) = 0xc0160028;
	DMA(4, 0x10) = (uint32_t)buf;
	DMA(4, 0x18) = 512;
	SD(0x2c) = 512;
	SD(0x30) = 1;
	SD(0x10) = hc ? lba : lba * 512;
	SD(0x0c) = 17;
	SD(4) = 0x80000000 | TIMING | 0x3f000000 | 4;
	DMA(4, 4) |= 1;
	SD(4) |= 0x80;
	int e = wait_ctl();
	L(0xd0000000 | (e & 0xff) << 16 | (SD(8) & 0xffff));
	int t = 0;
	while ((DMA(4, 4) & 1) && t < 1000000) t++;
	L(0xd1000000 | (t >= 1000000));
	return e;
}

void *entry(void)
{
	nlog = 0;
	/* clocks: SD0 (bit2) + bit0, DMA assumed on */
	CMU(8) |= 5;
	RMU0 &= ~4; delay(10); RMU0 |= 4;
	/* pins: CLK30 drv5, CMD32/D0 33/D1 29/D2 28/D3 27 drv3, function 4 + pull (0x804) */
	MFP(30) = 0x804 | 5 << 12;
	MFP(32) = 0x804 | 3 << 12;
	MFP(33) = 0x804 | 3 << 12;
	MFP(29) = 0x804 | 3 << 12;
	MFP(28) = 0x804 | 3 << 12;
	MFP(27) = 0x804 | 3 << 12;
	CMU(0x14) = 0x41;		/* slow identification clock */
	SD(0) = 0xc1;
	/* 74+ init clocks */
	SD(4) = 0x001f00e8;
	wait_ctl();
	L(0xa0000000 | SD(0)); L(SD(4)); L(SD(8));
	cmd(0, 0, 0);
	int v2 = cmd(8, 0x1aa, 1) == 0;
	L(0xb8000000 | r1());
	uint32_t ocr = 0;
	int i;
	for (i = 0; i < 200; i++) {
		cmd(55, 0, 1);
		cmd(41, v2 ? 0x40ff8000 : 0x00ff8000, 3);
		ocr = r1();
		if (ocr & 0x80000000) break;
		delay(2000);
	}
	nlog = nlog > 12 ? 12 : nlog;	/* keep log short: drop loop noise */
	L(0xb9000000 | i); L(ocr);
	int hc = (ocr >> 30) & 1;
	cmd(2, 0, 2);
	cmd(3, 0, 3);
	uint32_t rca = r1() & 0xffff0000;
	L(0xba000000); L(rca);
	cmd(9, rca, 2);
	cmd(7, rca, 3);
	cmd(55, rca, 1);
	cmd(6, 2, 1);			/* 4-bit bus */
	SD(0) = (SD(0) & ~3) | 1;
	CMU(0x14) = 3;			/* faster clock */
	for (int k = 0; k < 512; k++) out.blk[k] = 0;
	int e = read_block(ARGS[0], out.blk, hc);
	L(0xbb000000 | (e & 0xffff)); L(out.blk[510] | out.blk[511] << 8);
	r.addr = &out; r.size = sizeof(out);
	return &r;
}
