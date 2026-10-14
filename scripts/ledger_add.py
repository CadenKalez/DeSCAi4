#!/usr/bin/env python3
"""Append recovered functions to notes/ledger.csv.

usage: scripts/ledger_add.py --source 'src/x.cpp|h' --status recovered --evidence '...' \
          0x1004c2688 0x1004c26aa:ConstructFoo ...      (addr[:unique_name])
The symbol comes from notes/symbols-functions-demangled.txt; unique_name defaults to the method name.
"""
import argparse, csv, pathlib
ROOT = pathlib.Path(__file__).resolve().parent.parent
ap = argparse.ArgumentParser()
ap.add_argument("--source", required=True); ap.add_argument("--status", default="recovered")
ap.add_argument("--evidence", required=True); ap.add_argument("items", nargs="+")
a = ap.parse_args()
sym = {}
for line in (ROOT/"notes/symbols-functions-demangled.txt").read_text(errors="replace").splitlines():
    k, _, n = line.partition(" "); sym.setdefault(int(k, 16), n)
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
        print("added", key, uname or s)
