#!/usr/bin/env python3
"""Print the x86_64 disassembly of one function from $TMPDIR/sc4.asm: disfn.py 0xADDR [maxlines]"""
import os, sys, bisect
asm = os.path.join(os.environ.get("TMPDIR", "/tmp"), "sc4.asm")
a = int(sys.argv[1], 16); n = int(sys.argv[2]) if len(sys.argv) > 2 else 400
key = f"{a:016x}"
out = 0; on = False
for line in open(asm, errors="replace"):
    if not on:
        if line.startswith(key): on = True
        else: continue
    print(line.rstrip()[:130]); out += 1
    if out >= n or (out > 1 and line.startswith("_") and ":" in line): break
