#!/usr/bin/env python3
"""Dump a class vtable straight from the Mach-O: slot index, target address, demangled name.

usage: scripts/vtable.py cRZString [more classes...]
Uses notes/vtables-demangled.txt for vtable addresses and notes/symbols-functions-demangled.txt
(+ nm for non-text) for names. Slots stop at the first non-code pointer (next vtable's header).
"""
import struct, sys, pathlib, bisect, re
ROOT = pathlib.Path(__file__).resolve().parent.parent
BIN = ROOT/"game-binaries/MacOS/Sim City 4 Deluxe Editionsub"
data = BIN.read_bytes()

def segments():
    ncmds = struct.unpack_from("<I", data, 16)[0]; off = 32; segs = []
    for _ in range(ncmds):
        cmd, size = struct.unpack_from("<II", data, off)
        if cmd == 0x19:  # LC_SEGMENT_64
            name = data[off+8:off+24].rstrip(b"\0").decode()
            vmaddr, vmsize, fileoff, filesize = struct.unpack_from("<QQQQ", data, off+24)
            segs.append((name, vmaddr, vmsize, fileoff, filesize))
        off += size
    return segs
SEGS = segments()
TEXT = next(s for s in SEGS if s[0] == "__TEXT")

def read_u64(vm):
    for _, va, vs, fo, fs in SEGS:
        if va <= vm < va + min(vs, fs or vs):
            return struct.unpack_from("<Q", data, fo + vm - va)[0]
    return None

names = {}
for line in (ROOT/"notes/symbols-functions-demangled.txt").read_text(errors="replace").splitlines():
    a, _, n = line.partition(" "); names.setdefault(int(a, 16), n)
addrs = sorted(names)

def vtables():
    out = {}
    for line in (ROOT/"notes/vtables-demangled.txt").read_text().splitlines():
        a, _, n = line.partition(" ")
        out[n.replace("vtable for ", "")] = int(a, 16)
    return out

_ti_names = None
def typeinfo_name(addr):
    global _ti_names
    if _ti_names is None:
        _ti_names = {}
        for line in (ROOT/"notes/typeinfo-demangled.txt").read_text().splitlines():
            a, _, n = line.partition(" "); _ti_names[int(a, 16)] = n.replace("typeinfo for ", "")
    return _ti_names.get(addr, f"?{addr:#x}")

_all_syms = None
def vtable_end(base):
    """Address where the vtable symbol ends = next symbol of any kind (from nm)."""
    global _all_syms
    if _all_syms is None:
        _all_syms = sorted({int(l.split()[0], 16) for l in (ROOT/"notes/symbols-nm.txt").read_text().splitlines() if l.strip()})
    i = bisect.bisect_right(_all_syms, base)
    return _all_syms[i] if i < len(_all_syms) else base + 0x400

def dump(cls, vt):
    """Dumps every sub-vtable group (primary + secondary bases, each preceded by
    offset-to-top and a typeinfo pointer)."""
    base = vt[cls]; end = vtable_end(base)
    print(f"== {cls}  vtable @ {base:#x} .. {end:#x}")
    p = base; group = 0
    while p < end:
        off = read_u64(p); ti = read_u64(p + 8)
        if ti is None: break
        sgn = off - (1 << 64) if off >= (1 << 63) else off
        print(f"  -- group {group}: offset-to-top={sgn} typeinfo={typeinfo_name(ti)}  (vptr -> {p+16:#x})")
        p += 16; slot = 0
        while p < end:
            t = read_u64(p)
            if t is None or not (TEXT[1] <= t < TEXT[1] + TEXT[2]):
                break
            nm = names.get(t)
            if nm is None:
                i = bisect.bisect_right(addrs, t) - 1
                nm = f"<{names[addrs[i]]} +{t-addrs[i]:#x}>" if i >= 0 else "?"
            print(f"     [{slot:3d}] +{slot*8:#05x}  {t:#x}  {nm}")
            slot += 1; p += 8
        group += 1
        if slot == 0: break

if __name__ == "__main__":
    vt = vtables()
    for c in sys.argv[1:]:
        if c not in vt: print(f"no vtable for {c}"); continue
        dump(c, vt)
