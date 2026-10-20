#!/usr/bin/env python3
"""Print a vtable (slot offset -> function name) through GhidraMCP when the game binary is not available locally.
usage: scripts/vtable_ghidra.py <vtable-hex-addr> [slots=48]   (the address a constructor stores at +0, i.e. PTR_..._<addr>)"""
import json, pathlib, struct, sys, urllib.request
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from ghidra_conn import BASE
ROOT = pathlib.Path(__file__).resolve().parent.parent
names = {}
for l in (ROOT/"notes/symbols-functions-demangled.txt").read_text(errors="replace").splitlines():
    a, _, n = l.partition(" "); names[int(a, 16)] = n
base, n = int(sys.argv[1], 16), int(sys.argv[2]) if len(sys.argv) > 2 else 48
b = bytes(json.load(urllib.request.urlopen(f"{BASE}/read_memory?address={base:x}&length={n*8}", timeout=60))["data"])
for i in range(n):
    p = struct.unpack_from("<Q", b, i * 8)[0]
    print(f"+{i*8:#x} {p:x} {names.get(p, '?')}")
