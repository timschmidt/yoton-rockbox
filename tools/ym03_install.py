#!/usr/bin/env python3
"""Install Rockbox for the Yoton YM03 into its NOR flash.

  ym03_install.py BUILDDIR stage       page store, resident image, vendor boot
                                       record copy -> free region 0x762000-0x7DFFFF
  ym03_install.py BUILDDIR stubtest    run payloads/ym03stub from RAM (no key
                                       held -> boots Rockbox from the staged NOR)
  ym03_install.py BUILDDIR flash-stub --i-have-a-backup
                                       replace the vendor boot record in NOR
                                       sector 0 with the stub (keeps 0x400-0xFFF)

Layout (see payloads/ym03stub.c): pages at 0x762000, "YRES" header + resident
image at 0x7CC000, vendor boot record copy at 0x7DF000.
"""
import os, struct, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import yoton

PAGES_NOR, RES_NOR, OFMB_NOR, FREE_END = 0x762000, 0x7CC000, 0x7DF000, 0x7E0000
ROOT = os.path.dirname(HERE)


def stub_record():
    """Stage 1 as a 512-byte boot record (type 0xA5), like the vendor's: the
    vendor's next stage starts at NOR 0x200 and must stay intact."""
    raw = bytearray(open(os.path.join(ROOT, "payloads/ym03boot1.raw"), "rb").read())
    if len(raw) > 512:
        sys.exit("stage 1 too big")
    rec = raw + bytes(512 - len(raw))
    rec[2], rec[3] = 0xA5, 0x01                     # 512-byte boot record
    hw = struct.unpack("<256H", rec)
    struct.pack_into("<H", rec, 0, (sum(hw[1:]) + 0x1234) & 0xFFFF)
    return bytes(rec)


def stage2():
    s2 = open(os.path.join(ROOT, "payloads/ym03stub2.raw"), "rb").read()
    if len(s2) > 1024 or s2[:4] != b"YST2":
        sys.exit("stage 2 missing or too big")
    return s2


def main():
    build, op = sys.argv[1], sys.argv[2]
    d = yoton.Dev()
    d.ensure_ymon()
    if op == "stage":
        pages = open(os.path.join(build, "rockbox.pages"), "rb").read()
        img = open(os.path.join(build, "rockbox.ym03"), "rb").read()
        if PAGES_NOR + len(pages) > RES_NOR:
            sys.exit(f"page store too big: {len(pages)}")
        if RES_NOR + 16 + len(img) > OFMB_NOR:
            sys.exit(f"resident image too big: {len(img)}")
        hdr = struct.pack("<4I", 0x53455259, len(img), sum(img) & 0xFFFFFFFF, 0)
        vend = open(os.path.join(ROOT, "dumps/nor_descrambled_8m.bin"), "rb").read(1024)
        assert vend[2] == 0xA5, "vendor boot record copy looks wrong"
        yoton.nor_write(d, PAGES_NOR, pages)
        yoton.nor_write(d, RES_NOR, hdr + img)
        # vendor record copy, then stage 2 of our boot stub at +0x400
        yoton.nor_write(d, OFMB_NOR, vend + stage2())
        print(f"staged: {len(pages)} B pages, {len(img)} B image, vendor record")
    elif op == "restore-vendor":
        # put the original sector 0 back (vendor boot record)
        orig = open(os.path.join(ROOT, "dumps/nor_backup_8m.bin"), "rb").read(0x1000)
        prog = open(os.path.join(ROOT, "payloads/norprog0.bin"), "rb").read()
        d.load(prog)
        r = struct.unpack("<4I", d.call(args=(1, 0, 0)))
        if r[0] != 0:
            sys.exit(f"erase failed: {r}")
        for p in range(0, 0x1000, 256):
            d.write(0x122000, orig[p:p + 256])
            r = struct.unpack("<4I", d.call(args=(2, p, 256)))
            if r[0] != 0:
                sys.exit(f"program failed at {p:#x}: {r}")
        back = yoton.nor_read_raw(d, 0, 0x1000)
        print("sector 0 restored:", "OK" if back == orig else "MISMATCH")
    elif op == "stubtest":
        rec = stub_record()
        d.write(0x122000, rec)
        if d.read(0x122000, len(rec)) != rec:
            sys.exit("stub upload verify failed")
        prog = open(os.path.join(ROOT, "payloads/stubjump.bin"), "rb").read()
        d.load(prog)
        try:
            d.call(args=(), timeout=2000)
        except Exception as e:
            print("jumped into stub (", type(e).__name__, ")")
    elif op == "flash-stub":
        if "--i-have-a-backup" not in sys.argv:
            sys.exit("refusing: pass --i-have-a-backup")
        orig = open(os.path.join(ROOT, "dumps/nor_backup_8m.bin"), "rb").read(0x1000)
        cur = yoton.nor_read_raw(d, 0, 0x1000)
        if cur[0x400:] != orig[0x400:]:
            sys.exit("sector 0 tail differs from the backup: not touching it")
        # stage 1 in 0x000-0x1FF; the vendor's next stage from 0x200 on as it was
        sector = stub_record() + orig[0x200:]
        assert len(sector) == 0x1000
        # same erase+program+verify as yoton.nor_write, with the sector-0 programmer
        prog = open(os.path.join(ROOT, "payloads/norprog0.bin"), "rb").read()
        d.load(prog)
        r = struct.unpack("<4I", d.call(args=(1, 0, 0)))
        if r[0] != 0:
            sys.exit(f"erase failed: {r}")
        for p in range(0, 0x1000, 256):
            d.write(0x122000, sector[p:p + 256])
            r = struct.unpack("<4I", d.call(args=(2, p, 256)))
            if r[0] != 0:
                sys.exit(f"program failed at {p:#x}: {r}")
        back = yoton.nor_read_raw(d, 0, 0x1000)
        print("sector 0 verify:", "OK" if back == sector else "MISMATCH")
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
