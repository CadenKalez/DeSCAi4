#!/usr/bin/env python3
"""Lint the TESTS array of scripts/run_tests.sh: every entry is one line ` "name:files..."`, names are unique and every
listed source file exists. Exit 1 on problems. usage: scripts/check_test_registry.py [path-to-run_tests.sh]"""
import pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parent.parent


def main():
    path = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "scripts/run_tests.sh"
    lines = path.read_text().splitlines()
    try:
        start = next(i for i, l in enumerate(lines) if l.startswith("declare -a TESTS=("))
        end = next(i for i in range(start + 1, len(lines)) if lines[i].strip() == ")")
    except StopIteration:
        print("TESTS array not found"); return 1
    problems, seen = [], {}
    for n in range(start + 1, end):
        line, no = lines[n], n + 1
        if not line.strip() or line.lstrip().startswith("#"): continue
        m = re.fullmatch(r' "([A-Za-z0-9_]+):([^"]*)"', line)
        if not m:
            problems.append(f"line {no}: not of the form  \"name:files\" (quote/space problem): {line[:80]}")
            continue
        name, body = m.groups()
        if name in seen: problems.append(f"line {no}: duplicate test name {name} (also line {seen[name]})")
        seen[name] = no
        for tok in body.split():
            if tok.startswith("-"): continue
            f = tok.replace("$T", "scripts/tests").replace("$S", "src")
            if not (ROOT / f).exists(): problems.append(f"line {no}: {name}: missing file {f}")
    for p in problems: print(p)
    print(f"{len(seen)} tests, {len(problems)} problems")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
