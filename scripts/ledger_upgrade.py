#!/usr/bin/env python3
"""Upgrade existing ledger rows of a class (status documented/pending) to recovered once the code exists.

usage: scripts/ledger_upgrade.py --class cHitResourceFactory --source 'src/audio/X.cpp|h' --evidence '...'
           [--status recovered] [--partial 0xaddr,...] [--keep-name]
Rows already recovered* are left alone. Names in the 3rd column are kept."""
import argparse, csv, pathlib, re
ROOT = pathlib.Path(__file__).resolve().parent.parent
ap = argparse.ArgumentParser()
ap.add_argument("--class", dest="cls", required=True)
ap.add_argument("--source", required=True)
ap.add_argument("--evidence", required=True)
ap.add_argument("--status", default="recovered")
ap.add_argument("--partial", default="")
a = ap.parse_args()
partial = {int(x, 16) for x in a.partial.split(",") if x}
path = ROOT / "notes/ledger.csv"
rows = list(csv.reader(path.open()))
pat = re.compile(r"^(?:non-virtual thunk to |virtual thunk to )?" + re.escape(a.cls) + r"::")
n = 0
for r in rows[1:]:
    if pat.match(r[1]) and r[4] != "recovered":   # recovered-partial rows must be closable too
        r[3] = a.source
        r[4] = "recovered-partial" if int(r[0], 16) in partial else a.status
        r[5] = a.evidence
        n += 1
with path.open("w", newline="") as f:
    csv.writer(f).writerows(rows)
print(f"{a.cls}: {n} rows upgraded")
