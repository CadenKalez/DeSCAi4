#!/usr/bin/env python3
"""Draft pure-virtual interface headers from an implementer's vtable groups.

usage: scripts/gen_interfaces.py <ImplementerClass> [--out src/audio/interfaces]
For each base class listed in the RTTI (in layout order) the matching vtable group is turned into
a `class cIxxx : public cIGZUnknown` with its methods in slot order. Return types come from the
decompiled signatures in work/**.txt when available (else `/*unknown*/ int`).
Output is a DRAFT: overloaded names are suffixed `_Overload<n>` and must be renamed by hand.
"""
import re, sys, pathlib, argparse, importlib.util, collections
ROOT = pathlib.Path(__file__).resolve().parent.parent
def load(name, path):
    s = importlib.util.spec_from_file_location(name, path); m = importlib.util.module_from_spec(s); s.loader.exec_module(m); return m
vt = load("vt", ROOT/"scripts/vtable.py")

# base classes, from RTTI
bases = {}
for line in (ROOT/"notes/typeinfo-demangled.txt").read_text().splitlines():
    pass
import subprocess
def hierarchy(cls):
    out = subprocess.run([sys.executable, str(ROOT/"scripts/hierarchy.py"), f"^{re.escape(cls)}$"], capture_output=True, text=True).stdout
    m = re.match(r".*?  :  (.*)", out.strip())
    return [b.strip() for b in m.group(1).split(",")] if m else []

# Index every exported function: normalized "method(params) const" -> best (largest) implementation.
impl = {}   # key -> (size, ret_decl_text, trivial_body)
def norm_key(demangled):
    d = re.sub(r"^(non-virtual thunk to |virtual thunk to )", "", demangled)
    head, _, rest = d.partition("(")
    return head.split("::")[-1] + "(" + rest
for f in (ROOT/"work").rglob("*.txt"):
    blocks = re.split(r"\n(?=//==== )", f.read_text(errors="replace"))
    for blk in blocks:
        m = re.match(r"//==== (0x[0-9a-f]+)  (.*)\n// size=(\d+)", blk)
        if not m or m.group(2).startswith(("non-virtual thunk", "virtual thunk")): continue
        size = int(m.group(3)); key = norm_key(m.group(2))
        lines = [l for l in blk.splitlines()[2:] if l.strip() and not l.lstrip().startswith("//")]
        sig = next((l for l in lines if "(" in l and not l.startswith(" ")), "")
        body = " ".join(l.strip() for l in lines[lines.index(sig)+1:]) if sig in lines else ""
        trivial = bool(re.fullmatch(r"\{\s*return [01];\s*\}|\{\s*return;\s*\}", body))
        if key not in impl or size > impl[key][0]:
            impl[key] = (size, sig, trivial)

TYPEMAP = {"undefined1": "bool", "undefined4": "uint32_t", "undefined2": "uint16_t", "undefined8": "uint64_t",
           "undefined": "int", "uint": "uint32_t", "ulong": "uint64_t", "ulonglong": "uint64_t", "longlong": "int64_t",
           "long": "int64_t", "byte": "uint8_t", "ushort": "uint16_t", "char": "char"}
def ret_type_for(key):
    size, sig, trivial = impl.get(key, (0, "", False))
    if not sig: return "int /*unrecovered*/"
    s_ = re.sub(r"__thiscall|__stdcall|__cdecl|__fastcall", "", sig).strip()
    m = re.match(r"^(.*?)\s*\**\s*[\w:~<>]+\s*\(", s_)
    rt = (m.group(1).strip() if m else "").strip() or "void"
    ptr = "*" in s_.split("(")[0]
    if ptr and not rt.endswith("*"): rt += " *"
    rt = TYPEMAP.get(rt, rt)
    if rt == "uint64_t" and trivial: rt = "bool"   # stub `return 0/1` in rax
    return rt

def parse_demangled(n):
    n = re.sub(r"^non-virtual thunk to ", "", n)
    head, _, rest = n.partition("(")
    method = head.split("::")[-1]
    params = rest.rsplit(")", 1)[0]
    const = n.rstrip().endswith("const")
    return method, params, const

def groups_of(cls):
    base = vt.vtables()[cls]; end = vt.vtable_end(base)
    out = []; p = base
    while p < end:
        off = vt.read_u64(p); ti = vt.read_u64(p + 8)
        if ti is None: break
        p += 16; slots = []
        while p < end:
            t = vt.read_u64(p)
            if t is None or not (vt.TEXT[1] <= t < vt.TEXT[1] + vt.TEXT[2]): break
            slots.append((t, vt.names.get(t, "?"))); p += 8
        out.append(slots)
        if not slots: break
    return out

def main():
    ap = argparse.ArgumentParser(); ap.add_argument("cls"); ap.add_argument("--out", default="src/audio/interfaces")
    a = ap.parse_args()
    bs = hierarchy(a.cls); gs = groups_of(a.cls)
    outdir = ROOT/a.out; outdir.mkdir(parents=True, exist_ok=True)
    print(f"{a.cls}: bases={bs}  groups={len(gs)}")
    for i, b in enumerate(bs):
        if i >= len(gs) or not b.startswith("cI"): continue
        slots = gs[i]
        if i == 0:  # primary: interface slots end before the implementer's first destructor
            cut = next((k for k, (_, n) in enumerate(slots) if "::~" in n), len(slots))
            slots = slots[:cut]
        seen = collections.Counter(); lines = []
        for k, (addr, n) in enumerate(slots):
            if k < 3: continue  # QueryInterface/AddRef/Release come from cIGZUnknown
            m, params, const = parse_demangled(n)
            seen[m] += 1
            lines.append((k, addr, m, params, const))
        dup = {m for m, c in seen.items() if c > 1}
        idx = collections.Counter()
        body = []
        for k, addr, m, params, const in lines:
            name = m
            if m in dup:
                idx[m] += 1; name = f"{m}_Overload{idx[m]}"
            body.append(f"    // [{k}] +{k*8:#x}  {a.cls}::{m}({params}) @ {addr:#x}\n"
                        f"    virtual {ret_type_for(norm_key(vt.names.get(addr, '?')))} {name}({params}){' const' if const else ''} = 0;")
        txt = (f"// {b} — DRAFT generated by scripts/gen_interfaces.py from {a.cls}'s vtable group {i}.\n"
               f"// Slot order is authoritative; names marked _OverloadN and parameter names need hand-work.\n"
               f"#pragma once\n#include <cstdint>\n#include \"gz/cIGZUnknown.h\"\n\n"
               f"class {b} : public cIGZUnknown {{\npublic:\n" + "\n".join(body) + "\n};\n")
        (outdir/f"{b}.h").write_text(txt)
        print(f"  {b}: {len(lines)} methods  (dups: {sorted(dup)})")

main()
