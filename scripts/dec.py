#!/usr/bin/env python3
"""Print cleaned decompilations of arbitrary functions from the running Ghidra backend.
usage: scripts/dec.py 0x1004be3b9 0x1004bf512 ..."""
import json, re, sys, urllib.parse, urllib.request
import sys as _s, pathlib as _p; _s.path.insert(0, str(_p.Path(__file__).resolve().parent)); from ghidra_conn import BASE
DECL = re.compile(r"^\s*(?!return\b|goto\b|break\b|continue\b|else\b|throw\b)(undefined\d?|uint|int|long|ulong|char|byte|bool|float|double|short|ushort|code \*|void \*|size_t|\w+ \*+|\w+)\s+\**[\w\[\]]+(\s*\[\d+\])?;\s*$")
def clean(t):
    for _ in range(8):
        t = re.sub(r"<[A-Za-z_][^<>]*>", "<>", t)
    t = re.sub(r"\(\*\*\(code \*\*\)\(\*\(long \*\)(\w+) \+ (0x[0-9a-f]+)\)\)", r"VCALL(\1,\2)", t)
    t = re.sub(r"\(\*\*\(code \*\*\)\(\*(\w+) \+ (0x[0-9a-f]+)\)\)", r"VCALL(\1,\2)", t)
    return t
q = urllib.parse.urlencode({"functions": ",".join(sys.argv[1:]), "fields": "signature,decompiled_code,callees", "include_call_context": "false"})
res = json.load(urllib.request.urlopen(f"{BASE}/get_functions?{q}", timeout=180)).get("functions", {})
for k, f in res.items():
    code = (f.get("decompiled_code") or "").strip()
    lines = [l for l in code.splitlines() if l.strip() and not DECL.match(l)]
    print(f"## {k} {f.get('name','')} size={f.get('size')}")
    print(clean("\n".join(lines)))
    print()
