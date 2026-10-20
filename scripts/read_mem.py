#!/usr/bin/env python3
"""Read bytes from the GhidraMCP backend. usage: scripts/read_mem.py <hexaddr> <len> [u32|u8|hex]  (GHIDRA_URL/TOKEN honoured)"""
import json, pathlib, struct, sys, urllib.request
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from ghidra_conn import BASE
addr, n = sys.argv[1].replace("0x", ""), int(sys.argv[2]); fmt = sys.argv[3] if len(sys.argv) > 3 else "u32"
b = bytes(json.load(urllib.request.urlopen(f"{BASE}/read_memory?address={addr}&length={n}", timeout=60))["data"])
if fmt == "hex": print(b.hex())
elif fmt == "u8": print(list(b))
else:
    base = int(addr, 16)
    for i in range(0, len(b) - len(b) % 4, 4): print(f"{base + i:x}: {struct.unpack('<I', b[i:i+4])[0]:#x}")
