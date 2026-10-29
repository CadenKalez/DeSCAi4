#!/usr/bin/env python3
"""Progress report: how many functions of the game are recovered (reads notes/ledger.csv).

Categories come from the demangled symbol map (notes/symbols-functions-demangled.txt), so
run scripts/dump-symbols.sh first if it is missing. Total function count (incl. unnamed
thunks/stubs) is passed with --ghidra-total (from Ghidra's get_function_count).
"""
import argparse, csv, collections, re, pathlib, sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
THUNK_RX = re.compile(r"^(non-virtual thunk to |virtual thunk to |covariant return thunk to |guard variable for |___cxx_global_var_init|__GLOBAL__|___cxx_global)")

def owner_of(name):
    """Return (owner, is_thunk). Owner is the class/namespace path before the function name,
    with any leading return type removed; '' for free C functions."""
    if THUNK_RX.match(name):
        return ("", True)
    depth = 0; head_end = len(name)
    for i, ch in enumerate(name):          # head = text before the argument list
        if ch in "<[": depth += 1
        elif ch in ">]": depth -= 1
        elif ch == "(" and depth == 0:
            if name.startswith("(anonymous namespace)", i): continue
            head_end = i; break
    head = name[:head_end]
    depth = 0; last_space = -1
    for i, ch in enumerate(head):          # a depth-0 space separates return type from name
        if ch in "<[(": depth += 1
        elif ch in ">])": depth -= 1
        elif ch == " " and depth == 0: last_space = i
    if last_space >= 0 and not head.startswith("operator"):
        head = head[last_space+1:]
    if "::" not in head: return ("", False)
    depth = 0; cut = -1
    for i in range(len(head)-1):           # last depth-0 '::' splits owner from member
        ch = head[i]
        if ch in "<[(": depth += 1
        elif ch in ">])": depth -= 1
        elif depth == 0 and head[i:i+2] == "::": cut = i
    return (head[:cut], False) if cut >= 0 else ("", False)

LIB_FREE = re.compile(r"^_?(FT_|ft_|tt_|t1_|t42_|cff_|sfnt_|ps_|pcf_|pfr_|winfnt|af_|otv_|FT)|^(_?lua|_?luaL?_)|^(_?inflate|_?deflate|_?png_|_?jpeg_|_?jinit|_?tr_)|^(_?SDL|_?SND|_?snd|_?AEMS)")
GAME_OWNER = [  # (category, regex on owner)
    ("cSC4 game logic (cSC4*, SC4*, cSTE*, cSL*)", r"^(cSC4|cISC4|SC4|nSC4|cSTE|cSL|nSCRes|cSCProperty|cSCLua|cS3D|cI?S3D|SLAccessor)"),
    ("cGZ framework",                          r"^(cGZ|cIGZ|nGZ|GZ)"),
    ("cRZ framework",                          r"^(cRZ|cIRZ|RZ)"),
    ("Graphics driver layer (DX7 shim, DrawContext, texels)", r"^(nSGLDX7|cSGLDX7|CDirect|nsTexelOp|SC4DrawContext|cGDriver|SGL|cSGL)"),
    ("Audio (Hit*/Fresh*)",                    r"^(cHit|cFresh|cIHit|cIFresh|cHTML|cHit)"),
    ("Aspyr / Mac glue",                       r"^(MacTools|Mac[A-Z]|NCT_)"),
    ("other game classes",                     r"^(\(anonymous namespace\)|[A-Za-z_][A-Za-z_0-9]*)"),
]

# The binary links its third-party code (libjpeg, FreeType, Lua, zlib/png, SDL, OpenSSL, the EA gimex image codecs,
# the SND/AEMS audio middleware, SDL's Cocoa/HID glue) in one contiguous tail that starts with libjpeg's
# jdiv_round_up at 0x1007b7351; game code (cSC4*/cGZ*/cRZ*, iTunesPlayer, IUnknown_Mac) all lies below it.
# Free functions from that address on are therefore library code even when their names carry no known prefix.
LIB_TAIL_START = 0x1007b7340

def classify(name, addr=None):
    owner, thunk = owner_of(name)
    if thunk:
        return ("compiler", "compiler thunks / static-init glue")
    if "std::__1::" in name or owner.startswith("std") or owner.startswith("__cxx") or name.startswith("operator new") or name.startswith("operator delete"):
        return ("lib", "libc++ (std::__1)")
    if not owner:
        if name.startswith("operator"): return ("game", "stream operators etc. (operator>>/<<)")
        if LIB_FREE.match(name):
            return ("lib", "C libs (FreeType, Lua, zlib/png, SDL/SND)")
        if addr is not None and addr >= LIB_TAIL_START:
            return ("lib", "C libs (FreeType, Lua, zlib/png, SDL/SND)")
        return ("game", "free C functions (unclassified)")
    for cat, rx in GAME_OWNER:
        if re.match(rx, owner): return ("game", cat)
    return ("game", "other game classes")

def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--ghidra-total", type=int, default=0)
    a = ap.parse_args()
    sym = ROOT/"notes/symbols-functions-demangled.txt"
    if not sym.exists(): sys.exit("run scripts/dump-symbols.sh first")
    funcs = {}
    for line in sym.read_text(errors="replace").splitlines():
        addr, _, name = line.partition(" ")
        funcs[int(addr,16)] = name
    done = {}
    led = ROOT/"notes/ledger.csv"
    if led.exists():
        for r in csv.DictReader(led.open()):
            done[int(r["address"],16)] = r["status"]
    totals = collections.Counter(); rec = collections.Counter()
    for addr, name in funcs.items():
        kind, cat = classify(name, addr)
        totals[(kind,cat)] += 1
        if done.get(addr,"").startswith("recovered"): rec[(kind,cat)] += 1
    nlib = sum(v for (k,_),v in totals.items() if k=="lib")
    ncomp = sum(v for (k,_),v in totals.items() if k=="compiler")
    ngame = sum(v for (k,_),v in totals.items() if k=="game")
    nrec = sum(v for (k,_),v in rec.items() if k=="game")      # game functions only (thunks/libs are not counted)
    nrec_thunks = sum(v for (k,_),v in rec.items() if k=="compiler")
    total_all = a.ghidra_total or len(funcs)
    out = []
    out.append("# Decompilation progress\n")
    out.append(f"Binary: Sim City 4 Deluxe Editionsub (x86_64). Functions in Ghidra: **{total_all:,}**; with symbols: {len(funcs):,}; unnamed/stub: {total_all-len(funcs):,}.\n")
    out.append(f"- Third-party libraries (libc++, FreeType, Lua, zlib/png, SDL/SND): **{nlib:,}** ({nlib/len(funcs):.1%}) — linked, not rewritten.")
    out.append(f"- Compiler-generated thunks / static init: **{ncomp:,}** ({ncomp/len(funcs):.1%}) — regenerated by the compiler, not hand-written.")
    out.append(f"- **Game code to recover: {ngame:,}** ({ngame/len(funcs):.1%}).")
    out.append(f"- **Recovered so far: {nrec:,}** = {nrec/ngame:.2%} of game code. **Remaining: {ngame-nrec:,}**.")
    out.append(f"- (Ledger also covers {nrec_thunks:,} compiler thunks of recovered classes; they are not counted above.)\n")
    out.append("| Layer | Functions | Recovered | Remaining |\n|---|---:|---:|---:|")
    for kind in ("game","lib","compiler"):
        for (k,cat),v in sorted(totals.items(), key=lambda x:-x[1]):
            if k!=kind: continue
            r = rec[(k,cat)]
            if kind=="game":
                out.append(f"| {cat} | {v:,} | {r:,} | {v-r:,} |")
            else:
                out.append(f"| {cat} *({'library' if kind=='lib' else 'compiler'})* | {v:,} | — | — |")
    pend = [h for h,s in done.items() if s=="pending"]
    out.append(f"\nPending (blocked on a dependency): {len(pend)} functions in the ledger.")
    # macOS port scope: what the shipping target does not need (scripts/macos_scope.py is the one list).
    try:
        import importlib.util as _iu
        _spec = _iu.spec_from_file_location("macos_scope", ROOT / "scripts/macos_scope.py")
        _scope = _iu.module_from_spec(_spec); _spec.loader.exec_module(_scope)
        _m = _scope.summary()
        out.append(f"\n**macOS port scope**: {_m['excluded_total']:,} functions in {len(_scope.EXCLUDED)} classes are not needed on this "
                   f"target (`scripts/macos_scope.py`), so the necessary set is **{_m['necessary_total']:,}**. Recovered "
                   f"**{_m['necessary_recovered']:,}** = **{_m['necessary_pct']:.2%}** of it; 80% needs **{_m['target_80']:,}** "
                   f"({_m['target_80'] - _m['necessary_recovered']:+,}).")
    except Exception as _e:   # the meter must never fail because of the scope module
        out.append(f"\n**macOS port scope** unavailable: {_e}")
    text = "\n".join(out)+"\n"
    (ROOT/"notes/progress.md").write_text(text)
    # README meter: static shields.io badge + text bar between markers (works in a private repo)
    pct = nrec/ngame*100
    filled = round(pct/5)
    bar = "█"*filled + "░"*(20-filled)
    color = "red" if pct < 25 else "orange" if pct < 50 else "yellow" if pct < 75 else "green"
    try:                                  # the shipping target counts only what the macOS port needs
        _mc = _scope.summary()
        _mline = (f"\n![macOS port](https://img.shields.io/badge/macOS_port-{_mc['necessary_pct']*100:.1f}%25-blue) "
                  f"**{_mc['necessary_pct']*100:.1f}%** of the {_mc['necessary_total']:,} functions the macOS port needs "
                  f"({_mc['necessary_recovered']:,} recovered; 80% = {_mc['target_80']:,})")
    except Exception:
        _mline = ""
    block = ("<!-- progress:start -->\n"
             f"![decompiled](https://img.shields.io/badge/decompiled-{pct:.1f}%25-{color})\n\n"
             f"`{bar}` **{pct:.1f}%** — {nrec:,} of {ngame:,} game functions recovered, {ngame-nrec:,} remaining "
             "(details: [notes/progress.md](notes/progress.md))"
             + _mline + "\n\n"
             "[Coverage map](docs/coverage-map.svg): every class of the original as a box sized by its machine code, coloured by how far it has come\n"
             "<!-- progress:end -->")
    rd = ROOT/"README.md"
    if rd.exists():
        import re as _re
        t = rd.read_text()
        if "<!-- progress:start -->" in t:
            t = _re.sub(r"<!-- progress:start -->.*?<!-- progress:end -->", lambda m: block, t, flags=_re.S)
        else:
            head, sep, rest = t.partition("\n\n")
            t = head + "\n\n" + block + "\n\n" + rest
        rd.write_text(t)
    print(text)
    try:   # the treemap is drawn from the same ledger (scripts/coverage_treemap.py)
        import subprocess
        subprocess.run([sys.executable, str(ROOT/"scripts/coverage_treemap.py")], check=True, capture_output=True)
    except Exception as exc:
        print(f"coverage_treemap skipped: {exc}", file=sys.stderr)

main()
