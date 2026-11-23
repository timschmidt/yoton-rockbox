/*
 * SPI NOR erase/program for the Yoton (ATJ2157 SPI0, Zbit 8 MB).
 * ARGS: [0]=op (1 erase 4K, 2 program <=256 bytes from DATA), [1]=addr, [2]=len
 * Writes are confined to WR_LO..WR_HI as a safety net.
 */
#include "spinor.h"
struct res { void *addr; unsigned size; };
static struct res r;
static uint32_t out[4];
#define ARGS ((volatile uint32_t *)0x11f800)
#define DATA ((const uint8_t *)0x122000)
#define WR_LO 0x000000   /* sector 0 only: boot record install */
#define WR_HI 0x001000

static void tx_bytes(const uint8_t *p, int n)
{
	for (int i = 0; i < n; i++) {
		M32(SPI0_TX) = p[i];
		spi_wait(0x10, 0x10);
	}
	spi_wait(0x40, 0);
}

static void cmd_simple(uint8_t c)
{
	spi_cs(1); spi_cs(0);
	spi_mode(0); spi_mode(2);
	tx_bytes(&c, 1);
	spi_mode(0);
	spi_cs(1);
}

static uint8_t read_status(void)
{
	static const uint8_t c = 0x05;
	uint8_t s = 0xff;
	spi_cmd(&c, 1, &s, 1, 0);
	return s;
}

static int wait_ready(uint32_t loops)
{
	while (loops--)
		if (!(read_status() & 1)) return 0;
	return 1;
}

void *entry(void)
{
	uint32_t op = ARGS[0], addr = ARGS[1], len = ARGS[2];
	nor_setup();
	{ static const uint8_t c9f[] = { 0x9f }; uint8_t id[4]; spi_cmd(c9f, 1, id, 3, 0); out[3] = id[0] | id[1] << 8 | id[2] << 16; }
	out[0] = 0;
	if (addr < WR_LO || addr + (op == 1 ? 0x1000 : len) > WR_HI || (op == 2 && (len > 256 || ((addr & 0xff) + len) > 256))) {
		out[0] = 0xbad;
		goto done;
	}
	cmd_simple(0x06);				/* WREN */
	{
		uint8_t c[4] = { op == 1 ? 0x20 : 0x02, addr >> 16, addr >> 8, addr };
		spi_cs(1); spi_cs(0);
		spi_mode(0); spi_mode(2);
		tx_bytes(c, 4);
		if (op == 2) tx_bytes(DATA, len);
		spi_mode(0);
		spi_cs(1);
	}
	out[0] = wait_ready(2000000) ? 0x7173 : 0;	/* timeout marker */
done:
	out[1] = read_status();
	out[2] = op;
	r.addr = out; r.size = 16;
	return &r;
}
