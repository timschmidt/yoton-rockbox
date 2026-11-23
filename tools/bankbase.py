#!/usr/bin/env python3
"""Find the load address of an overlay bank: the base that maximises Thumb BL
targets landing on known function entry points.  bankbase.py BANK.bin KNOWN_HEX..."""
import struct, sys
def bl_targets(code):
    out = []
    for i in range(0, len(code) - 3, 2):
        h1, h2 = struct.unpack_from('<HH', code, i)
        if (h1 & 0xF800) == 0xF000 and (h2 & 0xD000) == 0xD000:
            s = (h1 >> 10) & 1; j1 = (h2 >> 13) & 1; j2 = (h2 >> 11) & 1
            i1 = 1 - (j1 ^ s); i2 = 1 - (j2 ^ s)
            off = (s << 24) | (i1 << 23) | (i2 << 22) | ((h1 & 0x3FF) << 12) | ((h2 & 0x7FF) << 1)
            if s: off -= 1 << 25
            out.append((i, i + 4 + off))
    return out
code = open(sys.argv[1], 'rb').read()
known = {int(x, 16) for x in sys.argv[2:]}
t = bl_targets(code)
best = []
for base in list(range(0x100000, 0x138000, 2)) + list(range(0x158000, 0x160000, 2)) + list(range(0x200000, 0x208000, 2)) + list(range(0x300000, 0x308000, 2)):
    hits = sum(1 for i, rel in t if (base + rel) in known)
    if hits:
        best.append((hits, base))
best.sort(reverse=True)
print(len(t), "BLs;", [(h, hex(b)) for h, b in best[:8]])
