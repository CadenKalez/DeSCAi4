#!/usr/bin/env python3
"""Rename ledger entries (and the Ghidra function) by address: scripts/ledger_rename.py 0xaddr=NewName ..."""
import csv, json, pathlib, sys, urllib.request
import sys as _s, pathlib as _p; _s.path.insert(0, str(_p.Path(__file__).resolve().parent)); from ghidra_conn import BASE
ROOT = pathlib.Path(__file__).resolve().parent.parent
ren = {int(k, 16): v for k, v in (x.split("=") for x in sys.argv[1:])}
rows = list(csv.reader((ROOT/"notes/ledger.csv").open())); hdr, body = rows[0], rows[1:]
for r in body:
    a = int(r[0], 16)
    if a in ren:
        r[2] = ren[a]
        b = json.dumps({"old_name": f"0x{a:x}", "new_name": ren[a], "strict_mode": "off"}).encode()
        try:
            ok = json.load(urllib.request.urlopen(urllib.request.Request(BASE + "/rename_function", b, {"Content-Type": "application/json"}), timeout=30)).get("status") == "success"
        except Exception as e:
            ok = False
        print(f"0x{a:x} -> {ren[a]} {'ok' if ok else 'GHIDRA RENAME FAILED'}")
w = csv.writer((ROOT/"notes/ledger.csv").open("w", newline=""), lineterminator="\n"); w.writerow(hdr); w.writerows(body)
