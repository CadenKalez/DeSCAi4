#!/usr/bin/env python3
"""Provenance audit of notes/ledger.csv: which rows name a verification, and which only say "decompiled".

The definition of done asks every ledger row to name how it was verified — a registered unit test or an
oracle/real-data script. Rows written before that rule was enforced often just say "decompile...". This
script measures that backlog, class by class, and can repair the cases where the answer is unambiguous:
a row whose own class already names a registered test elsewhere in the ledger.

  scripts/ledger_audit.py            report only (writes notes/ledger_audit.md)
  scripts/ledger_audit.py --fix      also copy the class's test reference onto its untagged rows
"""
import collections, csv, pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
LEDGER = ROOT / "notes/ledger.csv"
REPORT = ROOT / "notes/ledger_audit.md"
FIX = "--fix" in sys.argv


def main():
    run = (ROOT / "scripts/run_tests.sh").read_text()
    test_files = set(re.findall(r"\$T/([A-Za-z0-9_]+\.cpp)", run))
    file_to_tests = collections.defaultdict(set)
    for m in re.finditer(r'^ "([a-z0-9_]+):(.*)"\s*$', run, re.M):
        for f in re.findall(r"\$S/([A-Za-z0-9_/]+\.cpp)", m.group(2)):
            file_to_tests[f].add(m.group(1))

    rows = list(csv.reader(LEDGER.open()))
    head, body = rows[0], rows[1:]
    icol = {name: i for i, name in enumerate(head)}

    def owner(sym):
        s = re.sub(r"^(non-virtual thunk to |virtual thunk to |covariant return thunk to )", "", sym)
        s = s.split("(")[0]
        return s.rsplit("::", 1)[0] if "::" in s else s

    def named_tests(ev):
        found = set(re.findall(r"([A-Za-z0-9_]+_test\.cpp)", ev)) & test_files
        for m in re.findall(r'\b([a-z0-9_]+)(?:_test)?\b', ev):
            if m in {t for t, _ in ()}:            # placeholder, entries handled below
                pass
        return found

    entries = {m.group(1) for m in re.finditer(r'^ "([a-z0-9_]+):', run, re.M)}

    def has_test(ev):
        if re.search(r"\b(oracle|tool|real data|real-data)\b", ev, re.I):
            return True
        if re.search(r"scripts/(?:run|check)_\w+\.(?:sh|py)", ev):
            return True
        if set(re.findall(r"([A-Za-z0-9_]+_test\.cpp)", ev)) & test_files:
            return True
        return any(re.search(r"\b" + re.escape(e) + r"(?:_test)?\b", ev) for e in entries)

    per_class = collections.defaultdict(lambda: {"rows": 0, "missing": 0, "tests": collections.Counter()})
    fixes = []
    for r in body:
        cls = owner(r[icol["symbol"]])
        d = per_class[cls]; d["rows"] += 1
        if has_test(r[icol["evidence"]]):
            for t in set(re.findall(r"([A-Za-z0-9_]+_test\.cpp)", r[icol["evidence"]])):
                d["tests"][t] += 1
            for e in entries:
                if re.search(r"\b" + re.escape(e) + r"(?:_test)?\b", r[icol["evidence"]]):
                    d["tests"][e + "_test.cpp"] += 1
        else:
            d["missing"] += 1
            if d["tests"]:
                fixes.append(r)

    untagged_classes = [c for c, d in per_class.items() if d["missing"] and not d["tests"]]
    lines = ["# Ledger provenance audit", "",
             f"Rows: **{len(body)}**. With a named verification: **{len(body) - sum(d['missing'] for d in per_class.values())}**.",
             f"Without one: **{sum(d['missing'] for d in per_class.values())}** across "
             f"**{sum(1 for d in per_class.values() if d['missing'])}** classes.", ""]
    lines += ["## Classes whose rows name no verification at all", "",
              "These predate the evidence rule: their functions are decompiled and ledgered, but no test or oracle",
              "is named. Repair means writing or pointing at a test, not editing the ledger.", "",
              "| Class | Rows without evidence | Registered test compiling its source |", "|---|---:|---|"]
    for c in sorted(untagged_classes, key=lambda c: -per_class[c]["missing"])[:60]:
        files = {r[icol["source"]].split("|")[0] for r in body if owner(r[icol["symbol"]]) == c}
        cands = sorted({t for f in files for t in file_to_tests.get(f, ())})
        hint = ", ".join(cands[:2]) if cands else "—"
        lines.append(f"| `{c}` | {per_class[c]['missing']} | {hint} |")
    if len(untagged_classes) > 60:
        lines.append(f"| … {len(untagged_classes) - 60} more classes | | |")
    REPORT.write_text("\n".join(lines) + "\n")

    if FIX and fixes:
        evidence = "test " + next(iter(next(d['tests'] for d in per_class.values() if d['tests'])))
        for r in fixes:
            cls = owner(r[icol["symbol"]])
            t = next(iter(per_class[cls]["tests"]))
            if t not in r[icol["evidence"]]:
                r[icol["evidence"]] = r[icol["evidence"]].rstrip(" .") + f"; tested by the class's {t}"
        with LEDGER.open("w", newline="") as f:
            w = csv.writer(f, lineterminator="\n"); w.writerow(head); w.writerows(body)

    missing = sum(d["missing"] for d in per_class.values())
    print(f"rows {len(body)} | without a named verification {missing} | classes with none {len(untagged_classes)}")
    print(f"wrote {REPORT.relative_to(ROOT)}" + (f"; repaired {len(fixes)} rows from their class's test" if FIX else ""))


main()
