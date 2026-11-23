/* LCD bring-up test for the Yoton YM03 (ATJ2157 LCM + ST7735S-class 128x128). */
#include <stdint.h>
#define M32(a) (*(volatile uint32_t *)(a))
#define CMU(x)  M32(0xc0001000 + (x))
#define RMU0    M32(0xc0000000)
#define LCM(x)  M32(0xc01a0000 + (x))
#define MFP(p)  M32(0xc01c0004 + 4 * (p))
#define GPIO_SET(p) M32(0xc01c0210 + 4 * ((p) >> 5)) = 1u << ((p) & 31)
#define GPIO_CLR(p) M32(0xc01c0220 + 4 * ((p) >> 5)) = 1u << ((p) & 31)
#define ARGS ((volatile uint32_t *)0x11f800)

#define PIN_RST 21
#define PIN_CS  43
#define PIN_BL  22

struct res { void *addr; unsigned size; };
static struct res r;
static uint32_t out[16];

static void delay(unsigned n) { for (volatile unsigned i = 0; i < n * 4000; i++); }

static void lcd_cmd(uint8_t c)
{
	LCM(0) = (LCM(0) & 0xe7ffff3f) | 1;
	LCM(0x14) = c;
	delay(1);
}

static void lcd_data_mode(void)
{
	LCM(0) = (LCM(0) & 0xe7efff3f) | 0x40 | 0x100000;
}

static void lcd_dat(uint8_t d)
{
	LCM(0x10) = 0;
	LCM(0x14) = d;
}

static const uint8_t init_tab[] = {
	0xfd, 0x11, 0xfe, 0xb4,
	0xfd, 0xf0, 0x11,
	0xfd, 0xf4, 0xca, 0x78, 0x64,
	0xfd, 0xb1, 0x01, 0x08, 0x05,
	0xfd, 0xb2, 0x01, 0x08, 0x05,
	0xfd, 0xb3, 0x01, 0x08, 0x05, 0x01, 0x08, 0x05,
	0xfd, 0xb4, 0x03,
	0xfd, 0xc0, 0x28, 0x08, 0x04,
	0xfd, 0xc1, 0xc0,
	0xfd, 0xc2, 0x0d, 0x00,
	0xfd, 0xc3, 0x8d, 0x2a,
	0xfd, 0xc4, 0x8d, 0xee,
	0xfd, 0xc5, 0x07,
	0xfd, 0x36, 0xc8,
	0xfd, 0xe0, 0x04, 0x22, 0x07, 0x0a, 0x2e, 0x30, 0x25, 0x2a, 0x28, 0x26, 0x2e, 0x3a, 0x00, 0x01, 0x03, 0x13,
	0xfd, 0xe1, 0x04, 0x16, 0x06, 0x0d, 0x2d, 0x26, 0x23, 0x27, 0x27, 0x25, 0x2d, 0x3b, 0x00, 0x01, 0x04, 0x13,
	0xfd, 0x3a, 0x05,
	0xfd, 0x29,
	0xff
};

static void run_table(const uint8_t *t)
{
	for (unsigned i = 0; t[i] != 0xff; i++) {
		if (t[i] == 0xfd) { lcd_cmd(t[++i]); lcd_data_mode(); }
		else if (t[i] == 0xfe) delay(t[++i]);
		else lcd_dat(t[i]);
	}
}

static void window(int x0, int y0, int x1, int y1)
{
	lcd_cmd(0x2a); lcd_data_mode(); lcd_dat(0); lcd_dat(x0); lcd_dat(0); lcd_dat(x1);
	lcd_cmd(0x2b); lcd_data_mode(); lcd_dat(0); lcd_dat(y0); lcd_dat(0); lcd_dat(y1);
	lcd_cmd(0x2c); lcd_data_mode();
}

void *entry(void)
{
	uint32_t mode = ARGS[0];
	if (mode == 0) {
		/* LCM clock + reset */
		CMU(0x34) = (CMU(0x34) & ~0x30f) | 3;
		CMU(8) |= 0x2000 | 1;
		RMU0 &= ~0x2000; delay(1); RMU0 |= 0x2000;
		LCM(8) = 0; LCM(0xc) = 0; LCM(0) = 1;
		/* pinmux: D0-D7, RS, RD, WR -> LCM (func 2), CS with drive 2 */
		static const uint8_t pins[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 11 };
		for (unsigned i = 0; i < sizeof(pins); i++) MFP(pins[i]) = (MFP(pins[i]) & ~31) | 2;
		MFP(PIN_CS) = (MFP(PIN_CS) & ~(31 | 0x7000)) | 2 | 0x2000;
		/* panel reset on GPIO21 */
		MFP(PIN_RST) = (MFP(PIN_RST) & ~0x9f) | 0x40;
		GPIO_SET(PIN_RST); delay(10);
		GPIO_CLR(PIN_RST); delay(20);
		GPIO_SET(PIN_RST); delay(60);
		run_table(init_tab);
		/* backlight full on as plain GPIO */
		MFP(PIN_BL) = (MFP(PIN_BL) & ~0x9f) | 0x40;
		GPIO_SET(PIN_BL);
	}
	/* colour bars: 8 vertical bars over 128x128 */
	static const uint16_t bars[8] = { 0xffff, 0xffe0, 0x07ff, 0x07e0, 0xf81f, 0xf800, 0x001f, 0x0000 };
	window(0, 0, 127, 127);
	for (int y = 0; y < 128; y++)
		for (int x = 0; x < 128; x++) {
			uint16_t c = bars[x >> 4];
			if (mode == 1) c = ~c;
			lcd_dat(c >> 8); lcd_dat(c);
		}
	out[0] = LCM(0); out[1] = LCM(4); out[2] = LCM(8); out[3] = LCM(0xc);
	out[4] = CMU(8); out[5] = RMU0; out[6] = MFP(0); out[7] = MFP(PIN_CS);
	r.addr = out; r.size = sizeof(out);
	return &r;
}
