#!/usr/bin/env python3
"""List every function in the address span of a class, including methods Ghidra attributes only by plain name (the
plate-comment based notes/symbols-functions-demangled.txt misses many class methods). Functions of one class are laid out
contiguously by source file, so the span between the first and last attributed method (widened by --margin bytes) holds
the rest: constructors, Init, Read/Write, DoMessage, static helpers, thunks, STL glue.
usage: scripts/class_range.py cSC4Foo [--margin 0x1000]   -> "addr size-to-next name"; feed the addresses to scripts/dec_addrs.py"""
import argparse, pathlib, re
ROOT = pathlib.Path(__file__).resolve().parent.parent
ap = argparse.ArgumentParser()
ap.add_argument("cls")
ap.add_argument("--margin", default="0x1000")
a = ap.parse_args()
rows = []
for l in (ROOT / "notes/symbols-functions-demangled.txt").read_text(errors="replace").splitlines():
    ad, _, n = l.partition(" ")
    rows.append((int(ad, 16), n))
rows.sort()
hits = [ad for ad, n in rows if re.match(r"^(?:non-virtual thunk to )?" + re.escape(a.cls) + r"::", n)]
if not hits:
    raise SystemExit("no attributed methods for " + a.cls)
lo, hi = min(hits) - int(a.margin, 16), max(hits) + int(a.margin, 16)
for i, (ad, n) in enumerate(rows):
    if lo <= ad <= hi:
        nxt = rows[i + 1][0] if i + 1 < len(rows) else ad
        print(f"{ad:x} {nxt - ad:#x} {n[:110]}")
