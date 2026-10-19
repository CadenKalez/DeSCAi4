#!/usr/bin/env python3
"""List the strings a decompiled region refers to: data addresses (0x1009xxxxx -> C string from the Mach-O)
and stack-built literals (little-endian integer constants that decode to printable ASCII).
usage: scripts/strs.py FILE [FIRST_LINE LAST_LINE]"""
import re, struct, sys, pathlib
ROOT = pathlib.Path(__file__).resolve().parent.parent
data = (ROOT/"game-binaries/MacOS/Sim City 4 Deluxe Editionsub").read_bytes()
ncmds = struct.unpack_from("<I", data, 16)[0]; off = 32; segs = []
for _ in range(ncmds):
    cmd, size = struct.unpack_from("<II", data, off)
    if cmd == 0x19:
        vm, vs, fo, fs = struct.unpack_from("<QQQQ", data, off+24); segs.append((vm, vs, fo, fs))
    off += size
def cstr(addr):
    for vm, vs, fo, fs in segs:
        if vm <= addr < vm+vs and fo:
            i = fo + addr - vm; j = data.index(b"\0", i)
            return data[i:j].decode("latin1")
lines = pathlib.Path(sys.argv[1]).read_text().splitlines()
a = int(sys.argv[2]) if len(sys.argv) > 2 else 1
b = int(sys.argv[3]) if len(sys.argv) > 3 else len(lines)
seen = set()
for n in range(a-1, min(b, len(lines))):
    l = lines[n]
    for m in re.finditer(r"0x(1009[0-9a-f]{5}|100a[0-9a-f]{5})\b", l):
        s = cstr(int(m.group(1), 16))
        if s is not None and 0 < len(s) < 200 and all(32 <= ord(c) < 127 or c in "\n\t" for c in s):
            print(f"{n+1}: addr 0x{m.group(1)} = {s!r}")
    for m in re.finditer(r"0x([0-9a-f]{6,16})\b", l):
        v = int(m.group(1), 16)
        raw = v.to_bytes((v.bit_length()+7)//8, "little")
        if len(raw) >= 3 and all(32 <= c < 127 for c in raw):
            print(f"{n+1}: lit 0x{m.group(1)} = {raw.decode()!r}")
    for m in re.finditer(r'builtin_strncpy\([^,]+,"([^"]*)"', l):
        print(f"{n+1}: strncpy {m.group(1)!r}")
