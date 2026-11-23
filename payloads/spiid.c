#include "rom.h"
struct res { void *addr; unsigned size; };
static struct res r;
#define OUT ((volatile uint32_t *)0x120000)

static int wait(uint32_t mask, uint32_t val)
{
	for (int i = 0; i < 200000; i++)
		if ((M32(SPI0_STAT) & mask) == val) return 0;
	return 1;
}
static void ctl_mode(int m) { M32(SPI0_CTL) = (M32(SPI0_CTL) & ~3) | m; }
static void cs(int hi) { if (hi) M32(SPI0_CTL) |= 8; else M32(SPI0_CTL) &= ~8; }

/* send n command bytes, then read rn bytes */
static int xfer(const uint8_t *cmd, int n, uint8_t *rx, int rn)
{
	int err = 0;
	cs(1); cs(0);
	ctl_mode(0); ctl_mode(2);
	for (int i = 0; i < n; i++) M32(SPI0_TX) = cmd[i];
	err |= wait(0x10, 0x10); err |= wait(0x40, 0) << 1;
	ctl_mode(0);
	if (rn) {
		M32(SPI0_CTL) &= ~0x30; M32(SPI0_CTL) |= 0x30;
		M32(SPI0_BC) = rn;
		ctl_mode(1);
		for (int i = 0; i < rn; i++) {
			err |= wait(4, 0) << 2;
			rx[i] = M32(SPI0_RX);
		}
		ctl_mode(0);
	}
	cs(1);
	return err;
}

void *entry(void)
{
	volatile uint32_t *o = OUT;
	uint8_t id[8];
	static const uint8_t c9f[] = { 0x9f };
	static const uint8_t c9f_nand[] = { 0x9f, 0x00 };
	static const uint8_t c03[] = { 0x03, 0, 0, 0 };
	rom_spi_boot_init();
	for (int g = 0; g < 3; g++) {
		rom_spi_pins_on(g);
		rom_nor_reset();
		*o++ = 0x100 + g;
		*o++ = M32(SPI0_CTL);
		*o++ = xfer(c9f, 1, id, 4);
		*o++ = id[0] | id[1] << 8 | id[2] << 16 | id[3] << 24;
		*o++ = xfer(c9f_nand, 2, id, 4);
		*o++ = id[0] | id[1] << 8 | id[2] << 16 | id[3] << 24;
		*o++ = xfer(c03, 4, id, 8);
		*o++ = id[0] | id[1] << 8 | id[2] << 16 | id[3] << 24;
		*o++ = id[4] | id[5] << 8 | id[6] << 16 | id[7] << 24;
		rom_spi_pins_off(g);
	}
	r.addr = (void *)OUT;
	r.size = (uint8_t *)o - (uint8_t *)OUT;
	return &r;
}
