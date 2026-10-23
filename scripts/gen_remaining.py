#!/usr/bin/env python3
"""Regenerate notes/remaining_functions.csv: every game function the ledger does not call recovered.

The file was maintained by hand, so it drifted (a class could gain symbols without the CSV noticing, and
duplicated addresses crept in). It is now generated from the symbol map and the ledger, which are the
authorities: `notes/symbols-functions-demangled.txt` and `notes/ledger.csv`.

Columns: class, address, status, name. Status is `not started`, or the ledger status for rows that exist but
are not recovered (`pending`, `partial`, `recovered-partial`).

usage: scripts/gen_remaining.py [--check]     (--check: exit 1 if the file would change)
"""
import csv, pathlib, sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / "notes/remaining_functions.csv"

src = (ROOT / "scripts/progress.py").read_text().split("def main()")[0]
ns = {"__file__": str(ROOT / "scripts/progress.py")}
exec(src, ns)
classify, owner_of = ns["classify"], ns["owner_of"]

done = {int(r["address"], 16): r["status"] for r in csv.DictReader((ROOT / "notes/ledger.csv").open())}

rows = []
for line in (ROOT / "notes/symbols-functions-demangled.txt").read_text(errors="replace").splitlines():
    addr, _, name = line.partition(" ")
    a = int(addr, 16)
    kind, _ = classify(name, a)
    if kind != "game":
        continue
    status = done.get(a, "")
    if status.startswith("recovered"):
        continue
    owner, _ = owner_of(name)
    rows.append((owner or "(free)", f"{a:016x}", status or "not started", name))

rows.sort(key=lambda r: (r[0], r[1]))
import io
buf = io.StringIO()
w = csv.writer(buf, lineterminator="\n")
w.writerow(["class", "address", "status", "name"])
w.writerows(rows)
text = buf.getvalue()

if "--check" in sys.argv:
    current = OUT.read_text() if OUT.exists() else ""
    if current != text:
        print(f"{OUT.name} is stale ({len(rows)} rows would be written); run scripts/gen_remaining.py")
        sys.exit(1)
    print(f"{OUT.name} is current ({len(rows)} rows)")
    sys.exit(0)

OUT.write_text(text)
print(f"wrote {OUT.relative_to(ROOT)}: {len(rows)} remaining game functions")
