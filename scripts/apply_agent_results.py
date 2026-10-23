#!/usr/bin/env python3
"""Fold the files parallel porting agents leave behind into the shared bookkeeping, then archive them.

  notes/ledger_pending/<name>.txt   SOURCE <src|h> / EVIDENCE <text> / "0xADDR UniqueName" lines /
                                    optional "PARTIAL:" and "SKIPPED:" sections ("0xADDR reason")
        -> rows in notes/ledger.csv (existing partial rows are upgraded in place; new rows get the symbol from
           notes/symbols-functions-demangled.txt; SKIPPED rows become status "skipped" with the reason) and the
           unique name is applied to the running Ghidra backend (skip with --no-ghidra)
  scripts/tests/registry/<name>.txt one `"name:$T/..."` line -> inserted at the end of the TESTS array of
        scripts/run_tests.sh (never while that script is running)

Processed pending files move to notes/ledger_applied/. usage: scripts/apply_agent_results.py [--no-ghidra]"""
import csv, json, pathlib, re, shutil, sys, urllib.request
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from ghidra_conn import BASE
ROOT = pathlib.Path(__file__).resolve().parent.parent
no_ghidra = "--no-ghidra" in sys.argv
LEDGER = ROOT / "notes/ledger.csv"

rows = list(csv.reader(LEDGER.open()))
header, body = rows[0], rows[1:]
index = {r[0]: i for i, r in enumerate(body)}
names = {r[2] for r in body}
sym = {}
for line in (ROOT / "notes/symbols-functions-demangled.txt").read_text(errors="replace").splitlines():
    a, _, n = line.partition(" ")
    sym.setdefault("0x%x" % int(a, 16), n)

def rename(addr, new):
    if no_ghidra: return True
    req = urllib.request.Request(BASE + "/rename_function",
                                 json.dumps({"old_name": addr, "new_name": new, "strict_mode": "off"}).encode(),
                                 {"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=60) as r: return json.load(r).get("status") == "success"
    except Exception: return False

def unique(name):
    base, k = name, 2
    while name in names:
        name = f"{base}_{k}"; k += 1
    names.add(name); return name

added = upgraded = skipped = 0
pending = ROOT / "notes/ledger_pending"
for f in sorted(pending.glob("*.txt")) if pending.exists() else []:
    source = evidence = ""; section = "ok"
    for raw in f.read_text().splitlines():
        line = raw.strip()
        if not line: continue
        if line.startswith("SOURCE "): source = line[7:]; continue
        if line.startswith("EVIDENCE "): evidence = line[9:]; continue
        if line.upper().startswith("PARTIAL"): section = "partial"; continue
        if line.upper().startswith("SKIPPED"): section = "skipped"; continue
        m = re.match(r"^(0x[0-9a-fA-F]+)\s+(.*)$", line)
        if not m: continue
        addr = "0x%x" % int(m.group(1), 16); rest = m.group(2).strip()
        status = {"ok": "recovered", "partial": "recovered-partial", "skipped": "skipped"}[section]
        if addr in index:
            r = body[index[addr]]
            # "ok" upgrades anything not already recovered; "partial" corrects a skipped or over-claimed row
            # (an agent reporting an honest recovered-partial must be able to override a stale "recovered").
            if section == "ok" and r[4] != "recovered":
                r[3], r[4] = source or r[3], "recovered"
                if evidence: r[5] = evidence
                # a row upgraded out of "skipped" still carries the Skipped_xxxxxxxx placeholder as its
                # unique_name; take the agent's real name so the ledger reads properly.
                name_ok = unique(rest.split()[0].rstrip(":.,;")) if rest else ""
                if name_ok and r[2].startswith("Skipped_"):
                    r[2] = name_ok
                    rename(addr, name_ok)
                upgraded += 1
            elif section == "partial" and r[4] != "recovered-partial":
                if rest and r[2].startswith("Skipped_"):
                    r[2] = unique(rest.split()[0].rstrip(":.,;"))
                    rename(addr, r[2])
                r[3], r[4] = source or r[3], "recovered-partial"
                if evidence: r[5] = evidence + " [partial: " + rest[:120] + "]"
                upgraded += 1
            continue
        # a pending line is "0xADDR Name" but agents sometimes write "Name:" - strip punctuation
        uname = unique(rest.split()[0].rstrip(":.,;")) if section != "skipped" else unique("Skipped_" + addr[2:])
        body.append([addr, sym.get(addr, uname), uname, source if section != "skipped" else "", status,
                     evidence if section != "skipped" else "not needed on macOS: " + rest])
        index[addr] = len(body) - 1
        if section != "skipped" and not rename(addr, uname): print("  ghidra rename failed:", addr, uname)
        added += 1 if section != "skipped" else 0
        skipped += 1 if section == "skipped" else 0
    done = ROOT / "notes/ledger_applied"; done.mkdir(exist_ok=True)
    shutil.move(str(f), str(done / f.name))
with LEDGER.open("w", newline="") as fh:
    w = csv.writer(fh, lineterminator="\n"); w.writerow(header); w.writerows(body)
print(f"ledger: {added} added, {upgraded} upgraded, {skipped} skipped rows")

reg = ROOT / "scripts/tests/registry"
tests = (ROOT / "scripts/run_tests.sh").read_text()
new = 0
for f in sorted(reg.glob("*.txt")) if reg.exists() else []:
    entry = f.read_text().strip()
    name = entry.strip('"').split(":", 1)[0]
    marker = f' "{name}:'
    if marker not in tests:
        tests = tests.replace("\n)\nfail=0", f"\n {entry}\n)\nfail=0", 1); new += 1
    else:
        # replace the existing entry line when its source list changed
        pat = re.compile(r'^ "?' + re.escape(name) + r':.*$', re.M)
        m = pat.search(tests)
        if m and m.group(0).strip().strip('"') != entry.strip().strip('"'):
            tests = tests[:m.start()] + " " + entry + tests[m.end():]
            updated = globals().get("updated", 0) + 1
            globals()["updated"] = updated
    done = ROOT / "scripts/tests/registry_applied"; done.mkdir(exist_ok=True)
    shutil.move(str(f), str(done / f.name))
(ROOT / "scripts/run_tests.sh").write_text(tests)
print(f"run_tests.sh: {new} test entries added, {globals().get('updated', 0)} replaced")
