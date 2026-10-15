#!/usr/bin/env python3
"""Print comma-separated addresses (0x...) of every symbol whose demangled name matches a regex (thunks included).
usage: scripts/sym_addrs.py 'cGZWin::(PlotComposite|PlotPresent)\\('   -> for ledger_class --partial / ledger_set --addr"""
import pathlib, re, sys
root = pathlib.Path(__file__).resolve().parent.parent
rx = re.compile(sys.argv[1])
out = []
for line in (root/"notes/symbols-functions-demangled.txt").read_text(errors="replace").splitlines():
    addr, _, name = line.partition(" ")
    if rx.search(name): out.append("0x" + addr.lstrip("0"))
print(",".join(out))
