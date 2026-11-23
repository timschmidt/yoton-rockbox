/*
 * Yoton YM03 NOR boot stub (replaces the vendor boot record at NOR 0).
 *
 * The mask ROM loads this 1 KiB record to 0x101000 and enters 0x101011 with
 * SPI0 already set up for the NOR. Keys are sampled once at power-up:
 *
 *   RAM marker "ADFU" at 0x137ff4 -> boot ROM USB recovery (set by Rockbox)
 *   hold Right  (LRADC1 60..200)  -> boot ROM USB recovery (ADFU)
 *   hold Select (ADC2 350..490)   -> original vendor firmware
 *   otherwise                     -> Rockbox (resident image from NOR)
 *   3 Rockbox starts in a row that never cleared the boot counter at
 *   0x137ff8 (watchdog resets) -> boot ROM USB recovery
 *
 * An ADC that is not running reads 0, which selects nothing (Rockbox).
 * A missing or corrupt Rockbox image falls back to the vendor firmware.
 *
 * NOR layout (free region 0x762000-0x7DFFFF; vendor data ends 0x760C00):
 *   0x762000  page store ("YMPG")
 *   RES_NOR   "YRES", length, sum32 of the image, 0; image at RES_NOR+16
 *   OFMB_NOR  copy of the vendor boot record (1 KiB, descrambled)
 */
#include <stdint.h>
#define M32(a) (*(volatile uint32_t *)(a))
#define ROMCTX ((volatile uint32_t *)0x100034)
#define ROMFN(addr, ret, name, args) static ret (* const name) args = (ret (*) args)((addr) | 1)
ROMFN(0x11d4, void, rom_cleanup1, (void));
ROMFN(0x1258, void, rom_cleanup2, (void));
ROMFN(0x1144, void, rom_adfu, (void));

#define RES_NOR     0x7cc000
#define RES_MAGIC   0x53455259          /* "YRES" */
#define RES_MAX     0x14000
#define OFMB_NOR    0x7df000
#define OFMB_TMP    0x102000
#define TRAMP       0x137800            /* plugin buffer: free at boot */

#define SPI_CTL     0xc00a0000
#define SPI_STAT    0xc00a0004
#define SPI_TX      0xc00a0008
#define SPI_RX      0xc00a000c
#define SPI_BC      0xc00a0010

static inline void wd(void) { M32(0xc003001c) |= 1; }

/*
 * Everything the loader needs after the image has overwritten this record
 * lives in .tramp: it is copied to TRAMP and runs from there.
 */
#define TR __attribute__((section(".tramp"), noinline))

TR static int spi_wait(uint32_t mask, uint32_t val)
{
	for (int i = 0; i < 200000; i++)
		if ((M32(SPI_STAT) & mask) == val)
			return 0;
	return 1;
}

TR static void spi_mode(int m) { M32(SPI_CTL) = (M32(SPI_CTL) & ~3u) | m; }

/* NOR READ (0x03); dst NULL just sums. Returns the 32-bit byte sum, or
 * sets *err on a timeout. */
TR static uint32_t nor_read(uint32_t addr, uint8_t *dst, uint32_t len, int *err)
{
	uint32_t sum = 0;
	M32(SPI_CTL) |= 8; M32(SPI_CTL) &= ~8u;
	spi_mode(0); spi_mode(2);
	M32(SPI_TX) = 0x03;
	M32(SPI_TX) = addr >> 16;
	M32(SPI_TX) = addr >> 8;
	M32(SPI_TX) = addr;
	*err |= spi_wait(0x10, 0x10) | spi_wait(0x40, 0);
	spi_mode(0);
	while (len) {
		uint32_t c = len > 0x1000 ? 0x1000 : len;
		M32(SPI_CTL) &= ~0x30u; M32(SPI_CTL) |= 0x30;
		M32(SPI_BC) = c;
		spi_mode(1);
		for (uint32_t i = 0; i < c; i++) {
			*err |= spi_wait(4, 0);
			uint8_t b = M32(SPI_RX);
			sum += b;
			if (dst)
				*dst++ = b;
		}
		spi_mode(0);
		len -= c;
		M32(0xc003001c) |= 1;
	}
	M32(SPI_CTL) |= 8;
	return sum;
}

/* runs at TRAMP: load the image over 0x100000 and enter it. The stub and
 * its stack (0x101800) are overwritten by the load, so first move the stack
 * to the top of SRAM0. */
TR __attribute__((naked)) static void load_rockbox(uint32_t len)
{
	__asm__ volatile(
		"mov r4, r0\n"
		"ldr r0, =0x137f00\n"
		"mov sp, r0\n"
		"sub sp, #8\n"
		"movs r0, #0\n"
		"str r0, [sp]\n"
		"ldr r0, =%0\n"			/* NOR address */
		"ldr r1, =0x100000\n"			/* destination */
		"mov r2, r4\n"				/* length */
		"mov r3, sp\n"				/* &err */
		"bl nor_read\n"
		"ldr r0, =0x137ff0\n"			/* no host test mode */
		"movs r1, #0\n"
		"str r1, [r0]\n"
		"ldr r0, =0x100001\n"
		"bx r0\n"
		".ltorg\n"
		: : "i" (RES_NOR + 16));
}

/* runs at TRAMP: move the vendor boot record over us and enter it */
TR static void enter_vendor(void)
{
	for (int i = 0; i < 256; i++)
		((volatile uint32_t *)0x101000)[i] = ((volatile uint32_t *)OFMB_TMP)[i];
	ROMCTX[0x68 / 4] = 1;			/* the vendor record expects descrambling */
	__asm__ volatile(
		"ldr r0, =0x101011\n"
		"bx r0\n"
		".ltorg\n");
}

extern char __tramp_start[], __tramp_end[];
extern char __tramp_load[];

static void copy_tramp(void)
{
	uint8_t *s = (uint8_t *)__tramp_load, *d = (uint8_t *)TRAMP;
	for (int i = 0; i < __tramp_end - __tramp_start; i++)
		d[i] = s[i];
}

#define AT_TRAMP(fn) ((void *)((uintptr_t)(fn) - (uintptr_t)__tramp_start + TRAMP))

static void boot_adfu(void)
{
	rom_cleanup1();
	rom_cleanup2();
	rom_adfu();
}

static void boot_vendor(void)
{
	int err = 0;
	uint32_t (*rd)(uint32_t, uint8_t *, uint32_t, int *) = AT_TRAMP(nor_read);
	rd(OFMB_NOR, (uint8_t *)OFMB_TMP, 1024, &err);
	if (!err && ((uint8_t *)OFMB_TMP)[2] == 0xa5)
		((void (*)(void))AT_TRAMP(enter_vendor))();
}

static unsigned adc(uint32_t reg)
{
	unsigned lo = 0x3ff, hi = 0;
	for (int i = 0; i < 16; i++) {
		unsigned a = M32(reg) & 0x3ff;
		if (a < lo) lo = a;
		if (a > hi) hi = a;
		for (volatile int d = 0; d < 3000; d++);
	}
	/* a key must be held steadily */
	return (hi - lo < 40) ? hi : 0x3ff;
}

void stub_main(void)
{
	wd();

	/* warm reboot requested by Rockbox (USB command 0xCB 0x21) */
	if (M32(0x137ff4) == 0x55464441) {
		M32(0x137ff4) = 0;
		boot_adfu();
	}
	copy_tramp();

	/* key ADCs, as button-yotonym03.c */
	M32(0xc01c0004 + 4 * 17) = 0x303b;
	M32(0xc01c0304) = 0x3001;
	M32(0xc0001008) |= 1u << 27;
	M32(0xc0010038) |= 0x210;
	for (volatile int d = 0; d < 200000; d++);

	unsigned k1 = adc(0xc0010050), k2 = adc(0xc0010064);
	if (k1 >= 60 && k1 <= 200)
		boot_adfu();
	if (k2 >= 350 && k2 <= 490)
		boot_vendor();

	/* Boot attempts survive the watchdog reset in RAM; Rockbox clears the
	 * count once it is up. Three failed starts in a row: USB recovery. */
	uint32_t att = M32(0x137ff8);
	uint32_t tries = (att >> 8) == 0x594d42 ? (att & 0xff) : 0;
	if (tries >= 3) {
		M32(0x137ff8) = 0;
		boot_adfu();
	}
	M32(0x137ff8) = (0x594d42 << 8) | (tries + 1);

	/* Rockbox: header, then a checksum pass, then the real load */
	int err = 0;
	uint32_t hdr[4];
	uint32_t (*rd)(uint32_t, uint8_t *, uint32_t, int *) = AT_TRAMP(nor_read);
	rd(RES_NOR, (uint8_t *)hdr, 16, &err);
	if (!err && hdr[0] == RES_MAGIC && hdr[1] && hdr[1] <= RES_MAX &&
	    rd(RES_NOR + 16, 0, hdr[1], &err) == hdr[2] && !err)
		((void (*)(uint32_t))AT_TRAMP(load_rockbox))(hdr[1]);

	boot_vendor();
	boot_adfu();
	for (;;);
}
