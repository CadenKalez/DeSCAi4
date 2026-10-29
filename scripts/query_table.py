#!/usr/bin/env python3
"""Rewrites cSC4ViewInputControlQuery::kRowFunctions in src/sc4/ViewInputControlQuery.cpp, four entries per line, with
the given control-id -> handler assignments added: query_table.py 0x1011=SetOrGetAirportOccupantAirTrafficVolume ..."""
import re, sys, pathlib
p = pathlib.Path(__file__).resolve().parent.parent/"src/sc4/ViewInputControlQuery.cpp"
s = p.read_text()
m = re.search(r"(kRowFunctions\[0x7f\] = \{\n)(.*?)(\n\};)", s, re.S)
body = re.sub(r"//[^\n]*", "", m.group(2))
tokens = [t.strip() for t in body.replace("\n", " ").split(",") if t.strip()]
assert len(tokens) == 0x7f, len(tokens)
for arg in sys.argv[1:]:
    idx, name = arg.split("=")
    tokens[int(idx, 16) - 0x1000] = "&cSC4ViewInputControlQuery::" + name
lines = []
for i in range(0, 0x7f, 4):
    chunk = tokens[i:i+4]
    lines.append("    " + ", ".join(chunk) + f",   // {0x1000+i:#x}..{0x1000+i+len(chunk)-1:#x}")
new = m.group(1) + "\n".join(lines) + m.group(3)
p.write_text(s[:m.start()] + new + s[m.end():])
