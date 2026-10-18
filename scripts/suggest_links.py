#!/usr/bin/env python3
"""usage: scripts/suggest_links.py <test-name> — builds the test, and for every undefined symbol greps src/ for a .cpp that defines it."""
import re, subprocess, sys, pathlib, collections
ROOT = pathlib.Path(__file__).resolve().parent.parent
name = sys.argv[1]
line = next(l for l in (ROOT/"scripts/run_tests.sh").read_text().splitlines() if l.startswith(f' "{name}:'))
files = line.split(":",1)[1].rstrip('"').split()
args = [f.replace("$T","scripts/tests").replace("$S","src") for f in files]
r = subprocess.run(["clang++","-std=c++20","-Wno-overloaded-virtual","-Isrc","-Iscripts/tests/stubs","-fsanitize=address,undefined","-fsyntax-only"]+[a for a in args if a.endswith(".cpp")][:0], capture_output=True, text=True, cwd=ROOT)
r = subprocess.run(["clang++","-std=c++20","-Wno-overloaded-virtual","-Isrc","-Iscripts/tests/stubs","-fsanitize=address,undefined"]+[a for a in args if not a.startswith("-I")]+[a for a in args if a.startswith("-I")]+["-o","/tmp/_suggest"], capture_output=True, text=True, cwd=ROOT)
syms = sorted(set(re.findall(r'^  "(.+?)", referenced from', r.stderr, re.M)))
have = {a for a in args}
cands = collections.Counter()
unmatched = []
for s in syms:
    # take the unqualified function name (before '(') and class
    base = s.split("(")[0].replace("vtable for ","").replace("typeinfo for ","")
    key = base.split("::")[-1] if "::" in base else base
    cls = base.split("::")[0] if "::" in base else None
    pat = (cls+"::"+key if cls else key)
    g = subprocess.run(["grep","-rlE",re.escape(pat)+r"\s*\(|^[A-Za-z_].*"+re.escape(pat),"src","--include=*.cpp"],capture_output=True,text=True,cwd=ROOT).stdout.split()
    g = [x for x in g if "$S/"+x[4:] not in have]
    if g: cands[g[0]] += 1
    else: unmatched.append(s)
for f,n in cands.most_common(): print(n, "$S/"+f[4:])
print("UNMATCHED:", len(unmatched))
for s in unmatched[:25]: print("  ", s)
