#!/usr/bin/env python3
"""Rank work/unwired.log (hits<TAB>site<TAB>file:line) and map each site to ledger rows by class name."""
import csv, os, re, sys
log = sys.argv[1] if len(sys.argv) > 1 else 'work/unwired.log'
rows = []
if not os.path.exists(log):
    print(f'{log}: no unwired site was hit (or the run did not exit normally)'); sys.exit(0)
for line in open(log):
    p = line.rstrip('\n').split('\t')
    if len(p) == 3: rows.append((int(p[0]), p[1], p[2]))
rows.sort(reverse=True)
ledger = {}
with open('notes/ledger.csv', newline='') as f:
    for r in csv.reader(f):
        if len(r) > 5: ledger.setdefault(r[1].split('::')[0], []).append(r[4])
for hits, name, where in rows:
    cls = name.split('::')[0]
    sts = ledger.get(cls, [])
    summary = ','.join(sorted({s for s in sts if s}))[:60]
    print(f'{hits:>8}  {name:<50} {where}  ledger[{len(sts)}]:{summary}')
