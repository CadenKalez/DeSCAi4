#!/usr/bin/env python3
"""Compact view of only the lifecycle functions of a class (ctor, QueryInterface, Init, Shutdown, Read, Write, GetGZCLSID,
ReadProperties, DoMessage...) plus a size table of every function, to plan a boot-subset recovery.
usage: scripts/bootview.py GROUP CLASS [--all-sizes] [regex-of-extra-function-names]"""
import re, sys, pathlib
ROOT = pathlib.Path(__file__).resolve().parent.parent
src = (ROOT/"scripts/dec.py").read_text().split("q = urllib")[0]
ns = {}
exec(src.replace("import json, re, sys, urllib.parse, urllib.request", "import re"), ns)
clean, DECL = ns["clean"], ns["DECL"]
group, cls = sys.argv[1], sys.argv[2]
extra = sys.argv[3] if len(sys.argv) > 3 else r"$^"
text = (ROOT/"work"/group/f"{cls.replace('::','_')}.txt").read_text()
blocks = re.split(r"(?m)^//==== ", text)[1:]
base = cls.split("::")[-1]
life = re.compile(r"::(" + re.escape(base) + r"|QueryInterface|Init|Shutdown|Read|Write|GetGZCLSID|ReadProperties|DoMessage|AddRef|Release)\(")
print("-- sizes --")
for b in blocks:
    head, _, body = b.partition("\n")
    if "thunk to" in head: continue
    m = re.search(r"size=(\d+)", body)
    print(f"{m.group(1) if m else '?':>6}  {head.strip()[:120]}")
print("-- lifecycle --")
for b in blocks:
    head, _, body = b.partition("\n")
    if "thunk to" in head: continue
    if not (life.search(head) or re.search(extra, head)): continue
    lines = [l for l in body.splitlines() if l.strip() and not l.startswith("//") and not DECL.match(l) and "___stack_chk" not in l and "PTR____stack_chk_guard" not in l and "WARNING:" not in l]
    code = clean("\n".join(lines))
    code = re.sub(r"\*\(undefined\d? \*\*\*\)\(this \+ (0x[0-9a-f]+)\) = &PTR_[A-Za-z_0-9]+;", r"vptr@\1;", code)
    print(f"## {head.strip()[:100]}\n{code}")
