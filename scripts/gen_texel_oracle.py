#!/usr/bin/env python3
"""Turns the Ghidra decompilation of nsTexelOp::Convert_* (work/gz/nsTexelOp.txt) and the nRZSimGL DXT decoders
(scripts/dec_addrs.py output) into compilable C (work/texel/oracle.c) so src/render/TexelOps.cpp can be compared
bit for bit. Lookup tables come from the binary (scripts/macho_read.py)."""
import re, pathlib, struct, subprocess, sys
ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT/"scripts"))
import macho_read

text = (ROOT/"work/gz/nsTexelOp.txt").read_text()
blocks = re.split(r"(?m)^//==== ", text)[1:]
TABLES = {  # symbol -> (address, entries)
    "convert_A4_to_A_of_ARGB4": (0x100a26b20, 16), "convert_R4_to_R_of_ARGB4": (0x100a26b60, 16),
    "convert_G4_to_G_of_ARGB4": (0x100a26ba0, 16), "convert_B4_to_B_of_ARGB4": (0x100a26be0, 16),
    "convert_R5_to_R_of_A1RGB5": (0x100a26c20, 32), "convert_G5_to_G_of_A1RGB5": (0x100a26ca0, 32),
    "convert_A1_to_A_of_A1RGB5": (0x100a26d20, 2), "convert_R5_to_R_of_RGB565": (0x100a26d30, 32),
    "convert_G6_to_G_of_RGB565": (0x100a26db0, 64), "convert_B5_to_B_of_RGB565": (0x100a26eb0, 32),
}
data = macho_read.BIN.read_bytes(); segs = macho_read.segments(data)
out = ["#include <stdint.h>", "#include <string.h>", "#define _memcpy memcpy", "#define _memmove memmove", "#define Copy_4UB oracle_Copy_4UB", "#define Copy_1UB oracle_Copy_1UB", "void oracle_Copy_4UB(void*,void*,int,int,int,int);",
       "typedef unsigned char undefined, undefined1, byte, uchar;", "typedef unsigned short undefined2, ushort;",
       "typedef unsigned int undefined4, uint;", "typedef unsigned long undefined8, ulong;", "typedef long longlong;",
       "typedef short sshort;"]
for name, (addr, n) in TABLES.items():
    vals = struct.unpack(f"<{n}I", macho_read.read(addr, n*4, data, segs))
    out.append(f"static const unsigned {name}[{n}] = {{{', '.join(str(v) for v in vals)}}};")
names = []
for b in blocks:
    head, _, code = b.partition("\n")
    m = re.search(r"nsTexelOp::(Convert_\w+|Copy_\w+)\(", head)
    if not m or "nRZSimGL" in code: continue
    name = m.group(1)
    code = code.split("\n", 2)[2] if code.startswith("//") else code
    code = re.sub(r"(?m)^//.*$", "", code)
    code = re.sub(r"nsTexelOp::", "", code)
    code = re.sub(r"(?m)^\s*if \([^\n]*::once == '\\0'\) \{\n[^\n]*::once = [^\n]*\n\s*\}", "", code)
    code = re.sub(r"if \(\s*\) \{\s*\}", "", code)
    code = re.sub(r"\(void \*param_1,void \*param_2,int param_3,int param_4,int param_5,int param_6,\.\.\.\)",
                  "(void *param_1,void *param_2,int param_3,int param_4,int param_5,int param_6)", code)
    code = re.sub(r"void\s+Copy_(\w+)\s*\(void \*param_1,void \*param_2,int param_3,int param_4,int param_5,int param_6\s*,\.\.\.\)",
                  r"void Copy_\1(void *param_1,void *param_2,int param_3,int param_4,int param_5,int param_6)", code)
    code = code.replace(",...)", ")")
    code = re.sub(r"&(convert_\w+)", r"\1", code)
    code = re.sub(r"\(undefined4 \*\)\s*\n?\s*\(\s*(convert_\w+) \+ \(ulong\)", r"(const unsigned*)((const unsigned char*)\1 + (ulong)", code)
    out.append(f"/* {name} */\n" + re.sub(r"\bvoid Convert_", "void oracle_Convert_", re.sub(r"\bvoid Copy_", "void oracle_Copy_", code)))
    names.append(name)
(ROOT/"work/texel/oracle_raw.c").write_text("\n".join(out))
print(len(names))
