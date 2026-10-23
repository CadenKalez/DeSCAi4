#!/usr/bin/env python3
"""What the macOS port does *not* need, and what 80% of the necessary work means.

The decompilation covers the whole binary, but the shipping target is the native macOS/Metal port. Some
classes are dead weight on that target: a Direct3D 7 backend that Metal replaces, the Win32 window manager
the port re-implements, the embedded WWW/OLE browser, the in-game album, and the Lua debugger UI. Recovering
them would inflate the percentage without making the port any more complete.

This module is the single place that says which classes those are. `scripts/progress.py` imports it for the
"macOS port scope" section; `scripts/macos_scope_report.py` writes the per-class table.

To exclude another class, add it here with a reason. Nothing is excluded silently.
"""
import csv, collections, pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parent.parent

# class (or namespace-qualified class) -> why the macOS port does not need it
EXCLUDED = {
    "cGZWWWSystem": "embedded WWW/OLE browser; the macOS port has no embedded browser",
    "cSGLDX7D3DX": "Direct3D 7 / D3DX device setup; the port renders with Metal",
    "nSGLDX7::cTextureManager": "Direct3D 7 texture manager, replaced by the Metal texture layer",
    "nSGLDX7::cVertexBuffer": "Direct3D 7 vertex buffer, replaced by Metal buffers",
    "cGZWinMgrW32": "Win32 window manager; the port ships cGZWinMgrBase instead",
    "cGZWinMgrW32::cCanvasMessageTarget": "Win32 window-manager helper",
    "cGZWinMgrW32::cMouseMessageTarget": "Win32 window-manager helper",
    "cGZWinMgrW32::cKeyMessageTarget": "Win32 window-manager helper",
    "cSC4WinSnapshotAlbum": "in-game screenshot album window; not needed to play",
    "cSC4WinSnapshotTaker": "album screenshot capture; not needed to play",
    "cGZWinLuaScriptView": "Lua debugger UI (developer tool)",
    "cGZWinLuaScriptConsole": "Lua debugger UI (developer tool)",
    "cGZWinLuaScriptViewGeneral": "Lua debugger UI (developer tool)",
    "cGZWinLuaScriptViewVariables": "Lua debugger UI (developer tool)",
    "cGZWinLuaScriptViewCallStack": "Lua debugger UI (developer tool)",
    "cGZWinLuaScriptViewBreakpoints": "Lua debugger UI (developer tool)",
    "cGZWinLuaScriptBrowser": "Lua debugger UI (developer tool)",
}


def _classify():
    src = (ROOT / "scripts/progress.py").read_text().split("def main()")[0]
    ns = {"__file__": str(ROOT / "scripts/progress.py")}
    exec(src, ns)
    return ns["classify"], ns["owner_of"]


def totals():
    """(all_game, all_recovered, excluded_total, excluded_recovered) over game functions."""
    classify, owner_of = _classify()
    funcs = {}
    for line in (ROOT / "notes/symbols-functions-demangled.txt").read_text(errors="replace").splitlines():
        a, _, n = line.partition(" ")
        funcs[int(a, 16)] = n
    done = {int(r["address"], 16): r["status"] for r in csv.DictReader((ROOT / "notes/ledger.csv").open())}
    all_t = all_r = ex_t = ex_r = 0
    for a, n in funcs.items():
        kind, _ = classify(n, a)
        if kind != "game":
            continue
        owner, _ = owner_of(n)
        owner = owner or "(free)"
        rec = done.get(a, "").startswith("recovered")
        all_t += 1
        all_r += rec
        if owner in EXCLUDED:
            ex_t += 1
            ex_r += rec
    return all_t, all_r, ex_t, ex_r


def summary():
    all_t, all_r, ex_t, ex_r = totals()
    need_t = all_t - ex_t
    need_r = all_r - ex_r
    return {
        "all_total": all_t, "all_recovered": all_r,
        "excluded_total": ex_t, "excluded_recovered": ex_r,
        "necessary_total": need_t, "necessary_recovered": need_r,
        "necessary_pct": need_r / need_t if need_t else 0.0,
        "target_80": round(need_t * 0.80),
    }


def main():
    s = summary()
    print(f"game functions            {s['all_total']:,}  recovered {s['all_recovered']:,} ({s['all_recovered']/s['all_total']:.2%})")
    print(f"excluded for macOS        {s['excluded_total']:,}  in {len(EXCLUDED)} classes (of which {s['excluded_recovered']} already recovered)")
    print(f"necessary for macOS       {s['necessary_total']:,}  recovered {s['necessary_recovered']:,} ({s['necessary_pct']:.2%})")
    print(f"80% target                {s['target_80']:,}  -> {s['target_80'] - s['necessary_recovered']:+,} to go")


if __name__ == "__main__":
    main()
