#!/usr/bin/env python3
"""Tiny Thumb-2 cross-referencer over objdump output (no Ghidra needed).

xref.py BIN BASE DIS funcs            list functions (bl targets) with peripheral constants used
xref.py BIN BASE DIS refs CONST_PREFIX functions referencing constants starting with prefix (hex)
xref.py BIN BASE DIS show ADDR        print the function containing ADDR
xref.py BIN BASE DIS callers ADDR     list call sites of function ADDR
"""
import re, struct, sys, bisect

def load(binf, base, disf):
    data = open(binf, 'rb').read()
    ins = []  # (addr, text)
    for line in open(disf):
        m = re.match(r'\s*([0-9a-f]+):\s+(?:[0-9a-f]{4} ?){1,2}\s+(.*)', line)
        if m:
            ins.append((int(m.group(1), 16), m.group(2).strip()))
    return data, ins

def lit(data, base, a):
    o = a - base
    return struct.unpack_from('<I', data, o)[0] if 0 <= o < len(data) - 3 else None

def analyze(data, base, ins):
    calls = {}
    for a, t in ins:
        m = re.match(r'bl(?:x)?\s+0x([0-9a-f]+)', t) or re.match(r'bl(?:x)?\s+([0-9a-f]+)\b', t)
        if m:
            calls.setdefault(int(m.group(1), 16), []).append(a)
    funcs = sorted(calls)
    return calls, funcs

def func_of(funcs, a):
    i = bisect.bisect_right(funcs, a) - 1
    return funcs[i] if i >= 0 else None

def consts(data, base, ins):
    out = []
    for a, t in ins:
        m = re.search(r'\[pc, #\d+\]\s+@ \((0x)?([0-9a-f]+)', t)
        if m and t.startswith('ldr'):
            v = lit(data, base, int(m.group(2), 16))
            if v is not None:
                out.append((a, v))
    return out

def main():
    binf, base, disf, op = sys.argv[1], int(sys.argv[2], 0), sys.argv[3], sys.argv[4]
    data, ins = load(binf, base, disf)
    calls, funcs = analyze(data, base, ins)
    cs = consts(data, base, ins)
    if op == 'funcs':
        per = {}
        for a, v in cs:
            if v >= 0xc0000000 and v < 0xc1000000:
                per.setdefault(func_of(funcs, a), set()).add(v)
        for f in funcs:
            if f in per:
                print(f"{f:08x} calls={len(calls[f]):3d} " + ' '.join(f"{v:08x}" for v in sorted(per[f])))
    elif op == 'refs':
        pre = sys.argv[5].lower()
        seen = {}
        for a, v in cs:
            if f"{v:08x}".startswith(pre):
                seen.setdefault(func_of(funcs, a), set()).add(v)
        for f, vs in sorted(seen.items(), key=lambda x: x[0] or 0):
            print(f"{f or 0:08x} " + ' '.join(f"{v:08x}" for v in sorted(vs)))
    elif op == 'show':
        a0 = int(sys.argv[5], 16)
        f = func_of(funcs, a0)
        i = bisect.bisect_right(funcs, f)
        end = funcs[i] if i < len(funcs) else f + 0x400
        lits = dict(cs)
        for a, t in ins:
            if f <= a < end:
                extra = f"   ; ={lits[a]:#010x}" if a in lits else ''
                print(f"{a:08x}: {t}{extra}")
    elif op == 'callers':
        for c in calls.get(int(sys.argv[5], 16), []):
            print(f"{c:08x} in {func_of(funcs, c):08x}")

if __name__ == '__main__':
    main()
