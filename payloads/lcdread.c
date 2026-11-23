/* Read back LCD GRAM via RAMRD (vendor method: 3 bytes/pixel, first read dummy) */
#include <stdint.h>
#define M32(a) (*(volatile uint32_t *)(a))
#define LCM(x)  M32(0xc01a0000 + (x))
#define ARGS ((volatile uint32_t *)0x11f800)
struct res { void *addr; unsigned size; };
static struct res r;
static uint8_t buf[1024];
static void delay(unsigned n) { for (volatile unsigned i = 0; i < n * 4000; i++); }
static void lcd_cmd(uint8_t c) { LCM(0) = (LCM(0) & 0xe7ffff3f) | 1; LCM(0x14) = c; delay(1); }
static void dm(void) { LCM(0) = (LCM(0) & 0xe7efff3f) | 0x40 | 0x100000; }
static void dat(uint8_t d) { LCM(0x10) = 0; LCM(0x14) = d; }
void *entry(void)
{
	int y = ARGS[0], n = ARGS[1];	/* row, number of bytes */
	lcd_cmd(0x2a); dm(); dat(0); dat(0); dat(0); dat(127);
	lcd_cmd(0x2b); dm(); dat(0); dat(y); dat(0); dat(127);
	lcd_cmd(0x2e);
	LCM(0) = (LCM(0) & 0xe7dfff3f) | 0x40;
	(void)*(volatile uint8_t *)(0xc01a0014);	/* dummy */
	for (int i = 0; i < n; i++) buf[i] = *(volatile uint8_t *)(0xc01a0014);
	r.addr = buf; r.size = n;
	return &r;
}
