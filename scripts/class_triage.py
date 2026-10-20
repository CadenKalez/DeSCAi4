#!/usr/bin/env python3
"""Rank the unrecovered game classes by how self-contained they look.

usage: scripts/class_triage.py scan   — fetch size + direct callees of every unrecovered game function (a few minutes;
                                        written to work/triage.json, resumable)
       scripts/class_triage.py rank [N] — print the classes ordered by functions per external dependency

A dependency is a direct callee that belongs to another class which still has unrecovered functions (or is a free game
function that is unrecovered). Indirect (virtual) calls are not visible, so the score is only a first filter."""
import collections, csv, json, pathlib, re, sys, urllib.parse, urllib.request
ROOT = pathlib.Path(__file__).resolve().parent.parent
from ghidra_conn import BASE
OUT = ROOT/"work/triage.json"
sys.argv, args = sys.argv[:1], sys.argv[1:]
exec((ROOT/"scripts/progress.py").read_text().split("def main():")[0])
done = {int(r["address"], 16): r["status"] for r in csv.DictReader((ROOT/"notes/ledger.csv").open())}

def todo():
    out = []
    for line in (ROOT/"notes/symbols-functions-demangled.txt").read_text(errors="replace").splitlines():
        a, _, n = line.partition(" ")
        a = int(a, 16)
        kind, _ = classify(n, a)
        if kind != "game" or done.get(a, "").startswith("recovered"): continue
        out.append((a, n, owner_of(re.sub(r"^(non-virtual thunk to |virtual thunk to |covariant return thunk to )", "", n))[0] or "(free)"))
    return out

def scan():
    data = json.loads(OUT.read_text()) if OUT.exists() else {}
    fns = [f for f in todo() if f"{f[0]:x}" not in data]
    print(len(fns), "to fetch")
    for i in range(0, len(fns), 40):
        batch = fns[i:i+40]
        q = urllib.parse.urlencode({"functions": ",".join(f"0x{a:x}" for a, _, _ in batch), "fields": "signature,callees", "include_call_context": "false"})
        try:
            with urllib.request.urlopen(f"{BASE}/get_functions?{q}", timeout=300) as r:
                res = json.load(r).get("functions", {})
        except Exception as e:
            print("batch failed", e); continue
        for a, n, o in batch:
            f = res.get(f"0x{a:x}") or {}
            data[f"{a:x}"] = {"owner": o, "size": f.get("size") or 0, "callees": [c["address"].lstrip("0x") or c["address"] for c in f.get("callees", [])]}
        if (i // 40) % 25 == 0:
            OUT.write_text(json.dumps(data)); print(i, "/", len(fns), flush=True)
    OUT.write_text(json.dumps(data))

def rank(top):
    data = json.loads(OUT.read_text())
    remaining = collections.Counter(v["owner"] for v in data.values())
    owner_at = {}
    for line in (ROOT/"notes/symbols-functions-demangled.txt").read_text(errors="replace").splitlines():
        a, _, n = line.partition(" ")
        owner_at[a.lstrip("0") or "0"] = owner_of(re.sub(r"^(non-virtual thunk to |virtual thunk to |covariant return thunk to )", "", n))[0] or "(free)"
    byclass = collections.defaultdict(lambda: {"n": 0, "size": 0, "deps": collections.Counter()})
    for v in data.values():
        c = byclass[v["owner"]]
        c["n"] += 1; c["size"] += v["size"]
        for addr in v["callees"]:
            o = owner_at.get(addr.lstrip("0") or "0", "(free)")
            if o != v["owner"] and o != "(free)" and remaining.get(o, 0) > 0:
                c["deps"][o] += 1
    rows = []
    for o, c in byclass.items():
        if o == "(free)" or c["n"] < 6: continue
        rows.append((c["n"] / (1 + len(c["deps"])), c["n"], c["size"], len(c["deps"]), o, ", ".join(list(c["deps"])[:4])))
    rows.sort(reverse=True)
    for r in rows[:top]: print("%5.1f score %4d fn %6d B %2d deps  %-34s %s" % r)

cmd = args[0] if args else "rank"
if cmd == "scan": scan()
else: rank(int(args[1]) if len(args) > 1 else 60)
