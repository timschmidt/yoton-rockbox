/*
 * NOR read speed test: SPI0 fast-read (0x0B) into RAM through DMA channel 0,
 * the way the mask ROM does it (0x2224), at several SPI clock settings.
 * Each DMA read is compared with a PIO read at the ROM's clock (0x101).
 *
 * ARGS[0] = NOR address, ARGS[1] = number of clock values, ARGS[2..] = the
 * low 10 bits written to CMU+0x20. ARGS[10] = destination (0 = SRAM0
 * buffer, 1 = SRAM2 slot 0 through its DMA alias).
 * Result per value: { clk, cycles, mismatches, dma_err }.
 */
#include "spinor.h"
struct res { void *addr; unsigned size; };
static struct res r;
static uint32_t out[4 * 8];
#define ARGS    ((volatile uint32_t *)0x11f800)
#define REF     ((uint8_t *)0x122000)
#define BUF     ((uint8_t *)0x124000)
#define LEN     0x2000
#define CMU_SPI (0xc0001020)
#define DMA(o)  M32(0xc0070000 + (o))

static int dma_read(uint32_t addr, uint32_t dst_dma, uint32_t len)
{
	spi_cs(1); spi_cs(0);
	spi_mode(0); spi_mode(2);
	M32(SPI0_TX) = 0x0b;
	M32(SPI0_TX) = addr >> 16;
	M32(SPI0_TX) = addr >> 8;
	M32(SPI0_TX) = addr;
	M32(SPI0_TX) = 0;			/* dummy */
	int err = spi_wait(0x40, 0) | spi_wait(0x10, 0x10);
	M32(SPI0_BC) = len;
	DMA(0x108) = SPI0_RX;			/* ch0 src */
	DMA(0x110) = dst_dma;			/* ch0 dst */
	DMA(0x118) = len;			/* ch0 len */
	DMA(0x100) = 0x00200088;		/* ch0 mode: SPI RX -> memory */
	spi_mode(0);
	M32(SPI0_CTL) &= ~0x30; M32(SPI0_CTL) |= 0x30;
	M32(SPI0_CTL) |= 0x80000040;		/* SPI DMA request enable */
	DMA(0x104) = 1;
	spi_mode(1);
	int t;
	for (t = 0; t < 4000000 && (DMA(0x104) & 1); t++);
	if (t >= 4000000) err |= 2;
	spi_cs(1);
	M32(SPI0_CTL) &= 0x7fffefbc;
	return err;
}

void *entry(void)
{
	uint32_t addr = ARGS[0], n = ARGS[1], where = ARGS[10];
	nor_setup();
	{ static const uint8_t c9f[] = { 0x9f }; uint8_t id[4]; spi_cmd(c9f, 1, id, 3, 0); }
	uint32_t base = M32(CMU_SPI);
	M32(CMU_SPI) = (base & ~0x3ffu) | 0x101;
	nor_read(addr, REF, LEN);

	M32(0xe000e014) = 0xffffff;
	M32(0xe000e018) = 0;
	M32(0xe000e010) = 5;			/* SysTick on core clock */

	uint8_t *dst = where ? (uint8_t *)0x200000 : BUF;
	uint32_t dst_dma = where ? 0x10000000 : (uint32_t)BUF;
	for (uint32_t i = 0; i < n && i < 8; i++) {
		uint32_t clk = ARGS[2 + i];
		M32(CMU_SPI) = (base & ~0x3ffu) | clk;
		for (int k = 0; k < LEN; k++) dst[k] = 0x5a;
		uint32_t t0 = M32(0xe000e018);
		int err = dma_read(addr, dst_dma, LEN);
		uint32_t t1 = M32(0xe000e018);
		uint32_t bad = 0;
		for (int k = 0; k < LEN; k++) bad += dst[k] != REF[k];
		out[4 * i + 0] = clk;
		out[4 * i + 1] = (t0 - t1) & 0xffffff;
		out[4 * i + 2] = bad;
		out[4 * i + 3] = err;
	}
	M32(CMU_SPI) = (base & ~0x3ffu) | 0x101;
	r.addr = out; r.size = 16 * (n > 8 ? 8 : n);
	return &r;
}
