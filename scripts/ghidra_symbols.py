#!/usr/bin/env python3
"""Rebuild notes/symbols-functions-demangled.txt ("addr Class::method(args)") from the GhidraMCP backend when the
nm-derived file is unavailable (e.g. in a cloud checkout). Every function keeps its imported Mach-O symbol as a label
(__ZN...): those are demangled with c++filt like scripts/dump-symbols.sh does; functions without one fall back to the
plate comment, then to the plain Ghidra name. Honours GHIDRA_URL / GHIDRA_TOKEN.
usage: scripts/ghidra_symbols.py [out=notes/symbols-functions-demangled.txt]"""
import concurrent.futures as cf, json, pathlib, subprocess, sys, urllib.parse, urllib.request
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from ghidra_conn import BASE
ROOT = pathlib.Path(__file__).resolve().parent.parent
out = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "notes/symbols-functions-demangled.txt")

def get(url):
    for _ in range(4):
        try:
            with urllib.request.urlopen(url, timeout=300) as r: return json.load(r)
        except Exception as e: err = e
    raise err

rows, off = [], 0
while True:
    d = get(f"{BASE}/find_functions?limit=2000&offset={off}")
    rows += [(f["address"], f["name"]) for f in d["functions"]]
    off += d["count"]
    if d["count"] == 0 or off >= d["total"]: break

def demangle(names):
    out = subprocess.run(["c++filt", "-_"], input="\n".join(names) + "\n", capture_output=True, text=True, check=True).stdout
    return out.splitlines()

def batch(chunk):
    q = urllib.parse.urlencode({"functions": ",".join("0x" + a for a, _ in chunk), "fields": "labels,plate_comment"})
    fn = get(f"{BASE}/get_functions?{q}").get("functions", {})
    mangled, res = [], []
    for a, n in chunk:
        info = fn.get("0x" + a) or {}
        label = next((l["name"] for l in info.get("labels") or [] if l["name"].startswith("__Z")), None)
        pc = (info.get("plate_comment") or "").strip().splitlines()
        first = pc[0].strip() if pc else ""
        fallback = first if "::" in first and "(" in first else n
        res.append([a, fallback, label])
        if label: mangled.append(label)
    dem = iter(demangle(mangled)) if mangled else iter(())
    return [(a, next(dem) if label else fb) for a, fb, label in res]

chunks = [rows[i:i + 100] for i in range(0, len(rows), 100)]
syms = []
with cf.ThreadPoolExecutor(8) as ex:
    for i, r in enumerate(ex.map(batch, chunks)):
        syms += r
        if i % 50 == 0: print(f"{len(syms)}/{len(rows)}", file=sys.stderr)
syms.sort(key=lambda t: int(t[0], 16))
out.write_text("".join(f"{a} {n}\n" for a, n in syms))
print(len(syms), "symbols ->", out)
