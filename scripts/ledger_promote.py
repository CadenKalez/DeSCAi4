#!/usr/bin/env python3
"""Promote ledger rows to 'recovered' once the code behind them exists.

usage: scripts/ledger_promote.py --source 'src/gz/GZCOMCreators5.cpp' --evidence '...' 0xaddr [0xaddr ...]
Rewrites status/source/evidence of the listed addresses (they must already be in notes/ledger.csv)."""
import argparse, csv, io, pathlib, sys
ROOT = pathlib.Path(__file__).resolve().parent.parent
ap = argparse.ArgumentParser()
ap.add_argument("--source", required=True)
ap.add_argument("--evidence", required=True)
ap.add_argument("--status", default="recovered")
ap.add_argument("addresses", nargs="+")
a = ap.parse_args()
path = ROOT/"notes/ledger.csv"
want = {int(x, 16) for x in a.addresses}
hit = set()
out = []
with path.open(newline="") as f:
    lines = f.readlines()          # keeps each line's own ending: the file mixes CRLF and LF
for line in lines:
    body = line.rstrip("\r\n")
    ending = line[len(body):]
    head = body.split(",", 1)[0]
    if head.startswith("0x") and int(head, 16) in want:
        row = next(csv.reader([body]))
        row[3], row[4], row[5] = a.source, a.status, a.evidence
        buf = io.StringIO()
        csv.writer(buf, lineterminator="").writerow(row)
        line = buf.getvalue() + ending
        hit.add(int(head, 16))
    out.append(line)
with path.open("w", newline="") as f:
    f.writelines(out)
missing = want - hit
print(f"promoted {len(hit)}, missing {len(missing)}", *(f"0x{m:x}" for m in sorted(missing)))
