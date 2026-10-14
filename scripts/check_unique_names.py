#!/usr/bin/env python3
"""Fail if two ledger entries in the same class share a unique_name (names must identify one function)."""
import csv, collections, pathlib, sys
ROOT = pathlib.Path(__file__).resolve().parent.parent
seen = collections.defaultdict(list)
for r in csv.DictReader((ROOT/"notes/ledger.csv").open()):
    cls = r["symbol"].split("(")[0].rpartition("::")[0]
    seen[(cls, r["unique_name"])].append(r["address"])
bad = {k: v for k, v in seen.items() if len(v) > 1}
for (cls, name), addrs in sorted(bad.items()):
    print(f"DUPLICATE {cls}::{name}  {', '.join(addrs)}")
print(f"{len(seen)} unique names checked, {len(bad)} duplicates")
sys.exit(1 if bad else 0)
