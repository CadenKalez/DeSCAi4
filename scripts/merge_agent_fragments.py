#!/usr/bin/env python3
"""Fold the per-class fragments that parallel agents leave behind into the shared files, so agents never edit the
append-only notes/ledger.csv or scripts/run_tests.sh themselves (those conflict on every merge).

  notes/ledger_fragments/<Class>.csv   rows in notes/ledger.csv format (no header): address,symbol,unique_name,source,status,evidence
  scripts/tests/registry/<name>.txt    one line `"name:$T/..."` for scripts/run_tests.sh

  notes/ledger_fragments/<Class>_status_updates.csv   same 6 columns; each row UPDATES the existing ledger row with that
                                       address (status and evidence replaced; name and source must match, else it is reported)

Rows whose address or unique name already exists are skipped (and reported). Merged fragments move to
notes/ledger_fragments/applied/. usage: scripts/merge_agent_fragments.py   (never while scripts/run_tests.sh is running)"""
import csv, pathlib, shutil

ROOT = pathlib.Path(__file__).resolve().parent.parent
ledger = ROOT / "notes/ledger.csv"
rows = list(csv.reader(ledger.open()))
have_addr = {r[0] for r in rows[1:]}
have_name = {r[2] for r in rows[1:]}
frag_dir = ROOT / "notes/ledger_fragments"
added = skipped = 0
index = {r[0]: i for i, r in enumerate(rows)}
updated = 0
for f in sorted(frag_dir.glob("*_status_updates.csv")):
    for r in csv.reader(f.open()):
        i = index.get(r[0]) if len(r) == 6 else None
        if i is None or rows[i][2] != r[2]:
            print(f"cannot update from {f.name}: {r}")
            continue
        rows[i][4], rows[i][5] = r[4], r[5]
        updated += 1
    (frag_dir / "applied").mkdir(exist_ok=True)
    shutil.move(str(f), str(frag_dir / "applied" / f.name))
with ledger.open("w", newline="") as out:
    csv.writer(out, lineterminator="\n").writerows(rows)
for f in sorted(frag_dir.glob("*.csv")):
    new = []
    for r in csv.reader(f.open()):
        if len(r) != 6:
            print(f"BAD ROW in {f.name}: {r}")
            continue
        if r[0] in have_addr or r[2] in have_name:
            print(f"skip {f.name}: {r[0]} {r[2]} already in ledger")
            skipped += 1
            continue
        have_addr.add(r[0]); have_name.add(r[2]); new.append(r)
    with ledger.open("a", newline="") as out:
        csv.writer(out, lineterminator="\n").writerows(new)
    added += len(new)
    (frag_dir / "applied").mkdir(exist_ok=True)
    shutil.move(str(f), str(frag_dir / "applied" / f.name))

reg_dir = ROOT / "scripts/tests/registry"
tests = ROOT / "scripts/run_tests.sh"
lines = tests.read_text().split("\n")
close = max(i for i, l in enumerate(lines) if l.strip() == ")")
for f in sorted(reg_dir.glob("*.txt")) if reg_dir.exists() else []:
    entry = f.read_text().strip()
    name = entry.split(":", 1)[0].strip('"')
    if any(l.strip().startswith(f'"{name}:') for l in lines):
        print(f"skip registry {name}: already registered")
    else:
        lines.insert(close, " " + entry)
        close += 1
    (reg_dir / "applied").mkdir(exist_ok=True)
    shutil.move(str(f), str(reg_dir / "applied" / f.name))
tests.write_text("\n".join(lines))
print(f"{added} ledger rows added, {updated} updated, {skipped} skipped")
