#!/usr/bin/env python3
"""Set the status/evidence of specific ledger rows without touching other lines or line endings.
usage: scripts/ledger_set.py --addr 0x1001a2db8,0x... --status recovered --evidence '...' [--source 'src/..|h']"""
import argparse, pathlib, re
ap = argparse.ArgumentParser()
ap.add_argument("--addr", required=True)
ap.add_argument("--status", default="recovered")
ap.add_argument("--evidence", required=True)
ap.add_argument("--source")
a = ap.parse_args()
want = {x.lower() for x in a.addr.split(",") if x}
path = pathlib.Path(__file__).resolve().parent.parent / "notes/ledger.csv"
data = path.read_bytes().decode()
out, changed = [], 0
for line in data.splitlines(keepends=True):
    key = line.split(",", 1)[0].lower()
    if key in want:
        end = "\r\n" if line.endswith("\r\n") else ("\n" if line.endswith("\n") else "")
        body = line[: len(line) - len(end)]
        import csv, io
        row = next(csv.reader(io.StringIO(body)))
        row[4] = a.status
        row[5] = a.evidence
        if a.source: row[3] = a.source
        buf = io.StringIO(); csv.writer(buf, lineterminator="").writerow(row)
        line = buf.getvalue() + end
        changed += 1
    out.append(line)
path.write_bytes("".join(out).encode())
print(f"{changed} rows updated")
