#!/usr/bin/env python3
"""Closes the ledger rows of cSC4ViewInputControlQuery whose function is written in ViewInputControlQuery.cpp without a PARTIAL
note, and keeps the rest recovered-partial (the next batch to write)."""
import csv, pathlib, re, subprocess, sys
ROOT = pathlib.Path(__file__).resolve().parent.parent
src = (ROOT/"src/sc4/ViewInputControlQuery.cpp").read_text().splitlines()
defined = {}
stubStart = len(src)
for i, line in enumerate(src):
    m = re.match(r"^[A-Za-z0-9_:<>\*& ]*?cSC4ViewInputControlQuery::(~?\w+)\(", line)
    if m and not line.startswith("//"):
        j = i
        while j > 0 and src[j - 1].lstrip().startswith("//"): j -= 1   # the comment block directly above the definition
        window = "\n".join(src[j:i])
        defined.setdefault(m.group(1), []).append("PARTIAL" in window )
partial = []
for r in csv.DictReader((ROOT/"notes/ledger.csv").open()):
    m = re.search(r"ViewInputControlQuery::(~?\w+)\(", r["symbol"])
    if not m or r["status"] == "recovered": continue
    flags = defined.get(m.group(1))
    if not flags or any(flags): partial.append(r["address"])
print(len(partial), "still partial")
subprocess.check_call([sys.executable, str(ROOT/"scripts/ledger_upgrade.py"), "--class", "cSC4ViewInputControlQuery",
    "--source", "src/sc4/ViewInputControlQuery.cpp|h", "--evidence",
    "test scripts/tests/sc4_viewinputcontrolquery_test.cpp; checked against the disassembly", "--partial", ",".join(partial)])
