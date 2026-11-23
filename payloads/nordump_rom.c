/* NOR read through the mask-ROM routine (DMA, optional hardware descrambler) */
#include "spinor.h"
struct res { void *addr; unsigned size; };
static struct res r;
#define ARGS ((volatile uint32_t *)0x11f800)
#define BUF  ((uint8_t *)0x122000)
#define ROMCTX ((volatile uint32_t *)0x100034)
void *entry(void)
{
	uint32_t addr = ARGS[0], len = ARGS[1];
	nor_setup();
	{ static const uint8_t c9f[] = { 0x9f }; uint8_t id[4]; spi_cmd(c9f, 1, id, 3, 0); }
	ROMCTX[0x68 / 4] = ARGS[5];	/* descramble */
	ROMCTX[0x6c / 4] = ARGS[6];	/* CTL bit 9 */
	uint32_t e = 0;
	for (uint32_t o = 0; o < len; o += 512)
		e |= rom_nor_read(addr + o, 512, BUF + o, 0);
	ARGS[2] = e;
	r.addr = BUF; r.size = len;
	return &r;
}
