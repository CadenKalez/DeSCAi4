#!/usr/bin/env python3
"""Print base classes of classes matching a regex, read from Itanium RTTI (typeinfo objects).
usage: scripts/hierarchy.py '<regex>'"""
import re, struct, sys, pathlib
ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT/"scripts"))
import importlib.util
spec = importlib.util.spec_from_file_location("vt", ROOT/"scripts/vtable.py"); vt = importlib.util.module_from_spec(spec); spec.loader.exec_module(vt)

ti = {}  # addr -> name
for line in (ROOT/"notes/typeinfo-demangled.txt").read_text().splitlines():
    a, _, n = line.partition(" "); ti[int(a, 16)] = n.replace("typeinfo for ", "")
byname = {n: a for a, n in ti.items()}

def bases(addr):
    # layout: +0 vptr(type_info subclass vtable), +8 name ptr, then base info
    vptr = vt.read_u64(addr)
    kind = ti.get(vptr - 0, "")  # vptr points inside the class_type_info vtable; not directly named
    # distinguish by shape: si_class_type_info -> one base pointer at +16 (a known typeinfo)
    b1 = vt.read_u64(addr + 16)
    if b1 in ti: return [ti[b1]]
    # vmi_class_type_info: flags(u32) base_count(u32) then (ptr, offset_flags) pairs at +16
    flags, count = struct.unpack("<II", vt.data[vt.TEXT[3] + 0:vt.TEXT[3] + 0][:0] or b"\0" * 8)
    return []

def vmi(addr):
    def u32(vm):
        for _, va, vs, fo, fs in vt.SEGS:
            if va <= vm < va + min(vs, fs or vs): return struct.unpack_from("<I", vt.data, fo + vm - va)[0]
    flags, count = u32(addr + 16), u32(addr + 20)
    if not count or count > 16: return []
    out = []
    for i in range(count):
        p = vt.read_u64(addr + 24 + 16 * i)
        if p in ti: out.append(ti[p])
    return out

pat = re.compile(sys.argv[1])
for addr, name in sorted(ti.items(), key=lambda x: x[1]):
    if not pat.search(name): continue
    b = bases(addr) or vmi(addr)
    print(f"{name}" + (f"  :  {', '.join(b)}" if b else ""))
