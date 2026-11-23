#!/usr/bin/env python3
"""Wrap ym03stub.raw into a 1 KiB boot record the mask ROM accepts:
halfword0 = (sum(halfwords[1:]) + 0x1234) & 0xffff, byte2 = 0x5a (1 KiB)."""
import struct, sys
raw = bytearray(open(sys.argv[1], "rb").read())
if len(raw) > 1024:
    sys.exit("stub too big")
rec = raw + bytes(1024 - len(raw))
rec[2] = 0x5a
rec[3] = 0x01
hw = struct.unpack("<512H", rec)
csum = (sum(hw[1:]) + 0x1234) & 0xffff
struct.pack_into("<H", rec, 0, csum)
open(sys.argv[2], "wb").write(rec)
print(f"boot record {sys.argv[2]}: checksum {csum:#06x}, code {len(raw)} bytes")
