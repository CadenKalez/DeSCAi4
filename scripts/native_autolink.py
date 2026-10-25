#!/usr/bin/env python3
"""usage: scripts/native_autolink.py <link-list> <main.mm|cpp> [rounds]

Compiles every source of <link-list> plus <main> (objects cached in build/native_obj), links, and for each undefined symbol greps
src/ for the .cpp/.mm that defines it, appending those to the list. Repeats until the link is clean (or nothing matches).
Never adds src/experimental_host, src/tools, src/app/main.mm, or the reference-only render sources."""
import re, subprocess, sys, pathlib, collections, os
ROOT = pathlib.Path(__file__).resolve().parent.parent
os.chdir(ROOT)
listf, main = pathlib.Path(sys.argv[1]), sys.argv[2]
rounds = int(sys.argv[3]) if len(sys.argv) > 3 else 12
DEBUG = bool(os.environ.get("NATIVE_DEBUG"))   # -O0, frame pointers, ASan: objects in build/native_obj_debug
OBJ = ROOT / ("build/native_obj_debug" if DEBUG else "build/native_obj"); OBJ.mkdir(parents=True, exist_ok=True)
BAD = ("src/app/game_main.mm", "experimental_host", "src/tools", "src/app/main.mm", "MetalGL", "GLApi", "GLState", "D3DDevice7", "DDSurface7", "D3DVertexBuffer7",
       "DirectDraw7", "D3D7Stub", "DX7", "MacOpenGL", "FastATI", "/pending/", "src/sc4/AutomataState.cpp", "src/app/GameAudio",
       "src/app/audio_demo", "gzcom_creators/cGZLuaScriptServer.cpp", "GZCOMCreatorsLotConfig.cpp")
FLAGS = ["-std=c++20", "-w", "-fobjc-arc", "-Isrc", "-Iscripts/tests/stubs", "-I/opt/homebrew/include/freetype2", "-arch", "arm64"] + (["-O0", "-g", "-fno-omit-frame-pointer", "-fsanitize=address"] if DEBUG else ["-O1", "-g"])
FW = ["-L/opt/homebrew/lib", "-lfreetype", "-framework", "Foundation", "-framework", "Metal", "-framework", "QuartzCore", "-framework", "Cocoa", "-framework", "ImageIO",
      "-framework", "UniformTypeIdentifiers", "-framework", "CoreGraphics", "-framework", "AVFoundation"]
srcs = [p for p in ROOT.joinpath("src").rglob("*") if p.suffix in (".cpp", ".mm") and not any(b in str(p) for b in BAD)]
text = {str(p.relative_to(ROOT)): p.read_text(errors="ignore") for p in srcs}

def entries():
    return [l.strip() for l in listf.read_text().splitlines() if l.strip() and not l.startswith("#")]

def obj(s):
    return OBJ / (s.replace("/", "_") + ".o")

def build():
    todo = []
    for s in entries() + [main]:
        o = obj(s)
        newest_header = max((p.stat().st_mtime for p in ROOT.joinpath("src").rglob("*.h")), default=0)
        if not o.exists() or o.stat().st_mtime < pathlib.Path(s).stat().st_mtime or o.stat().st_mtime < newest_header:
            todo.append(s)
    procs = []
    errs = []
    def run(s):
        r = subprocess.run(["clang++"] + FLAGS + ["-c", s, "-o", str(obj(s))], capture_output=True, text=True)
        return s, r
    from concurrent.futures import ThreadPoolExecutor
    with ThreadPoolExecutor(8) as ex:
        for s, r in ex.map(run, todo):
            if r.returncode != 0:
                errs.append(s + ": " + "\n".join(l for l in r.stderr.splitlines() if "error" in l)[:600])
    if errs:
        print("COMPILE ERRORS:\n" + "\n".join(errs)); sys.exit(1)
    out = pathlib.Path(os.environ.get("TMPDIR", "/tmp")) / ((os.environ.get("NATIVE_OUT") or ("native_link_probe_debug" if DEBUG else "native_link_probe")))
    r = subprocess.run(["clang++", "-arch", "arm64"] + [str(obj(s)) for s in entries() + [main]] + FW + (["-fsanitize=address"] if DEBUG else []) + ["-o", str(out)], capture_output=True, text=True)
    return r.stderr

def defining(sym):
    m = re.match(r"(?:vtable|typeinfo) for (.+)", sym)
    if m:
        cls = m.group(1).split("::")[-1]
        pats = [re.compile(re.escape(cls) + r"::" + re.escape(cls) + r"\(|" + re.escape(cls) + r"::~"), re.compile(re.escape(cls) + r"::")]
    else:
        nm = re.sub(r"\(.*", "", sym); parts = nm.split("::")
        pats = [re.compile(r"\b" + re.escape("::".join(parts[-2:])) + r"\s*\(") if len(parts) >= 2 else re.compile(r"\b" + re.escape(parts[-1]) + r"\s*\(")]
        if len(parts) >= 2:
            pats.append(re.compile(r"^[A-Za-z_][^;\n]*[\s*&]" + re.escape(parts[-1]) + r"\s*\(", re.M))
    have = set(entries())
    for pat in pats:
        hit = [c for c in text if c not in have and pat.search(text[c])]
        if hit:
            hit.sort(key=lambda c: (c.endswith("Creators.cpp"), len(c)))
            return hit[0]
    return None

for rnd in range(rounds):
    err = build()
    syms = sorted(set(re.findall(r'^  "(.+?)", referenced from', err, re.M)))
    dup = re.findall(r"duplicate symbol '(.+?)'", err)
    if dup: print("DUPLICATE SYMBOLS:", dup[:10]); sys.exit(2)
    if not syms: print("LINK CLEAN" if "error" not in err else err[-1500:]); break
    cands = collections.Counter(); unmatched = []
    for s in syms:
        c = defining(s)
        if c: cands[c] += 1
        else: unmatched.append(s)
    if not cands:
        print("UNMATCHED:\n" + "\n".join(unmatched[:60])); sys.exit(3)
    with listf.open("a") as f:
        for c in cands: f.write(c + "\n")
    print(f"round {rnd}: +{len(cands)} files, {len(unmatched)} unmatched")
