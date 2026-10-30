#!/usr/bin/env python3
"""Byte-matching check of our sources against the shipped game (v1.2.1 build 185949, x86_64).

  scripts/matching/match.sh SOURCE...                    score every function the sources define that the game has
  scripts/matching/match.sh --diff 'Class::Method(args)' SOURCE   show that function's normalized diff (original vs ours)
  --opt -Os                                              optimization level (default -Os, the closest on a 30-file sample)

The shipped game was built with Xcode 9.4 (Apple clang 9.1.0, macOS 10.13 SDK, deployment target 10.12). That compiler is not
available here; the installed clang compiles for x86_64-apple-macos10.12 as an APPROXIMATION, so a function at 100 % here is
very likely, not certainly, byte-exact with the original toolchain. Objects go to work/matching/obj (git-ignored).
"""
import argparse, difflib, os, pathlib, subprocess, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(pathlib.Path(__file__).parent))
import compare  # noqa: E402

GAME = ROOT / "game/MacOS/Sim City 4 Deluxe Editionsub"   # scripts/fetch_gamedata.sh; else the Steam install
if not GAME.exists():
    GAME = pathlib.Path.home() / "Library/Application Support/Steam/steamapps/common/SimCity 4 Deluxe/Sim City 4 Deluxe Edition.app/Contents/MacOS/Sim City 4 Deluxe Editionsub"
FLAGS = ["-std=c++20", "-w", "-Isrc", "-Iscripts/tests/stubs", "-I/opt/homebrew/include/freetype2", "-target", "x86_64-apple-macos10.12"]


def build(src, opt):
    out = ROOT / "work/matching/obj" / (src.replace("/", "__") + opt + ".o")
    out.parent.mkdir(parents=True, exist_ok=True)
    lang = ["-x", "objective-c++"] if src.endswith(".mm") else []
    r = subprocess.run(["clang++"] + FLAGS + [opt] + lang + ["-c", src, "-o", str(out)], cwd=ROOT, capture_output=True, text=True)
    if r.returncode:
        sys.exit(f"compile failed: {src}\n{r.stderr[-2000:]}")
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("sources", nargs="+")
    ap.add_argument("--opt", default="-Os")
    ap.add_argument("--diff")
    a = ap.parse_args()
    orig = compare.Original(str(GAME))
    rows = []
    for src in a.sources:
        of = compare.ObjectFile(str(build(src, a.opt)))
        for name, start, size in of.functions():
            addr = orig.by_name.get(name)
            if addr is None:
                continue
            mine, _ = of.function(start, size)
            theirs, obytes = orig.function(addr)
            if a.diff:
                if name == a.diff:
                    print(f"{name} @ {addr:#x}: score {compare.score(mine, theirs) * 100:.1f}%")
                    for line in difflib.unified_diff(theirs, mine, "original", "ours", lineterm="", n=3):
                        print(line)
                    return
                continue
            rows.append((compare.score(mine, theirs), obytes, addr, name))
    if a.diff:
        sys.exit(f"not found in the sources (or not in the game): {a.diff}")
    rows.sort(key=lambda r: (-r[0], -r[1]))
    for sc, b, addr, name in rows:
        print(f"{sc * 100:6.1f}%  {b:6} B  {addr:#x}  {name}")
    exact = [r for r in rows if r[0] == 1.0]
    print(f"\n{len(rows)} functions matched by name; {len(exact)} at 100 % ({sum(r[1] for r in exact)} of {sum(r[1] for r in rows)} bytes)"
          f"  [approximate toolchain: installed clang {a.opt}, not Apple clang 9.1]")


if __name__ == "__main__":
    main()
