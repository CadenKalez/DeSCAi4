#!/usr/bin/env python3
"""Rewrite the ledger's `source` column into one canonical, resolvable form.

Rows written by different sessions used several shapes: `src/a.cpp|h`, `src/a.cpp|src/a.h`,
`src/a.cpp|h + src/b.h`, `-`, a bare `h`, or free text. Tools that walk the column (the audit, the
class verifier) cannot resolve those. This script normalises each row to:

    src/a.cpp|h      the implementation and its header, when both exist
    src/a.cpp        implementation only
    src/a.h          header only (interface-only classes)

Tokens that do not resolve to a file under src/ are dropped; a row that ends up with nothing is left
untouched and reported, so no provenance is silently invented.

usage: scripts/normalize_ledger_sources.py [--dry-run]
"""
import csv, pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
LEDGER = ROOT / "notes/ledger.csv"
dry = "--dry-run" in sys.argv


def from_symbol(symbol):
    """Fallback: the file whose basename matches the owning class, when exactly one such file exists."""
    head = re.sub(r"^(non-virtual thunk to |virtual thunk to |covariant return thunk to )", "", symbol)
    head = head.split("(")[0]
    owner = head.rsplit("::", 1)[0] if "::" in head else ""
    owner = owner.split("<")[0].split("::")[-1]
    if not owner or not re.match(r"^[cn]?[A-Z]", owner):
        return None
    names = {owner.lower(), owner[1:].lower() if owner[:1] in "cn" else owner.lower()}
    hits = [p for p in ROOT.glob("src/**/*") if p.suffix in (".cpp", ".h") and p.stem.lower() in names]
    if not hits:
        return None
    cpp = next((p for p in hits if p.suffix == ".cpp"), None)
    return canon(str(cpp.relative_to(ROOT))) if cpp else canon(str(hits[0].relative_to(ROOT)))


def canon(source):
    """The canonical source string for one ledger row, or None when nothing resolves."""
    tokens = [t.strip() for t in re.split(r"[|+,;\s]+", source or "") if t.strip()]
    cpp = hdr = None
    for t in tokens:
        t = t.strip("()").rstrip(".")
        if not t.startswith("src/"):
            continue
        p = ROOT / t
        if t.endswith(".cpp") and p.exists() and cpp is None:
            cpp = t
        elif t.endswith(".h") and p.exists() and hdr is None:
            hdr = t
    if cpp:
        sibling = cpp[:-4] + ".h"
        if (ROOT / sibling).exists():
            return f"{cpp}|h"
        return cpp
    return hdr


def main():
    rows = list(csv.reader(LEDGER.open()))
    head, body = rows[0], rows[1:]
    col = head.index("source")
    changed = unresolved = 0
    symcol = head.index("symbol")
    for r in body:
        old = r[col]
        new = canon(old) or from_symbol(r[symcol])
        if new is None:
            unresolved += 1
            continue
        if new != old:
            r[col] = new
            changed += 1
    if not dry:
        with LEDGER.open("w", newline="") as f:
            w = csv.writer(f, lineterminator="\n")
            w.writerow(head); w.writerows(body)
    print(f"{'would change' if dry else 'changed'}: {changed} of {len(body)} rows; "
          f"unresolvable (left as-is): {unresolved}")


main()
