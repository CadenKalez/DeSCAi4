#!/usr/bin/env python3
"""List the owners (classes) with the most unrecovered game functions: scripts/remaining.py [N] [regex]."""
import csv, collections, pathlib, re, sys
sys.argv, args = sys.argv[:1], sys.argv[1:]
ROOT = pathlib.Path(__file__).resolve().parent.parent
exec((ROOT/"scripts/progress.py").read_text().split("def main():")[0])
top = int(args[0]) if args else 60
pat = re.compile(args[1]) if len(args) > 1 else None
done = {int(r["address"], 16): r["status"] for r in csv.DictReader((ROOT/"notes/ledger.csv").open())}
left = collections.Counter(); bytes_ = collections.Counter()
for line in (ROOT/"notes/symbols-functions-demangled.txt").read_text(errors="replace").splitlines():
    addr, _, name = line.partition(" ")
    a = int(addr, 16)
    kind, cat = classify(name, a)
    if kind != "game" or done.get(a, "").startswith("recovered"): continue
    owner = owner_of(name)[0] or "(free)"
    if pat and not pat.search(owner): continue
    left[owner] += 1
for owner, n in left.most_common(top): print(f"{n:5d} {owner}")
print("total", sum(left.values()))
