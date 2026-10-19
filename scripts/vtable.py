#!/usr/bin/env python3
"""Print the vtable(s) of a class with slot offsets and the functions they point to.
usage: scripts/vtable.py ClassName [more-classes]   (reads the game binary + notes/symbols)"""
import pathlib, struct, sys, bisect
ROOT = pathlib.Path(__file__).resolve().parent.parent
data = (ROOT/"game-binaries/MacOS/Sim City 4 Deluxe Editionsub").read_bytes()
ncmds = struct.unpack_from("<I", data, 16)[0]; off = 32; segs = []
for _ in range(ncmds):
    cmd, size = struct.unpack_from("<II", data, off)
    if cmd == 0x19:
        vm, vs, fo, fs = struct.unpack_from("<QQQQ", data, off+24); segs.append((vm, vs, fo, fs))
    off += size
def va2off(va):
    for vm, vs, fo, fs in segs:
        if vm <= va < vm+vs: return fo+va-vm
names = {}
for line in (ROOT/"notes/symbols-functions-demangled.txt").read_text(errors="replace").splitlines():
    a, _, n = line.partition(" ")
    names[int(a, 16)] = n
vts = sorted((int(l[:16], 16), l[17:].replace("vtable for ", "")) for l in (ROOT/"notes/vtables-demangled.txt").read_text().splitlines() if l)
starts = [v[0] for v in vts]
def show(cls):
    for i, (addr, name) in enumerate(vts):
        if name != cls: continue
        end = vts[i+1][0] if i+1 < len(vts) else addr+0x400
        a = addr; group = 0
        while a < end:
            otop = struct.unpack_from("<q", data, va2off(a))[0]
            print(f"-- {cls} group at +{-otop:#x} (vtable {a:#x})")
            a += 16; slot = 0
            while a < end:
                p = struct.unpack_from("<Q", data, va2off(a))[0]
                if p == 0 and slot > 0 and struct.unpack_from("<Q", data, va2off(a+8))[0] in (0,) and a+16 < end and False: break
                if p in names or p == 0 or (p & 0xfffffff000000000) == 0x1000000000 and va2off(p):
                    if p == 0 and slot > 0: break
                    print(f"  [{slot:3d}] {slot*8:#05x}  {names.get(p, hex(p))}")
                    slot += 1; a += 8
                else:
                    break
            # next group: skip to an aligned header (offset-to-top is a small negative/zero, followed by a typeinfo ptr)
            while a < end:
                v = struct.unpack_from("<q", data, va2off(a))[0]
                nxt = struct.unpack_from("<Q", data, va2off(a+8))[0] if va2off(a+8) else 0
                if (v == 0 or -0x1000 < v < 0) and nxt: break
                a += 8
for c in sys.argv[1:]: show(c)
