#include "spinor.h"
struct res { void *addr; unsigned size; };
static struct res r;
#define ARGS ((volatile uint32_t *)0x11f800)
#define BUF  ((uint8_t *)0x122000)
void *entry(void)
{
	uint32_t addr = ARGS[0], len = ARGS[1];
	nor_setup();
	{ static const uint8_t c9f[] = { 0x9f }; uint8_t id[4] = {0}; ARGS[4] = spi_cmd(c9f, 1, id, 3, 0); ARGS[3] = id[0] | id[1] << 8 | id[2] << 16; }
	if (ARGS[5]) M32(SPI0_CTL) |= ARGS[5]; else M32(SPI0_CTL) &= ~0x1000;
	ARGS[2] = nor_read(addr, BUF, len);
	M32(SPI0_CTL) &= ~0x1000;
	r.addr = BUF; r.size = len;
	return &r;
}
