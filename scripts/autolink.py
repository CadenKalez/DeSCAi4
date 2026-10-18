#!/usr/bin/env python3
"""usage: scripts/autolink.py <test-name> [rounds] — repeatedly adds the .cpp files suggested by suggest_links.py to the test's line in run_tests.sh."""
import subprocess, sys, pathlib, re
ROOT = pathlib.Path(__file__).resolve().parent.parent
name = sys.argv[1]
for _ in range(int(sys.argv[2]) if len(sys.argv) > 2 else 6):
    out = subprocess.run(["python3","scripts/suggest_links.py",name],capture_output=True,text=True,cwd=ROOT).stdout.splitlines()
    add = [l.split()[1] for l in out if re.match(r"^\d+ \$S/",l)]
    if not add: break
    p = ROOT/"scripts/run_tests.sh"
    lines = p.read_text().split("\n")
    for i,l in enumerate(lines):
        if l.startswith(f' "{name}:'):
            lines[i] = l.rstrip('"') + " " + " ".join(add) + '"'
    p.write_text("\n".join(lines))
    print("added", add)
