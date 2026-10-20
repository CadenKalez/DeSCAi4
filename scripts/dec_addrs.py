#!/usr/bin/env python3
"""Decompile a list of addresses through the running GhidraMCP backend and print them (with their
symbol names). usage: scripts/dec_addrs.py 0xaddr [0xaddr ...]   or   --grep REGEX (match symbol names)"""
import sys, json, re, urllib.parse, urllib.request, pathlib
ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT/"scripts")); from ghidra_conn import BASE
args = sys.argv[1:]
names = {}
for line in (ROOT/"notes/symbols-functions-demangled.txt").read_text(errors="replace").splitlines():
    a, _, n = line.partition(" ")
    try: names[int(a, 16)] = n
    except ValueError: pass
if args and args[0] == "--grep":
    rx = re.compile(args[1])
    addrs = [a for a, n in names.items() if rx.search(n)]
    addrs.sort()
else:
    addrs = [int(x, 16) for x in args]
for i in range(0, len(addrs), 20):
    chunk = addrs[i:i+20]
    q = urllib.parse.urlencode({"functions": ",".join(f"0x{a:x}" for a in chunk), "fields": "decompiled_code", "include_call_context": "false"})
    with urllib.request.urlopen(f"{BASE}/get_functions?{q}", timeout=180) as r:
        data = json.load(r).get("functions", {})
    for a in chunk:
        d = data.get(f"0x{a:x}") or data.get(hex(a)) or next((v for k, v in data.items() if int(k, 16) == a), None)
        print(f"//==== 0x{a:x} {names.get(a, '?')}")
        code = d.get("decompiled_code") if isinstance(d, dict) else d
        print(code)
