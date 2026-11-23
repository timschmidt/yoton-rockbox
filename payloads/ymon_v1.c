/*
 * ymon - fault-tolerant ADFU-compatible RAM monitor for the ATJ2157.
 *
 * Loaded by the boot ROM (cd 13 WRITERAM at 0x118000, cd 20 SWITCH).
 * Speaks the same CBW/CSW framing as the ROM ADFU, with these commands
 * (CDB: cd, sub LE32, len LE32, addr LE32):
 *
 *   0x13 / 0x93  write / read memory (CPU-copied through a bounce buffer, so
 *                any address incl. ROM and system registers is readable)
 *   0x20         jump to addr (never returns)
 *   0x21         call addr (fault-protected). CSW status 1 if it faulted.
 *   0x22 / 0x23  result size / result data of last 0x21 (struct {addr,size})
 *   0x24         fault info: {magic, cfsr, hfsr, mmfar, bfar, pc, lr, xpsr}
 *   0x7e         ping: returns "YMON" + version
 *   0xb0 (cdb0)  system reset
 *
 * Unknown commands return CSW status 2 and the monitor keeps running.
 * A USB bus reset triggers a system reset (the ROM must re-enumerate).
 */
#include <stdint.h>

#define M32(a) (*(volatile uint32_t *)(a))
#define USB(x) M32(0xc0140000 + (x))
#define RTC_WDCTL 0xc003001c

#define YMON_VERSION 1
#define BOUNCE ((uint8_t *)0x11a000)
#define BOUNCE_SZ 0x2000

typedef void (*vec_t)(void);
static vec_t vt[64] __attribute__((aligned(256)));
static uint8_t cbw[32] __attribute__((aligned(4)));
static uint8_t csw_status;
static volatile uint32_t jump_addr;
static struct { void *addr; uint32_t size; } *result;
uint32_t fault_info[8];
uint32_t exec_saved_sp;

extern void fault_entry(void);
extern uint32_t exec_protected(uint32_t fn);

static inline void wd_clear(void) { M32(RTC_WDCTL) |= 1; }

static void sysreset(void)
{
	__asm__ volatile("dsb");
	M32(0xe000ed0c) = 0x05fa0004;
	for (;;);
}

static void hang(void) { for (;;); }

/* USB DMA: ch 0 = bulk IN (to host), ch 2 = bulk OUT (from host) */
static int usb_dma(int ch, void *p, uint32_t n, int in)
{
	USB(0x308 + ch * 8) = n << 8 | (in << 1);
	USB(0x30c + ch * 8) = (uint32_t)p;
	USB(0x330) = 1;
	USB(0x308 + ch * 8) |= 1;
	uint32_t done = 2u << ch, err = done << 16;
	for (;;) {
		uint32_t s = USB(0x328);
		if (s & done) break;
		if (s & err) {
			USB(0x330) = 0;
			USB(0x308 + ch * 8) = 0;
			USB(0x328) = s & err;
			return -1;
		}
	}
	USB(0x328) = done;
	wd_clear();
	return 0;
}

static void copy(uint8_t *d, const uint8_t *s, uint32_t n)
{
	if ((((uint32_t)d | (uint32_t)s | n) & 3) == 0)
		for (; n; n -= 4, d += 4, s += 4) *(volatile uint32_t *)d = *(volatile const uint32_t *)s;
	else
		for (; n; n--) *(volatile uint8_t *)d++ = *(volatile const uint8_t *)s++;
}

static void send(const void *p, uint32_t n)
{
	const uint8_t *s = p;
	while (n) {
		uint32_t c = n > BOUNCE_SZ ? BOUNCE_SZ : n;
		copy(BOUNCE, s, c);
		usb_dma(0, BOUNCE, c, 1);
		s += c; n -= c;
	}
}

static void recv(void *p, uint32_t n)
{
	uint8_t *d = p;
	while (n) {
		uint32_t c = n > BOUNCE_SZ ? BOUNCE_SZ : n;
		usb_dma(2, BOUNCE, c, 0);
		copy(d, BOUNCE, c);
		d += c; n -= c;
	}
}

static void send_csw(void)
{
	cbw[3] = 0x53;			/* USBC -> USBS */
	*(uint32_t *)(cbw + 8) = 0;
	cbw[12] = csw_status;
	usb_dma(0, cbw, 13, 1);
}

static void cmd(void)
{
	uint32_t sub = *(uint32_t *)(cbw + 16);
	uint32_t len = *(uint32_t *)(cbw + 20);
	uint32_t addr = *(uint32_t *)(cbw + 24);
	csw_status = 0;
	switch (sub) {
	case 0x13: recv((void *)addr, len); break;
	case 0x93: send((void *)addr, len); break;
	case 0x20: jump_addr = addr | 1; break;
	case 0x21:
		fault_info[0] = 0;
		wd_clear();
		result = (void *)exec_protected(addr | 1);
		wd_clear();
		if (fault_info[0]) { csw_status = 1; result = 0; }
		break;
	case 0x22: {
		uint32_t z = result ? result->size : 0;
		send(&z, len > 4 ? 4 : len);
		break;
	}
	case 0x23: if (result) send(result->addr, len); break;
	case 0x24: send(fault_info, len > 32 ? 32 : len); break;
	case 0x7e: {
		static const uint32_t ping[4] = { 0x4e4f4d59 /* YMON */, YMON_VERSION, 0, 0 };
		send(ping, len > 16 ? 16 : len);
		break;
	}
	default:
		/* drain/skip data phase is the host's job; just fail */
		csw_status = 2;
	}
	send_csw();
}

static void usb_irq(void)
{
	wd_clear();
	uint32_t t1 = USB(0x140) & ~USB(0x130);
	if (t1 & 4) {
		uint32_t t0 = USB(0x14c) & ~USB(0x13c);
		if (t0 & 1) USB(0x14c) = 1;
		if (t0 & 2) sysreset();		/* bus reset */
		if (t0 & 4) USB(0x14c) = 4;
	}
	if (t1 & 2) {
		uint32_t t0 = USB(0x148) & ~USB(0x138);
		if (t0 & 0x20) {
			if (USB(0x1b8) != 0x1f) return;
			if (usb_dma(2, cbw, 0x1f, 0)) return;
			if (*(uint32_t *)cbw != 0x43425355) return;
			if (cbw[15] == 0xcd) cmd();
			else if (cbw[15] == 0xb0) { csw_status = 0; send_csw(); sysreset(); }
			else { csw_status = 2; send_csw(); }
		}
	}
}

void ymon_main(void)
{
	wd_clear();
	__asm__ volatile("cpsid i");
	for (int i = 0; i < 64; i++) vt[i] = hang;
	vt[2] = hang;			/* NMI */
	vt[3] = vt[4] = vt[5] = vt[6] = fault_entry;
	vt[16 + 12] = usb_irq;
	M32(0xe000ed24) |= 0x70000;	/* SHCSR: enable MemManage/BusFault/UsageFault */
	M32(0xe000ed08) = (uint32_t)vt;	/* VTOR */
	__asm__ volatile("dsb; isb");
	USB(0x130) |= 1;
	M32(0xe000e100) = 1u << 12;	/* NVIC: USB */
	__asm__ volatile("cpsie i");
	for (;;) {
		wd_clear();
		if (jump_addr) {
			uint32_t a = jump_addr;
			jump_addr = 0;
			((void (*)(void))a)();
		}
	}
}
