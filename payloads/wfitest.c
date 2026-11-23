/* Does SysTick keep running (and wake the core) across WFI? USB IRQs stay
 * enabled, so the core is woken by host polling even if SysTick stops. */
#include <stdint.h>
#define M32(a) (*(volatile uint32_t *)(a))
struct res { void *addr; unsigned size; };
static struct res r; static uint32_t o[8];
static uint32_t vt[64] __attribute__((aligned(256)));
static volatile uint32_t ticks;
static void st_handler(void) { ticks++; }
void *entry(void)
{
	uint32_t old = M32(0xe000ed08);
	for (int i = 0; i < 64; i++) vt[i] = ((uint32_t *)old)[i];
	vt[15] = (uint32_t)st_handler;
	M32(0xe000ed08) = (uint32_t)vt;
	ticks = 0;
	M32(0xe000e014) = 48000 - 1;		/* 1 kHz at 48 MHz */
	M32(0xe000e018) = 0;
	M32(0xe000e010) = 7;			/* core clk, TICKINT, enable */
	/* busy phase: 200 ms */
	while (ticks < 200);
	uint32_t t_busy = ticks;
	/* wfi phase: 200 wakeups */
	uint32_t wakes = 0;
	for (int i = 0; i < 200; i++) { __asm__ volatile("wfi"); wakes++; }
	uint32_t t_after = ticks;
	/* busy phase again: wait 200 more ticks, bounded by a spin count */
	uint32_t spin = 0, t0 = ticks;
	while (ticks - t0 < 200 && spin < 20000000) spin++;
	M32(0xe000e010) = 0;
	M32(0xe000ed08) = old;
	o[0] = t_busy; o[1] = wakes; o[2] = t_after - t_busy; o[3] = spin; o[4] = ticks - t0;
	r.addr = o; r.size = 20; return &r;
}
