#!/usr/bin/env python3
"""Boot the paged Rockbox main firmware from RAM in test mode and read back
the screen after its self-reboot.

  ym03_maintest.py BUILDDIR [--png OUT] [--no-pages]

Writes BUILDDIR/rockbox.pages to NOR (unless --no-pages), loads
BUILDDIR/rockbox.ym03 at 0x100000 through ymon, arms the 30 s test-mode
reboot and jumps. Afterwards the stock firmware boots; the tool switches back
to ADFU and dumps the framebuffer (it survives) as a PNG.
"""
import argparse, os, struct, sys, time
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import yoton

p = argparse.ArgumentParser()
p.add_argument("build")
p.add_argument("--png", default="ym03_main.png")
p.add_argument("--no-pages", action="store_true")
p.add_argument("--interactive", action="store_true", help="no test-mode reboot; just boot and exit")
p.add_argument("--usb", action="store_true", help="test mode that lets Rockbox enter USB mode")
p.add_argument("--wps", action="store_true", help="autoplay with the Now Playing screen; CPU profile at 0x137000")
p.add_argument("--faults", action="store_true", help="with --wps: log page-fault addresses instead of CPU samples")
p.add_argument("--rofaults", action="store_true", help="log rodata faults {addr,pc} from playback start at 0x307c00")
p.add_argument("--ui", action="store_true", help="autoplay, then drive the real UI with injected keys (Back, scrolling)")
p.add_argument("--back", action="store_true", help="with --ui: profile from the Back press (3 s) on")
p.add_argument("--bigplay", action="store_true", help="play across windows of /ym03big")
p.add_argument("--browse", action="store_true", help="windowed browser test in /KEXP")
p.add_argument("--browse-play", action="store_true", help="the same with an MP3 playing")
p.add_argument("--powerkey2", action="store_true", help="power key candidate check")
p.add_argument("--powerkey", action="store_true", help="guided power key search")
p.add_argument("--keys", action="store_true", help="guided capture of VOL+/VOL-/POWER")
p.add_argument("--dirtime", action="store_true", help="time two full reads of /KEXP")
p.add_argument("--plugin", type=int, help="run test plugin N (see main.c) instead of autoplay")
p.add_argument("--list", action="store_true", help="autoplay the files named in /ym03test.lst")
p.add_argument("--lrprof", action="store_true", help="CPU profile records return addresses (callers)")
p.add_argument("--vol5", action="store_true", help="inject five Vol- presses from 3 s")
p.add_argument("--pcm", action="store_true", help="autoplay the AIFF/WAV test files even if /ym03test.mp3 exists")
a = p.parse_args()

d = yoton.Dev()
d.ensure_ymon()
if not a.no_pages:
    pages = open(os.path.join(a.build, "rockbox.pages"), "rb").read()
    # page store 0x762000 up to the installed resident image at 0x7CC000
    if 0x762000 + len(pages) > 0x7CC000:
        sys.exit(f"page store too big: {len(pages)}")
    yoton.nor_write(d, 0x762000, pages)
img = open(os.path.join(a.build, "rockbox.ym03"), "rb").read()
if 0x100000 + len(img) > 0x118000:
    sys.exit("resident image would overwrite ymon")
d.write(0x100000, img, step=0x1000)
if d.read(0x100000, len(img)) != img:
    sys.exit("image verify failed")
# test-mode magic, and the NOR stub's "go to ADFU" marker so that a crash
# or reboot lands straight back in ADFU with RAM (traces) intact
d.write(0x137FD0, bytes(16))
if a.pcm:
    d.write(0x137FD8, b"NOMP")
if a.list:
    d.write(0x137FD8, b"LIST")
if a.lrprof:
    d.write(0x137FD8, b"LRPF")
if a.vol5:
    d.write(0x137FD8, b"VOL5")
if a.back:
    d.write(0x137FDC, b"BACK")
if a.bigplay:
    d.write(0x137FDC, b"BIGP")
if a.browse_play:
    d.write(0x137FDC, b"BRWP")
if a.browse:
    d.write(0x137FDC, b"BRWS")
if a.powerkey2:
    d.write(0x137FDC, b"PWR2")
if a.powerkey:
    d.write(0x137FDC, b"PWRK")
if a.keys:
    d.write(0x137FDC, b"KEYS")
if a.dirtime:
    d.write(0x137FDC, b"DIRT")
if a.plugin is not None:
    d.write(0x137FDC, b"PLG" + bytes([0x30 + a.plugin]))
d.write(0x137C00, bytes(16))
d.write(0x137FE0, struct.pack("<4I", 0, 0, 0x524C4146 if a.rofaults else 0x544C4146 if a.faults else 0, 0x31535057 if a.wps else 0x31534955 if a.ui else 0))
d.write(0x137FF0, struct.pack("<II", 0 if a.interactive else 0x42535559 if a.usb else 0x54535459,
                              0 if a.interactive else 0x55464441))
d.xfer(0x20, 0, 0x100001)
if a.interactive:
    print("booted rockbox.ym03 (interactive)")
    sys.exit(0)
print(f"jumped to rockbox.ym03 ({len(img)} bytes); waiting for test-mode reboot")
import usb.util
usb.util.dispose_resources(d.t.dev)
t0 = time.time()
while yoton._find() is not None and time.time() - t0 < 400:
    time.sleep(0.5)
print(f"device left the bus after {time.time() - t0:.0f}s")
yoton.to_adfu_from_msc(120)
if not yoton.wait_dev(60):
    sys.exit("device did not come back")
time.sleep(1)
d = yoton.Dev()
d.ensure_ymon()
yoton.rgb565_png(d.read(0x158000, 128 * 128 * 2), 128, 128, a.png)
print("screen ->", a.png)
