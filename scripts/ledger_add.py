#!/usr/bin/env python3
"""Append recovered functions to notes/ledger.csv.

usage: scripts/ledger_add.py --source 'src/x.cpp|h' --status recovered --evidence '...' \
          0x1004c2688 0x1004c26aa:ConstructFoo ...      (addr[:unique_name])
The symbol comes from notes/symbols-functions-demangled.txt; unique_name defaults to the method name.
"""
import argparse, csv, json, pathlib, urllib.request
import sys as _s, pathlib as _p; _s.path.insert(0, str(_p.Path(__file__).resolve().parent)); from ghidra_conn import BASE
ROOT = pathlib.Path(__file__).resolve().parent.parent
ap = argparse.ArgumentParser()
ap.add_argument("--source", required=True); ap.add_argument("--status", default="recovered")
ap.add_argument("--evidence", required=True)
ap.add_argument("--no-ghidra", action="store_true", help="do not rename in the running Ghidra backend")
ap.add_argument("items", nargs="+")
a = ap.parse_args()
sym = {}
for line in (ROOT/"notes/symbols-functions-demangled.txt").read_text(errors="replace").splitlines():
    k, _, n = line.partition(" "); sym.setdefault(int(k, 16), n)
def ghidra_rename(addr, new_name):
    body = json.dumps({"old_name": f"0x{addr:x}", "new_name": new_name, "strict_mode": "off"}).encode()
    req = urllib.request.Request(BASE + "/rename_function", body, {"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            return json.load(r).get("status") == "success"
    except Exception as e:
        print("  ghidra rename failed:", e)
        return False

led = ROOT/"notes/ledger.csv"
have = {r["address"] for r in csv.DictReader(led.open())}
with led.open("a", newline="") as f:
    w = csv.writer(f, lineterminator="\n")
    for it in a.items:
        addr, _, uname = it.partition(":")
        v = int(addr, 16); key = f"0x{v:x}"
        if key in have: print("skip (already in ledger):", key); continue
        s = sym.get(v)
        if s is None: print("unknown symbol for", key); continue
        w.writerow([key, s, uname or s.split("(")[0].split("::")[-1], a.source, a.status, a.evidence])
        note = ""
        if uname and not a.no_ghidra:
            note = " [renamed]" if ghidra_rename(v, uname) else " [RENAME FAILED]"
        print("added", key, uname or s, note)
