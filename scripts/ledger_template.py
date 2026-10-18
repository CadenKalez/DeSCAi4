#!/usr/bin/env python3
"""Register every unrecovered instantiation of cRZAutoRefCount<T> / cRZSysServPtr<T,...> in notes/ledger.csv.

All instantiations share the behaviour of the templates in src/rz/cRZAutoRefCount.h and cRZSysServPtr.h, so
they are recovered by that code; names are made unique with the interface type (AutoRefSndConstructFromPointer).
usage: scripts/ledger_template.py --evidence scripts/tests/rz_autoref_test.cpp [--dry-run]
"""
import argparse, collections, csv, pathlib, re
ROOT = pathlib.Path(__file__).resolve().parent.parent
ap = argparse.ArgumentParser(); ap.add_argument("--evidence", required=True); ap.add_argument("--dry-run", action="store_true")
a = ap.parse_args()
have = {r["address"] for r in csv.DictReader((ROOT/"notes/ledger.csv").open())}
names_used = {r["unique_name"] if "unique_name" in r else r.get("name", "") for r in csv.DictReader((ROOT/"notes/ledger.csv").open())}
rx = re.compile(r"^(cRZAutoRefCount|cRZSysServPtr)<(.*?)>::(.*)$")

def first_arg(args):
    depth = 0
    for i, ch in enumerate(args):
        if ch == "<": depth += 1
        elif ch == ">": depth -= 1
        elif ch == "," and depth == 0: return args[:i]
    return args

def short(t):
    t = t.replace("*", "Ptr").replace("::", "_")
    t = re.sub(r"^c(I?[A-Z])", r"\1", t)
    return re.sub(r"[^A-Za-z0-9]", "", t)

rows = []
for line in (ROOT/"notes/symbols-functions-demangled.txt").read_text(errors="replace").splitlines():
    addr, _, name = line.partition(" ")
    if f"0x{int(addr,16):x}" in have: continue
    m = rx.match(name)
    if not m: continue
    # nested-template template args contain '>' - re-split at the real closing bracket
    kind = m.group(1)
    body = name[len(kind)+1:]
    depth, end = 1, 0
    for i, ch in enumerate(body):
        if ch == "<": depth += 1
        elif ch == ">":
            depth -= 1
            if depth == 0: end = i; break
    args, rest = body[:end], body[end+3:]
    t = short(first_arg(args))
    method = rest.split("(")[0]
    params = rest[len(method):]
    prefix = "AutoRef" if kind == "cRZAutoRefCount" else "SysServPtr"
    cls = kind
    if method == cls:
        op = "ConstructFromPointer" if "const&" not in params and params != "()" else ("ConstructCopy" if "const&" in params else "Construct")
    elif method == "~" + cls: op = "Destruct"
    elif method == "operator=": op = "AssignCopy" if "const&" in params else "AssignPointer"
    elif method == "operator->": op = "Arrow"
    elif method == "AsPPVoidParam": op = "AsOutParam"
    elif method.startswith("operator "): op = "Conversion"
    else: op = re.sub(r"[^A-Za-z0-9]", "", method)
    src = "src/rz/cRZAutoRefCount.h" if kind == "cRZAutoRefCount" else "src/rz/cRZSysServPtr.h"
    if kind == "cRZSysServPtr":
        t = short(first_arg(args)) + "_" + re.sub(r"[^0-9]", "", args.split(",", 1)[1].split(",")[0])[:6] if "," in args else t
    rows.append([int(addr, 16), name, f"{prefix}{t}{op}", src])

cnt = collections.Counter(r[2] for r in rows)
out = []
for addr, name, uname, src in rows:
    if cnt[uname] > 1 or uname in names_used: uname = f"{uname}_{addr & 0xffff:04x}"
    out.append([f"0x{addr:x}", name, uname, src, "recovered", a.evidence])
print(len(out), "functions;", len({r[2] for r in out}), "unique names")
if a.dry_run:
    for r in out[:15]: print(r[:3])
else:
    with (ROOT/"notes/ledger.csv").open("a", newline="") as f:
        csv.writer(f, lineterminator="\n").writerows(out)
