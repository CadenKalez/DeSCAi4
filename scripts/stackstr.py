#!/usr/bin/env python3
"""Recover libc++ short-string literals a function builds on its stack from the x86_64 disassembly.
usage: scripts/stackstr.py 0xSTART 0xEND     (needs $TMPDIR/sc4.asm = otool -arch x86_64 -tV <binary>)
Prints, at each call, the printable strings that were stored in rbp/rsp-relative slots since the previous call."""
import os, re, sys, struct, collections
asm = os.path.join(os.environ.get("TMPDIR", "/tmp"), "sc4.asm")
lo, hi = int(sys.argv[1], 16), int(sys.argv[2], 16)
mem = {}; dirty = set()
rx = re.compile(r"^([0-9a-f]{16})\t(movabsq|movq|movl|movw|movb)\t\$(-?0x[0-9a-f]+), (-?0x[0-9a-f]+)\(%(rbp|rsp)\)")
rxi = re.compile(r"^([0-9a-f]{16})\t(movabsq|movq|movl|movw|movb|movabs)\t\$(-?0x[0-9a-f]+), %(\w+)")
rxs = re.compile(r"^([0-9a-f]{16})\t(movq|movl|movw|movb)\t%(\w+), (-?0x[0-9a-f]+)\(%(rbp|rsp)\)")
rxz = re.compile(r"^([0-9a-f]{16})\t(xorl|xorps)\t%(\w+), %(\w+)")
BASE = {}
for base, names in {"a": ["rax","eax","ax","al"], "b": ["rbx","ebx","bx","bl"], "c": ["rcx","ecx","cx","cl"], "d": ["rdx","edx","dx","dl"],
                    "si": ["rsi","esi","si","sil"], "di": ["rdi","edi","di","dil"]}.items():
    for n in names: BASE[n] = base
for i in range(8, 16):
    for suf in ["", "d", "w", "b"]: BASE[f"r{i}{suf}"] = f"r{i}"
regs = {}
size = {"movabsq": 8, "movq": 8, "movl": 4, "movw": 2, "movb": 1}
started = False
def flush(addr):
    if not dirty: return
    out = []
    keys = sorted(k for k in mem if k in dirty or (k-1) in dirty)
    runs, cur = [], []
    for k in sorted(mem):
        if cur and k == cur[-1] + 1: cur.append(k)
        else:
            if cur: runs.append(cur)
            cur = [k]
    if cur: runs.append(cur)
    for r in runs:
        if not any(k in dirty for k in r): continue
        b = bytes(mem[k] for k in r)
        for m in re.finditer(rb"[ -~]{3,}", b):
            out.append(m.group().decode())
    if out: print(f"{addr:x}: {out}")
    dirty.clear()
for line in open(asm, errors="replace"):
    m = line.split("\t", 1)
    if len(m[0]) != 16: continue
    try: a = int(m[0], 16)
    except ValueError: continue
    if a < lo: continue
    if a > hi: break
    lp = re.search(r'literal pool for: (".*")\s*$', line)
    if lp: print(f"{a:x}: pool {lp.group(1)}")
    r = rx.match(line)
    if r:
        n = size[r.group(2)]; v = int(r.group(3), 16) & ((1 << (8*n)) - 1)
        d = int(r.group(4), 16)
        if r.group(5) == "rsp": d += 1 << 40
        for i in range(n):
            mem[d+i] = (v >> (8*i)) & 0xff; dirty.add(d+i)
    elif rxi.match(line):
        r = rxi.match(line); b = BASE.get(r.group(4))
        if b: regs[b] = int(r.group(3), 16) & 0xffffffffffffffff
    elif rxs.match(line):
        r = rxs.match(line); b = BASE.get(r.group(3)); n = size[r.group(2)]
        if b in regs:
            v = regs[b] & ((1 << (8*n)) - 1); d = int(r.group(4), 16)
            if r.group(5) == "rsp": d += 1 << 40
            for i in range(n):
                mem[d+i] = (v >> (8*i)) & 0xff; dirty.add(d+i)
    elif rxz.match(line):
        r = rxz.match(line)
        if r.group(3) == r.group(4) and BASE.get(r.group(3)): regs[BASE[r.group(3)]] = 0
    elif "\tcallq\t" in line:
        flush(a)
        # a call usually follows; keep memory (strings may be reused)
flush(hi)
