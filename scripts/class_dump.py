#!/usr/bin/env python3
"""Dump the decompilation of one class straight from the GhidraMCP backend, without notes/symbols-functions-demangled.txt.

A class's code is laid out contiguously by source file: the constructors are named after the class, and the file ends with
the static initialiser `__GLOBAL__sub_I_<File>.cpp`. This script finds the constructors (functions named exactly like the
class), takes everything from the first one up to the first static initialiser after it, prints the address list and writes the
decompiled code to work/<group>/<Class>.txt (work/ is gitignored: it is derived from the game binary).

usage: scripts/class_dump.py cSC4Foo [--group sc4] [--list] [--lo 0xADDR --hi 0xADDR]
  --list      only print "addr name" rows (no decompile)
  --lo/--hi   override the span (classes whose methods are spread over several files, or that have no constructor)
Honours GHIDRA_URL / GHIDRA_TOKEN (see scripts/ghidra_conn.py). The function list is cached in $TMPDIR/sc4_funcs.json."""
import argparse, json, os, pathlib, sys, tempfile, urllib.parse, urllib.request
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from ghidra_conn import BASE

ROOT = pathlib.Path(__file__).resolve().parent.parent
ap = argparse.ArgumentParser()
ap.add_argument("cls")
ap.add_argument("--group", default="sc4")
ap.add_argument("--list", action="store_true")
ap.add_argument("--lo")
ap.add_argument("--hi")
a = ap.parse_args()


def get(path):
    for _ in range(4):
        try:
            with urllib.request.urlopen(BASE + path, timeout=300) as r:
                return json.load(r)
        except Exception as e:
            err = e
    raise err


cache = pathlib.Path(tempfile.gettempdir()) / "sc4_funcs.json"
if cache.exists():
    rows = [tuple(r) for r in json.loads(cache.read_text())]
else:
    rows, off = [], 0
    while True:
        d = get(f"/find_functions?limit=2000&offset={off}")
        rows += [(int(f["address"], 16), f["name"]) for f in d["functions"]]
        off += d["count"]
        if d["count"] == 0 or off >= d["total"]:
            break
    rows.sort()
    cache.write_text(json.dumps(rows))

if a.lo and a.hi:
    lo, hi = int(a.lo, 16), int(a.hi, 16)
else:
    ctors = [ad for ad, n in rows if n == a.cls]
    if not ctors:
        raise SystemExit(f"no function named {a.cls}; pass --lo/--hi")
    lo = min(ctors)
    hi = next((ad for ad, n in rows if ad > lo and n.startswith("__GLOBAL__sub_I_")), None)
    if hi is None:
        raise SystemExit("no static initialiser after the constructors; pass --hi")

sel = [(ad, n) for ad, n in rows if lo <= ad <= hi]
for ad, n in sel:
    print(f"{ad:x} {n[:100]}")
print(f"# {len(sel)} functions in 0x{lo:x}..0x{hi:x}", file=sys.stderr)
if a.list:
    raise SystemExit

out = []
for i in range(0, len(sel), 10):
    ch = sel[i:i + 10]
    q = urllib.parse.urlencode({"functions": ",".join(f"0x{ad:x}" for ad, _ in ch), "fields": "decompiled_code",
                                "include_call_context": "false"})
    r = get(f"/get_functions?{q}").get("functions", {})
    for ad, n in ch:
        d = r.get(f"0x{ad:x}") or r.get(f"{ad:x}") or {}
        out.append(f"//==== 0x{ad:x} {n}\n{d.get('decompiled_code') if isinstance(d, dict) else d}\n")
dest = ROOT / "work" / a.group / f"{a.cls}.txt"
dest.parent.mkdir(parents=True, exist_ok=True)
dest.write_text("\n".join(out))
print(f"wrote {dest}", file=sys.stderr)
