#!/usr/bin/env python3
"""Compact view of an exported class (work/<group>/<Class>.txt): drops thunks, local declarations,
Ghidra warnings and comment banners, folds vtable calls into VCALL(obj, 0xoff), collapses libc++
template noise to T. usage: scripts/readclass.py <group> <Class> [name-regex]"""
import re, sys, pathlib
ROOT = pathlib.Path(__file__).resolve().parent.parent
grp, cls = sys.argv[1], sys.argv[2]
flt = re.compile(sys.argv[3]) if len(sys.argv) > 3 else None
path = ROOT / "work" / grp / (re.sub(r"[^A-Za-z0-9_]+", "_", cls) + ".txt")
DECL = re.compile(r"^\s*(?!return\b|goto\b|break\b|continue\b|else\b|throw\b|case\b|do\b|if\b|while\b|for\b|switch\b)"
                  r"(undefined\d?|uint|int|long|ulong|char|byte|bool|float|double|short|ushort|code \*|void \*|size_t|"
                  r"\w+ \*+|\w+)\s+\**[\w\[\]]+(\s*\[\d+\])?;\s*$")
def clean(t):
    for _ in range(8):
        t = re.sub(r"<[A-Za-z_][^<>]*>", "<>", t)
    t = re.sub(r"\(\*\*\(code \*\*\)\(\*\(long \*\)(\w+) \+ (0x[0-9a-f]+)\)\)", r"VCALL(\1,\2)", t)
    t = re.sub(r"\(\*\*\(code \*\*\)\(\*(\w+) \+ (0x[0-9a-f]+)\)\)", r"VCALL(\1,\2)", t)
    t = re.sub(r"\(\*\*\(code \*\*\)\(\*\*\(long \*\*\)\((\w+ \+ 0x[0-9a-f]+)\) \+ (0x[0-9a-f]+)\)\)", r"VCALL(*(\1),\2)", t)
    t = re.sub(r"\*\(code \*\*\)\(\*\*\(long \*\*\)\((this \+ \w+)\) \+ (0x[0-9a-f]+)\)", r"VCALL(*(\1),\2)", t)
    return t
text = path.read_text()
for part in re.split(r"(?m)^(?=//==== )", text):
    if not part.strip():
        continue
    head = part.split("\n", 1)[0]
    if "thunk to" in head:
        continue
    if flt and not flt.search(head):
        continue
    lines = []
    for l in part.splitlines()[1:]:
        s = l.strip()
        if not s or s.startswith("//") or DECL.match(l):
            continue
        lines.append(clean(l.rstrip()))
    print(head[4:].strip())
    print("\n".join(lines))
    print()
