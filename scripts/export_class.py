#!/usr/bin/env python3
"""Bulk-export decompiled code for every function of the given classes from the running
GhidraMCP backend (scripts/ghidra-mcp-headless.sh, port 8089) into work/<group>/<Class>.txt.

usage: scripts/export_class.py <group> <regex-on-class-name> [--fields signature,decompiled_code,callees]
Includes compiler thunks attributed to the class. Output is derived from the game binary, so
work/ is gitignored.
"""
import argparse, json, pathlib, re, sys, urllib.parse, urllib.request, collections
ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from ghidra_conn import BASE

src = (ROOT/"scripts/progress.py").read_text().replace("\nmain()\n", "\n")
ns = {"__file__": str(ROOT/"scripts/progress.py")}
exec(compile(src, "progress", "exec"), ns)
owner_of = ns["owner_of"]

def class_functions(rx):
    pat = re.compile(rx)
    out = collections.defaultdict(list)
    for line in (ROOT/"notes/symbols-functions-demangled.txt").read_text(errors="replace").splitlines():
        addr, _, name = line.partition(" ")
        n2 = re.sub(r"^(non-virtual thunk to |virtual thunk to |covariant return thunk to )", "", name)
        owner, _ = owner_of(n2)
        if owner and pat.search(owner.split("::")[0]):
            out[owner].append((int(addr, 16), name))
    return out

def fetch(addrs, fields):
    q = urllib.parse.urlencode({"functions": ",".join(f"0x{a:x}" for a in addrs), "fields": fields,
                                "include_call_context": "false"})
    with urllib.request.urlopen(f"{BASE}/get_functions?{q}", timeout=180) as r:
        return json.load(r).get("functions", {})

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("group"); ap.add_argument("regex")
    ap.add_argument("--fields", default="signature,decompiled_code,callees")
    a = ap.parse_args()
    outdir = ROOT/"work"/a.group; outdir.mkdir(parents=True, exist_ok=True)
    classes = class_functions(a.regex)
    total = 0
    for cls, fns in sorted(classes.items()):
        fns.sort()
        chunks = []
        for i in range(0, len(fns), 15):
            batch = fns[i:i+15]
            try:
                res = fetch([x for x, _ in batch], a.fields)
            except Exception as e:  # keep going; record the failure
                for x, n in batch: chunks.append(f"// 0x{x:x} {n}\n// EXPORT ERROR: {e}\n")
                continue
            for x, n in batch:
                f = res.get(f"0x{x:x}") or {}
                code = (f.get("decompiled_code") or "").strip() or "// (no decompilation)"
                code = "\n".join(l for l in code.splitlines() if l.strip())
                callees = ", ".join(f"{c['name']}@{c['address']}" for c in f.get("callees", []))
                chunks.append(f"//==== 0x{x:x}  {n}\n// size={f.get('size')} callees: {callees}\n{code}\n")
            total += len(batch)
        safe = re.sub(r"[^A-Za-z0-9_]+", "_", cls)[:80]
        (outdir/f"{safe}.txt").write_text("\n".join(chunks))
        print(f"{cls}: {len(fns)} functions")
    print(f"exported {total} functions in {len(classes)} classes -> {outdir}")

main()
