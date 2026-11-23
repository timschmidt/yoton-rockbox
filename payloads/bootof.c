/* Boot the vendor firmware from ymon the way the mask ROM does (0x1b14):
 * SPI init, pin group 0, NOR reset, load+verify mbrec via ROM 0x1984 with
 * descrambling, then jump to the boot record entry 0x101011. */
#include "rom.h"
#define ROMCTX ((volatile uint32_t *)0x100034)
ROMFN(0x1984, int, rom_load_mbrec, (void));
struct res { void *addr; unsigned size; };
static struct res r; static uint32_t o[4];
void *entry(void)
{
	rom_spi_boot_init();
	ROMCTX[0x64 / 4] = 0;		/* pin group 0 */
	rom_spi_pins_on(0);
	rom_nor_reset();
	ROMCTX[0x6c / 4] = 0;
	ROMCTX[0x70 / 4] = 0;		/* NOR address 0 */
	ROMCTX[0x68 / 4] = 1;		/* descramble */
	int rc = rom_load_mbrec();
	o[0] = rc; o[1] = M32(0x100000); o[2] = M32(0x101000);
	if (rc == 0) {
		__asm__ volatile("cpsid i");
		M32(0xe000e180) = 0xffffffff;	/* NVIC: all off */
		M32(0xe000ed08) = 0;		/* VTOR back to ROM */
		__asm__ volatile("dsb; isb; cpsie i");
		((void (*)(void))0x101011)();
	}
	r.addr = o; r.size = 12; return &r;
}
