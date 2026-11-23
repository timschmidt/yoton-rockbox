/* ATJ2157 mask-ROM entry points (from dumps/rom.bin analysis) */
#include <stdint.h>
#define M32(a) (*(volatile uint32_t *)(a))
#define ROMFN(addr, ret, name, args) static ret (* const name) args = (ret (*) args)((addr) | 1)
ROMFN(0x1ab4, void, rom_spi_boot_init, (void));          /* save CMU, SPI0 clock/reset, ... */
ROMFN(0x383c, void, rom_spi_pins_on, (int grp));         /* pinmux SPI group 0..2 (0xc01c0000) */
ROMFN(0x3884, void, rom_spi_pins_off, (int grp));
ROMFN(0x3280, void, rom_nor_reset, (void));              /* AB,06,C5 00,E9,FF,FF FF */
ROMFN(0x3190, void, rom_nand_reset, (void));             /* AB,FF */
ROMFN(0x31ac, int,  rom_spi_probe, (int nand));          /* 9F; 0 = id ok */
ROMFN(0x2224, int,  rom_nor_read, (uint32_t addr, uint32_t len, void *dst, int x));
ROMFN(0x4b28, void, rom_udelay, (unsigned n));
#define SPI0_CTL  0xc00a0000
#define SPI0_STAT 0xc00a0004
#define SPI0_TX   0xc00a0008
#define SPI0_RX   0xc00a000c
#define SPI0_BC   0xc00a0010
