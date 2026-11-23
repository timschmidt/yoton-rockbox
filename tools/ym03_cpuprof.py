#!/usr/bin/env python3
"""Decode the 1 kHz PC samples ("YCPU" block at framebuffer row 64).
ym03_cpuprof.py ELF SAMPLES.bin"""
import struct, subprocess, sys, bisect, collections
elf, f = sys.argv[1], sys.argv[2]
syms = []
for l in subprocess.run(["arm-none-eabi-nm", "-n", "-S", elf], capture_output=True, text=True).stdout.splitlines():
    p = l.split()
    if len(p) == 4 and p[2] in "tTwW":
        syms.append((int(p[0], 16), int(p[1], 16), p[3]))
syms.sort(); addrs = [s[0] for s in syms]
def fn(a):
    if a == 0xfffffff0: return "<handler mode>"
    a &= ~1
    i = bisect.bisect_right(addrs, a) - 1
    if i >= 0 and a < syms[i][0] + max(syms[i][1], 2): return syms[i][2]
    return f"?{a:#x}"
d = open(f, "rb").read()
magic, n = struct.unpack_from("<II", d)
assert magic == 0x55504359, hex(magic)
n = min(n, (len(d) - 8) // 4)
c = collections.Counter(fn(struct.unpack_from("<I", d, 8 + 4 * i)[0]) for i in range(n))
print(f"{n} samples")
for k, v in c.most_common(35): print(f"{100.0 * v / n:5.1f}% {v:5} {k}")
