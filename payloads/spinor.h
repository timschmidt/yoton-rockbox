/* Minimal SPI0 NOR access (register semantics from the ATJ2157 mask ROM) */
#include "rom.h"
static int spi_wait(uint32_t mask, uint32_t val)
{
	for (int i = 0; i < 200000; i++)
		if ((M32(SPI0_STAT) & mask) == val) return 0;
	return 1;
}
static inline void spi_mode(int m) { M32(SPI0_CTL) = (M32(SPI0_CTL) & ~3) | m; }
static inline void spi_cs(int hi) { if (hi) M32(SPI0_CTL) |= 8; else M32(SPI0_CTL) &= ~8; }
static int spi_cmd(const uint8_t *cmd, int n, uint8_t *rx, int rn, int keep_cs)
{
	int err = 0;
	spi_cs(1); spi_cs(0);
	spi_mode(0); spi_mode(2);
	for (int i = 0; i < n; i++) M32(SPI0_TX) = cmd[i];
	err |= spi_wait(0x10, 0x10); err |= spi_wait(0x40, 0);
	spi_mode(0);
	while (rn > 0) {
		int c = rn > 0x1000 ? 0x1000 : rn;
		M32(SPI0_CTL) &= ~0x30; M32(SPI0_CTL) |= 0x30;
		M32(SPI0_BC) = c;
		spi_mode(1);
		for (int i = 0; i < c; i++) {
			err |= spi_wait(4, 0);
			*rx++ = M32(SPI0_RX);
		}
		spi_mode(0);
		rn -= c;
	}
	if (!keep_cs) spi_cs(1);
	return err;
}
static int nor_read(uint32_t addr, void *dst, int len)
{
	uint8_t c[4] = { 0x03, addr >> 16, addr >> 8, addr };
	return spi_cmd(c, 4, dst, len, 0);
}
static void nor_setup(void)
{
	static int done __attribute__((section(".data"))) = 0;
	if (!done) { rom_spi_boot_init(); rom_spi_pins_on(0); rom_nor_reset(); done = 1; }
}
