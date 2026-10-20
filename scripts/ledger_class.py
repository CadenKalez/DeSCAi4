#!/usr/bin/env python3
"""Add every symbol of a class to notes/ledger.csv with automatically disambiguated unique names.

usage: scripts/ledger_class.py --class cHitControlGroup --source 'src/audio/HitControlGroup.cpp|h' \
          --evidence '...' [--status recovered] [--partial 0xaddr,...] [--name 0xaddr=Name ...] [--no-ghidra]

Naming: method name when unique inside the class; otherwise method name + parameter types
(SetPosition_int_int_int); non-virtual thunks get a 'Thunk' suffix; constructors become
Construct*, destructors Destruct* (first = body, with-delete = ...AndDelete). --name overrides.
Skips addresses already in the ledger.
"""
import argparse, collections, csv, json, pathlib, re, subprocess, sys, urllib.request
import sys as _s, pathlib as _p; _s.path.insert(0, str(_p.Path(__file__).resolve().parent)); from ghidra_conn import BASE
ROOT = pathlib.Path(__file__).resolve().parent.parent
ap = argparse.ArgumentParser()
ap.add_argument("--class", dest="cls", required=True)
ap.add_argument("--source", required=True)
ap.add_argument("--evidence", required=True)
ap.add_argument("--status", default="recovered")
ap.add_argument("--partial", default="")
ap.add_argument("--name", action="append", default=[])
ap.add_argument("--no-ghidra", action="store_true")
ap.add_argument("--match", help="regex over the demangled symbol (replaces the Class:: prefix match; for free functions)")
ap.add_argument("--direct-only", action="store_true", help="skip nested classes (Class::Inner::method)")
a = ap.parse_args()
override = {int(k, 16): v for k, v in (x.split("=") for x in a.name)}
partial = {int(x, 16) for x in a.partial.split(",") if x}

syms = {}
pat = re.compile(a.match) if a.match else re.compile(r"^(?:non-virtual thunk to |virtual thunk to )?" + re.escape(a.cls) + r"::")
for line in (ROOT/"notes/symbols-functions-demangled.txt").read_text(errors="replace").splitlines():
    addr, _, name = line.partition(" ")
    m = pat.match(name)
    if m and not (a.direct_only and "::" in name[m.end():].split("(")[0]):
        syms[int(addr, 16)] = name

def parse(name):
    thunk = name.startswith(("non-virtual thunk to ", "virtual thunk to "))
    base = re.sub(r"^(non-virtual thunk to |virtual thunk to )", "", name)
    head, _, rest = base.partition("(")
    method = head.rsplit("::", 1)[-1]
    params = rest.rsplit(")", 1)[0]
    const = base.rstrip().endswith("const")
    return thunk, method, params, const

def sig(params):
    p = re.sub(r"\bunsigned int\b", "uint", params)
    p = re.sub(r"\bunsigned char\b", "uchar", p)
    p = re.sub(r"cIGZ?\w* const&", lambda m: "String" if "String" in m.group(0) else m.group(0), p)
    p = re.sub(r"[^A-Za-z0-9]+", "_", p).strip("_")
    return p or "void"

info = {a_: parse(n) for a_, n in syms.items()}
groups = collections.defaultdict(list)
for a_, (thunk, method, params, const) in info.items():
    groups[(thunk, method)].append(a_)
names = {}
for (thunk, method), addrs in groups.items():
    addrs.sort()
    cls_short = a.cls.split("<")[0]
    for i, a_ in enumerate(addrs):
        _, _, params, _ = info[a_]
        if method == cls_short:
            base = "Construct" + (f"_{sig(params)}" if params else "")
            base = base if i % 2 == 0 else base + "Wrapper"   # C1/C2 pairs: second is the wrapper
        elif method == "~" + cls_short:
            base = ["Destruct", "DestructAndDelete", "DestructBody"][min(i, 2)] if len(addrs) > 1 else "Destruct"
        elif len(addrs) > 1 and len({info[x][2] for x in addrs}) > 1:
            base = f"{method}_{sig(params)}"
        else:
            base = method
        names[a_] = base + ("Thunk" if thunk else "")
for k, v in override.items():
    names[k] = v
# any remaining duplicates: append the address tail
cnt = collections.Counter(names.values())
for a_, n in list(names.items()):
    if cnt[n] > 1:
        names[a_] = f"{n}_{a_ & 0xffff:04x}"

have = {r["address"] for r in csv.DictReader((ROOT/"notes/ledger.csv").open())}
def ghidra_rename(addr, new):
    body = json.dumps({"old_name": f"0x{addr:x}", "new_name": new, "strict_mode": "off"}).encode()
    req = urllib.request.Request(BASE + "/rename_function", body, {"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            return json.load(r).get("status") == "success"
    except Exception:
        return False
added = failed = 0
with (ROOT/"notes/ledger.csv").open("a", newline="") as f:
    w = csv.writer(f, lineterminator="\n")
    for a_ in sorted(syms):
        key = f"0x{a_:x}"
        if key in have:
            continue
        status = "recovered-partial" if a_ in partial else a.status
        w.writerow([key, syms[a_], names[a_], a.source, status, a.evidence])
        added += 1
        if not a.no_ghidra and not ghidra_rename(a_, names[a_]):
            failed += 1
print(f"{a.cls}: {added} added, {len(syms)-added} already present, {failed} ghidra renames failed")
