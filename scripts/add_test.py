#!/usr/bin/env python3
"""Register a test in scripts/run_tests.sh (inserted before the sc4_powerlinegeometry entry).
usage: scripts/add_test.py NAME 'tests/NAME_test.cpp src/a.cpp src/b.cpp'   (paths relative to the repo; scripts/tests/ and src/ are abbreviated)"""
import pathlib, sys
name, files = sys.argv[1], sys.argv[2]
files = files.replace("scripts/tests/", "$T/").replace("src/", "$S/")
p = pathlib.Path(__file__).resolve().parent / "run_tests.sh"
s = p.read_text()
if f' "{name}:' in s: sys.exit(f"{name} already registered")
k = ' "sc4_powerlinegeometry:'
i = s.index(k)
p.write_text(s[:i] + f' "{name}:{files}"\n' + s[i:])
print("registered", name)
