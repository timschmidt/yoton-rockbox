#!/usr/bin/env python3
"""Pager fault profile -> hot input sections.

ym03_prof.py BUILDDIR PROF.bin [--add]
PROF.bin is framebuffer row 64 read back after a test run: "YPRF", count,
then fault addresses. Addresses are mapped to input sections through
BUILDDIR/rockbox.map. With --add the counts are merged into
BUILDDIR/ym03_hot.txt, which ym03_pagelink.py packs first."""
import os, re, struct, sys, bisect, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ym03_pagelink as P

build, prof = sys.argv[1], sys.argv[2]
secs = [s for s in P.parse_map(os.path.join(build, "rockbox.map"))
        if s[0] and s[0].startswith((".pg_", ".pr_")) and s[3]]
secs.sort(key=lambda s: s[2])
starts = [s[2] for s in secs]

def find(a):
    i = bisect.bisect_right(starts, a) - 1
    if i >= 0 and a < secs[i][2] + secs[i][3]:
        return secs[i]
    return None

d = open(prof, "rb").read()
magic, n = struct.unpack_from("<II", d)
assert magic == 0x46525059, hex(magic)
n = min(n, (len(d) - 8) // 4)
c = collections.Counter()
miss = 0
for i in range(n):
    a = struct.unpack_from("<I", d, 8 + 4 * i)[0]
    s = find(a)
    if s:
        c[(P.lds_file_pattern(s[4]), s[1], s[3])] += 1
    else:
        miss += 1
print(f"{n} faults, {miss} outside paged sections")
for (f, sec, sz), v in c.most_common(40):
    print(f"{v:5} {sz:6} {sec:40} {f.split('/')[-1]}")
if "--add" in sys.argv:
    hot = collections.Counter()
    path = os.path.join(build, "ym03_hot.txt")
    if os.path.exists(path):
        for l in open(path):
            v, f, sec = l.split()
            hot[(f, sec)] += int(v)
    for (f, sec, sz), v in c.items():
        hot[(f, sec)] += v
    with open(path, "w") as fo:
        for (f, sec), v in hot.most_common():
            fo.write(f"{v} {f} {sec}\n")
    print(f"{len(hot)} hot sections in {path}")
