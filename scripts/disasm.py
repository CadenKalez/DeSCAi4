#!/usr/bin/env python3
"""Print the disassembly of a function through GhidraMCP. usage: scripts/disasm.py <hexaddr>"""
import json, pathlib, sys, urllib.request
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from ghidra_conn import BASE
d = json.load(urllib.request.urlopen(f"{BASE}/disassemble_function?address={sys.argv[1].replace('0x','')}", timeout=120))
for i in d["instructions"]: print(i["address"], i["instruction"])
