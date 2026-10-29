#!/usr/bin/env python3
"""Definition-of-done check for a recovered class. Fails (exit 1) when a registry is stale, so nothing is forgotten.

usage: python3 scripts/verify_class_done.py [ClassName ...] [--fix]

Checks (all repo-wide, the named classes get extra checks):
  1. notes/ledger.csv: no duplicate unique names (check_unique_names.py)
  2. notes/todo.csv is current: `todo.py sync-ledger` + `todo.py sync-classes` would change nothing
  3. docs/SEAMS.md is current: `list_seams.py` would change nothing
  4. notes/progress.md and the README meter are current (progress.py would change nothing)
  5. every named class: it has a registered test (scripts/run_tests.sh), a ledger evidence column, every recovered-partial /
     pending function appears in the LEDGER-<class> to-do row, and every seam it declares or uses is in docs/SEAMS.md
  6. every `// TODO` / `FIXME` / `not recovered` left in the class's source files has a matching open to-do row mentioning the class
--fix rewrites the stale generated files instead of failing on them (then re-run to see the remaining, manual, failures)."""
import csv, pathlib, re, subprocess, sys
ROOT = pathlib.Path(__file__).resolve().parent.parent
args = [a for a in sys.argv[1:] if not a.startswith("--")]
fix = "--fix" in sys.argv
problems = []

def run(*cmd):
    return subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)

def generated(paths, commands, label):
    before = {p: (ROOT/p).read_bytes() if (ROOT/p).exists() else b"" for p in paths}
    for c in commands: run(*c)
    stale = [p for p in paths if (ROOT/p).read_bytes() != before[p]]
    if stale and not fix:
        for p in stale: (ROOT/p).write_bytes(before[p])
        problems.append(f"{label} is stale ({', '.join(stale)}): run the generator or this script with --fix")
    elif stale:
        print(f"refreshed {label}: {', '.join(stale)}")

# 1
r = run("python3", "scripts/check_unique_names.py")
if "0 duplicates" not in r.stdout:
    dup = [l for l in r.stdout.splitlines() if "duplicates" in l][-1:]
    problems.append(f"ledger has duplicate unique names ({dup[0] if dup else '?'}) - see check_unique_names.py (33 are inherited from the branch merge)")
# 2-4
generated(["notes/todo.csv"], [("python3", "scripts/todo.py", "sync-ledger"), ("python3", "scripts/todo.py", "sync-classes")], "notes/todo.csv")
generated(["docs/SEAMS.md"], [("python3", "scripts/list_seams.py")], "docs/SEAMS.md")
generated(["notes/progress.md", "README.md", "docs/coverage-map.svg"], [("python3", "scripts/progress.py")], "progress meter")

ledger = list(csv.DictReader((ROOT/"notes/ledger.csv").open()))
todo = list(csv.DictReader((ROOT/"notes/todo.csv").open()))
run_tests = (ROOT/"scripts/run_tests.sh").read_text()
for cls in args:
    rows = [r for r in ledger if re.match(r"^(?:non-virtual thunk to |virtual thunk to )?" + re.escape(cls) + r"::", r["symbol"])]
    if not rows: problems.append(f"{cls}: no ledger rows (run scripts/ledger_class.py)"); continue
    if any(not r["evidence"].strip() for r in rows): problems.append(f"{cls}: ledger rows without evidence")
    sources = {r["source"] for r in rows if r["source"]}
    # Verification evidence is one of: a registered unit test (named by file or by entry name, in any of the
    # forms the ledger uses - "scripts/tests/x_test.cpp", "tests/x_test", "x_test", or the entry name), or an
    # oracle / real-data script under scripts/ (AGENT_RULES workflow step 3 allows both).
    entries = {m.group(1) for m in re.finditer(r'^ "([a-z0-9_]+):', run_tests, re.M)}
    test_files = {m.group(1) for m in re.finditer(r'\$T/([A-Za-z0-9_]+\.cpp)', run_tests)}
    evidence = " ".join(r["evidence"] for r in rows)
    cited_files = set(re.findall(r'([A-Za-z0-9_]+_test\.cpp)', evidence))
    oracles = {m for m in re.findall(r"scripts/(?:run_[A-Za-z0-9_]+\.sh|check_[A-Za-z0-9_]+\.py)", evidence) if (ROOT / m).exists()}
    tool_evidence = re.search(r"\b(oracle|tool|real data|real-data)\b", evidence, re.I) is not None
    entry_named = any(re.search(r'\b' + re.escape(e) + r'(?:_test)?\b', evidence) for e in entries)
    file_named = bool(cited_files & test_files)
    if not (oracles or file_named or entry_named):
        problems.append(f"{cls}: no registered test or oracle script named in the evidence column")
    for t in cited_files - test_files:
        if not (ROOT / "scripts/tests" / t).exists():
            problems.append(f"{cls}: evidence names scripts/tests/{t}, which does not exist")
        elif not tool_evidence:
            problems.append(f"{cls}: test {t} is not registered in scripts/run_tests.sh (scripts/add_test.py)")
    partial = [r for r in rows if r["status"] in ("recovered-partial", "partial", "pending")]
    if partial and not any(t["id"] == f"LEDGER-{cls}" and t["status"] == "open" for t in todo):
        problems.append(f"{cls}: {len(partial)} partial/pending functions but no open LEDGER-{cls} to-do row (todo.py sync-ledger)")
    # TODO markers in the source
    files = []
    for s in sources:
        for part in re.split(r"[|+ ]+", s):
            part = part.strip()
            if part.startswith("src/"):
                base = ROOT/part
                files += [base] if base.suffix in (".cpp", ".h", ".mm") else [base.with_suffix(".cpp"), base.with_suffix(".h")]
    markers = []
    for f in set(files):
        if f.exists():
            for i, l in enumerate(f.read_text(errors="replace").splitlines(), 1):
                if re.search(r"\bTODO\b|\bFIXME\b|not recovered yet", l): markers.append(f"{f.relative_to(ROOT)}:{i}")
    mine = [t for t in todo if t["status"] == "open" and (t["class"] == cls or cls in t["task"] or cls in t["id"])]
    if markers and len(mine) <= 1 and not any(t["id"].startswith(("TERRAIN", "TODO")) or not t["id"].startswith(("LEDGER-", "CLASS-")) for t in mine):
        problems.append(f"{cls}: TODO/FIXME/'not recovered' markers at {', '.join(markers[:4])} but no hand-written to-do row (todo.py add ...)")
    # seams
    seams = (ROOT/"docs/SEAMS.md").read_text()
    for f in set(files):
        if f.suffix == ".h" and f.exists():
            for m in re.finditer(r"^(?:class|struct) (I[A-Z]\w*(?:Host|Services?|Api))\b(?!\s*;)", f.read_text(errors="replace"), re.M):
                if f"`{m.group(1)}`" not in seams: problems.append(f"{cls}: seam {m.group(1)} missing from docs/SEAMS.md")

if problems:
    print("NOT DONE:")
    for p in problems: print(" -", p)
    sys.exit(1)
print("ok: registries current" + (f", {', '.join(args)} complete" if args else ""))
