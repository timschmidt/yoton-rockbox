/*
 * Yoton YM03 NOR boot stage 1: a 512-byte boot record (type 0xA5) at NOR 0.
 * It must stay within 0x000-0x1FF: the vendor boot record it replaces is a
 * 512-byte one too, and the vendor's next stage starts at NOR 0x200.
 *
 * Loads stage 2 (payloads/ym03stub.c, 1 KiB at NOR 0x7DF400) to 0x102400
 * and enters it; if stage 2 is missing, falls into the boot ROM's USB
 * recovery (ADFU).
 */
#include <stdint.h>
#define M32(a) (*(volatile uint32_t *)(a))
#define ROMCTX ((volatile uint32_t *)0x100034)
#define ROMFN(addr, ret, name, args) static ret (* const name) args = (ret (*) args)((addr) | 1)
ROMFN(0x11d4, void, rom_cleanup1, (void));
ROMFN(0x1258, void, rom_cleanup2, (void));
ROMFN(0x1144, void, rom_adfu, (void));

#define S2_NOR   0x7df400
#define S2_ADDR  0x102400
#define S2_MAGIC 0x32545359     /* "YST2" */

/* byte-polled NOR READ (0x03) on SPI0 as the boot ROM left it set up; the
 * ROM's own DMA read misbehaves outside its boot path (USB recovery) */
#define SPI_CTL  0xc00a0000
#define SPI_STAT 0xc00a0004
#define SPI_TX   0xc00a0008
#define SPI_RX   0xc00a000c
#define SPI_BC   0xc00a0010
static int spi_wait(uint32_t mask, uint32_t val)
{
	for (int i = 0; i < 200000; i++)
		if ((M32(SPI_STAT) & mask) == val)
			return 0;
	return 1;
}
static void spi_mode(int m) { M32(SPI_CTL) = (M32(SPI_CTL) & ~3u) | m; }
static int nor_read(uint32_t addr, uint8_t *dst, uint32_t len)
{
	int err = 0;
	M32(SPI_CTL) |= 8; M32(SPI_CTL) &= ~8u;
	spi_mode(0); spi_mode(2);
	M32(SPI_TX) = 0x03;
	M32(SPI_TX) = addr >> 16;
	M32(SPI_TX) = addr >> 8;
	M32(SPI_TX) = addr;
	err |= spi_wait(0x10, 0x10) | spi_wait(0x40, 0);
	spi_mode(0);
	M32(SPI_CTL) &= ~0x30u; M32(SPI_CTL) |= 0x30;
	M32(SPI_BC) = len;
	spi_mode(1);
	for (uint32_t i = 0; i < len; i++) {
		err |= spi_wait(4, 0);
		dst[i] = M32(SPI_RX);
	}
	spi_mode(0);
	M32(SPI_CTL) |= 8;
	return err;
}

void boot1_main(void)
{
	M32(0xc003001c) |= 1;		/* watchdog */
	int err = nor_read(S2_NOR, (uint8_t *)S2_ADDR, 1024);
	if (!err && M32(S2_ADDR) == S2_MAGIC)
		((void (*)(void))(S2_ADDR + 0x11))();
	rom_cleanup1();
	rom_cleanup2();
	rom_adfu();
	for (;;);
}
