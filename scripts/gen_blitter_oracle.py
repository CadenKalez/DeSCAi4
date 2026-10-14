#!/usr/bin/env python3
"""Turn the Ghidra decompilation of every nGZDrawSoftRender::cBlitter<Dst,Src,bool> method into a
compilable C oracle (work/blit/oracle.c + manifest.json) so the hand-written template can be checked
against the original behaviour bit for bit.

input : work/blit/all.txt   (scripts/dec_addrs.py --grep 'nGZDrawSoftRender::cBlitter<')
output: work/blit/oracle.c  one function per original, `oracle_<addr>(params..., unsigned char *stk)`;
        stack arguments (the by-value descriptors) are read from stk at the offsets the original used:
        +0x08 dst pixels, +0x10 dst pitch, +0x28 src pixels, +0x30 src pitch, +0x38/+0x3c src w/h,
        +0x48/+0x4c start x/y, +0x50/+0x54 step x/y, +0x58/+0x5c colour, +0x60 flag/colour key.
"""
import re, json, pathlib
ROOT = pathlib.Path(__file__).resolve().parent.parent
text = (ROOT/"work/blit/all.txt").read_text()
blocks = re.split(r"(?m)^//==== ", text)[1:]
FORMATS = ["0u, 0u, 0u, 0u, 1, 0", "0u, 16711680u, 65280u, 255u, 4, 8", "0u, 31744u, 992u, 31u, 2, 5",
           "0u, 3840u, 240u, 15u, 2, 4", "0u, 63488u, 2016u, 31u, 2, 5", "255u, 0u, 0u, 0u, 1, 0",
           "32768u, 31744u, 992u, 31u, 2, 5", "4278190080u, 16711680u, 65280u, 255u, 4, 8", "61440u, 3840u, 240u, 15u, 2, 4"]
prelude = """#include <stdint.h>
#include <string.h>
#define _memset memset
#define _memcpy memcpy
#define _memmove memmove
typedef unsigned char undefined, undefined1, byte, uchar, bool;
typedef unsigned short undefined2, ushort;
typedef unsigned int undefined4, uint;
typedef unsigned long undefined8, ulong;
typedef long longlong;
typedef short sshort;
"""
out = [prelude]
manifest = []
for b in blocks:
    head, _, code = b.partition("\n")
    m = re.match(r"(0x[0-9a-f]+) nGZDrawSoftRender::cBlitter<nGZDrawSoftRender::RGBFormat<([^>]*)>, nGZDrawSoftRender::RGBFormat<([^>]*)>, (true|false)>::(\w+)\(", head)
    if not m:
        print("skip", head[:100]); continue
    addr, dst, src, flag, method = m.groups()
    # header: everything up to the first '{' at line start
    hm = re.search(r"(?m)^\{", code)
    header, body = code[:hm.start()], code[hm.start():]
    header = re.sub(r"(?m)^//.*$", "", header).strip()
    rt, _, rest = header.partition("nGZDrawSoftRender::")
    ret = rt.strip() or "void"
    params = re.search(r"\(([^()]*)\)\s*$", header).group(1)
    ret = {"undefined8 *": "void *", "undefined4 *": "void *", "undefined2 *": "void *", "undefined1 *": "void *",
           "byte *": "void *", "uint *": "void *"}.get(ret, ret)
    # stack variable declarations -> macros
    macros, undef = [], []
    def decl(mm):
        ty, star, name, off = mm.group(1), mm.group(2) or "", mm.group(3), mm.group(4)
        macros.append(f"#define {name} (*({ty} {star}*)(stk + 0x{int(off,16):x}))")
        undef.append(name)
        return ""
    body = re.sub(r"(?m)^\s*([A-Za-z_][A-Za-z0-9_]*)\s+(\**)\s*((?:in_stack_|[a-zA-Z]Stack)([0-9a-f]{8,16}));\s*$",
                  lambda mm: decl(mm), body)
    used = set(re.findall(r"\b(_?(?:in_stack_|[a-zA-Z]Stack)[0-9a-f]{8,16})\b", body))
    for name in sorted(used):
        if name in undef: continue
        off = int(re.search(r"([0-9a-f]{8,16})$", name).group(1), 16)
        macros.append(f"#define {name} (*(unsigned long *)(stk + 0x{off:x}))")
        undef.append(name)
    for mm in sorted(set(re.findall(r'\b(stack0x[0-9a-f]{8})\b', body))):
        macros.append(f"#define {mm} (*(unsigned char *)(stk + 0x{int(mm[7:],16):x}))")
        undef.append(mm)
    # sub-field access name._4_4_  -> bytes at offset
    def sub(mm):
        base, off, size = mm.group(1), int(mm.group(2)), int(mm.group(3))
        ty = {1: "unsigned char", 2: "unsigned short", 4: "unsigned int"}[size]
        return f"(*({ty} *)((unsigned char *)&{base} + {off}))"
    body = re.sub(r"\b([A-Za-z_][A-Za-z0-9_]*)\._(\d+)_(\d+)_", sub, body)
    func = f"{ret} oracle_{addr[2:]}({params}, unsigned char *stk)"
    out.append("\n".join(macros))
    out.append(func + "\n" + body)
    out.append("\n".join(f"#undef {n}" for n in undef))
    manifest.append({"addr": addr[2:], "dst": FORMATS.index(dst), "src": FORMATS.index(src), "flag": flag == "true",
                     "method": method, "ret": ret, "params": params})
(ROOT/"work/blit/oracle.c").write_text("\n".join(out))
(ROOT/"work/blit/manifest.json").write_text(json.dumps(manifest))
print(len(manifest), "functions")
