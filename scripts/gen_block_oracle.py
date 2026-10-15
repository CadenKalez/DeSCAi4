#!/usr/bin/env python3
"""Turn selected Ghidra decompiles from a work/<group>/<Class>.txt export into a compilable C oracle.
usage: gen_block_oracle.py <export.txt> <out.c> <0xaddr>...   (each function becomes oracle_<addr>)
Only for plain-C style bodies (no vtables / library calls)."""
import re, sys, pathlib
argv = sys.argv[1:]
tables = []
while "--table" in argv:  # --table SYMBOL:0xADDR:BYTES  (raw data referenced as DAT_*, read from the Mach-O)
    i = argv.index("--table"); tables.append(argv[i+1]); del argv[i:i+2]
subs = []
while "--sub" in argv:  # --sub 'REGEX=>REPLACEMENT' applied to every body (fix lost call arguments, rename callees)
    i = argv.index("--sub"); subs.append(argv[i+1].split("=>", 1)); del argv[i:i+2]
floatlits = set()
while "--floatlits" in argv:  # --floatlits 0xADDR: type every floating literal of that function as float (all-SSE-single code)
    i = argv.index("--floatlits"); floatlits.add(int(argv[i+1], 16)); del argv[i:i+2]
src, out, addrs = argv[0], argv[1], [int(a, 16) for a in argv[2:]]
text = pathlib.Path(src).read_text()
blocks = re.split(r"(?m)^(?=//==== )", text)
res = ["#include <stdint.h>", "#include <string.h>", "#include <stdbool.h>", "#include <math.h>", "#define ABS(x) fabs(x)", "#define SQRT(x) sqrtf(x)", "#define NAN(x) isnan(x)",
       "typedef unsigned char undefined, undefined1, byte, uchar, sVF_V3F, sVF_V3F_C4UB, sVF_V3F_T2F, sVF_V3F_2T2F, sVF_V3F_C4UB_T2F, sVF_V3F_C4UB_2T2F, eGDElementType, cGZGraphicSystem, cS3DMatrix3, cS3DVector3, cS3DBoundingBox, cS3DTransform;",
       "typedef unsigned short undefined2, ushort;", "typedef unsigned int undefined4, uint;",
       "typedef unsigned long undefined8, ulong;", "typedef long longlong;", "static long guard_val__; static long *PTR____stack_chk_guard_100a657c8 = &guard_val__;", "static void ___stack_chk_fail(void) { __builtin_trap(); }", "#define _memcpy memcpy", "#define __thiscall", "typedef void code();", "typedef unsigned char int7; typedef unsigned long ulong_;\n#define CONCAT71(a,b) ((unsigned long)(unsigned char)(b)) ", "#define CONCAT44(a,b) ((unsigned long)(unsigned)(b))"]
sys.path.insert(0, str(pathlib.Path(__file__).parent))
for t in tables:
    import macho_read
    sym, a, n = t.split(":")
    raw = macho_read.read(int(a, 16), int(n))
    res.append(f"static const unsigned char {sym}[{n}] = {{{', '.join(str(x) for x in raw)}}};")
for b in blocks:
    m = re.match(r"//==== 0x([0-9a-f]+)", b)
    if not m or int(m.group(1), 16) not in addrs:
        continue
    a = m.group(1)
    body = "\n".join(l for l in b.splitlines() if not l.startswith("//"))
    body = re.sub(r"\b(?:[A-Za-z_]\w*::)+~?\w+", f"oracle_{a}", body, count=1)
    for rx, rep in subs:
        body = re.sub(rx, rep, body)
    if int(a, 16) in floatlits:
        body = re.sub(r"(?<![\w.])(\d+\.\d+)(?![\w.])", r"\1f", body)
    body = re.sub(r"&(kVF\w+|vLookup\w+)", r"\1", body)
    body = re.sub(r"&(DAT_\w+)", r"\1", body)  # data blobs are byte arrays; keep byte-offset arithmetic
    res.append(body)
pathlib.Path(out).write_text("\n".join(res) + "\n")
