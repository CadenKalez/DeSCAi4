#!/usr/bin/env python3
"""Compact view of an exported class (work/<group>/<Class>.txt): thunks dropped, local declarations, stack-guard noise and
comments removed, virtual calls shortened to VCALL(obj,0xslot). usage: scripts/showclass.py GROUP CLASS [FIRST_FN [N]]"""
import re, sys, pathlib
ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT/"scripts"))
src = (ROOT/"scripts/dec.py").read_text().split("q = urllib")[0]
ns = {"__file__": str(ROOT/"scripts/dec.py")}
exec(src.replace("import json, re, sys, urllib.parse, urllib.request", "import re"), ns)
clean, DECL = ns["clean"], ns["DECL"]
group, cls = sys.argv[1], sys.argv[2]
first = int(sys.argv[3]) if len(sys.argv) > 3 else 0
count = int(sys.argv[4]) if len(sys.argv) > 4 else 10**9
text = (ROOT/"work"/group/f"{cls}.txt").read_text()
blocks = re.split(r"(?m)^//==== ", text)[1:]
out = []
for b in blocks:
    head, _, body = b.partition("\n")
    if "thunk to" in head: continue
    lines = [l for l in body.splitlines() if l.strip() and not l.startswith("//") and not DECL.match(l)
             and "___stack_chk" not in l and "PTR____stack_chk_guard" not in l and "WARNING:" not in l]
    code = clean("\n".join(lines))
    code = re.sub(r"\*\(undefined\d? \*\*\*\)\(this \+ (0x[0-9a-f]+)\) = &PTR_[A-Za-z_0-9]+;", r"vptr@\1;", code)
    out.append(f"## {head.strip()}\n{code}")
sel = out[first:first+count]
print("\n".join(sel))
print(f"[{len(out)} non-thunk functions, shown {first}..{first+len(sel)}]", file=sys.stderr)
