/* Change logger for key discovery: watches PMU (0xc0010000..7c), GPIO
 * (0xc01c0200..27c) and RTC (0xc0030000..3c) every 10 ms and logs
 * (time/10ms, reg, value) whenever a register changes beyond ADC noise. */
#include <stdint.h>
#define M32(a) (*(volatile uint32_t *)(a))
#define ARGS ((volatile uint32_t *)0x11f800)
#define LOG ((volatile uint32_t *)0x120000)
#define MAXLOG 8000
struct res { void *addr; unsigned size; };
static struct res r;
static uint32_t prev[96];
static uint32_t regaddr(int i)
{
	if (i < 32) return 0xc0010000 + 4 * i;
	if (i < 64) return 0xc01c0200 + 4 * (i - 32);
	return 0xc0030000 + 4 * (i - 64);
}
static int changed(uint32_t a, uint32_t b)
{
	uint32_t x = a ^ b;
	if (!x) return 0;
	/* ADC-like: only low 10 bits differ by a few counts -> noise */
	if (!(x & ~0x3ffu)) {
		int d = (int)(a & 0x3ff) - (int)(b & 0x3ff);
		if (d < 0) d = -d;
		return d > 20;
	}
	return 1;
}
void *entry(void)
{
	uint32_t n = ARGS[0], cnt = 0;
	M32(0xe000e014) = 48000 - 1;
	M32(0xe000e018) = 0;
	M32(0xe000e010) = 5;
	for (int i = 0; i < 80; i++) prev[i] = M32(regaddr(i));
	LOG[cnt++] = 0xffffffff;
	for (int i = 0; i < 80; i++) { LOG[cnt++] = regaddr(i); LOG[cnt++] = prev[i]; }
	for (uint32_t t = 0; t < n && cnt < MAXLOG * 2 - 4; t++) {
		for (int i = 0; i < 80; i++) {
			if (i == 64 + 7) continue;		/* RTC+0x1c watchdog: ymon pokes it */
			uint32_t v = M32(regaddr(i));
			if (changed(v, prev[i]) && cnt < MAXLOG * 2 - 4) {
				LOG[cnt++] = t << 8 | i;
				LOG[cnt++] = v;
				prev[i] = v;
			}
		}
		for (int k = 0; k < 10; k++)
			while (!(M32(0xe000e010) & 0x10000));
	}
	M32(0xe000e010) = 0;
	r.addr = (void *)LOG; r.size = cnt * 4;
	return &r;
}
