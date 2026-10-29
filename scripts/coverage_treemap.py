#!/usr/bin/env python3
"""Render the decompilation coverage treemap (docs/coverage-map.svg), the way OpenSC4's tools/coverage_treemap.py does.

    python3 scripts/coverage_treemap.py [--out docs/coverage-map.svg]

The top of the picture is a funnel: the share of the game's machine-code bytes that has reached each step (code exists ->
recovered -> verified against the machine code). Below it every game function of the binary is drawn as a box: area = the
function's bytes, boxes grouped area > subsystem > owning class, coloured by the furthest step reached, hatched when the
function is recovered on a unit test alone although its decompile shows artifacts that usually hide behaviour (the
exactness worklist of notes/exactness_audit.md).

Inputs: notes/symbols-functions-demangled.txt (function list; a function's size is the distance to the next entry),
notes/ledger.csv (status and evidence per address) and work/*/*.txt (the exports the worklist is read from).
Libraries and compiler thunks are left out of the denominator exactly like scripts/progress.py does; skipped rows stay in it as
their own colour. Same inputs, same file, byte for byte.
"""
import argparse
import bisect
import collections
import csv
import html
import importlib.util
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parent.parent


def _load(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / "scripts" / f"{name}.py")
    module = importlib.util.module_from_spec(spec)
    return spec, module


# progress.py runs its report at import time; read only the two helpers we need from its source
def _progress_helpers():
    source = (ROOT / "scripts" / "progress.py").read_text()
    head = source.split("def main():")[0]
    namespace = {"__name__": "progress_helpers", "__file__": str(ROOT / "scripts" / "progress.py")}
    exec(compile(head, "progress.py", "exec"), namespace)
    return namespace["classify"], namespace["owner_of"]


classify, owner_of = _progress_helpers()
_spec, audit = _load("audit_exactness")
_spec.loader.exec_module(audit)

STEPS = [  # (label, meaning, colour)
    ("Not started", "no code for it yet", "#d5d9d4"),
    ("Partial", "some of it is written; the rest waits on neighbours or hosts", "#9cc3e4"),
    ("Recovered", "written from the decompile and covered by a test", "#e8b04a"),
    ("Verified", "checked against the disassembly, the machine-code oracle or real game data", "#1f6b43"),
]
SKIPPED = ("Skipped", "left out on purpose (thunk aliases, not needed on macOS)", "#c9c3b6")
BG, INK, MUTED, TRACK = "#f6f7f4", "#1d2320", "#5b6560", "#e3e6e0"

# (area, subsystem, regex on the owner); the first match wins
AREAS = [
    ("Simulation", "Simulators", r"^cSC4(\w*Simulator|\w*Developer|DemandSimulator|\w*Manager)$|^cSC4(Budget|Census|Crime|Flammability)"),
    ("Simulation", "Lots and buildings", r"^cSC4(Lot|Building|Zone|Business|Foundation|Flora|Prop|Occupant|Park|MySim)|^cISC4(Lot|Building)"),
    ("Simulation", "Disasters", r"^cSC4(Disaster|Fire|Riot|Volcano|Earthquake)|^cSTE(Volcano|Lava)"),
    ("Simulation", "Region and city", r"^cSC4(City|Region|Neighbor|Nation|App|Tutorial|Advisor|Situation)|^cSC4Regional"),
    ("Automata", "Vehicles", r"^cSC4(Vehicle|Emergency|Ambulance|GetawayVan|CarjackedVehicle|MayorLimo|Train|Watercraft|Pedestrian|Aircraft|Airplane|Blimp|Helicopter|Traffic)"),
    ("Automata", "Automaton core", r"^cSC4(Automat|Dummy|AnimationTick)"),
    ("Networks", "Networks and paths", r"^cSC4(Network|Path|Pipe|Subway|Power(Pole|Line)|Underground|Road|Rail|Bridge|Tunnel|Traffic)|^nSC4Networks|^cSC4(Config|Tile|Int)Rule"),
    ("World and rendering", "Terrain", r"^cSTE"),
    ("World and rendering", "3D and drawing", r"^cS3D|^cIS3D|^nS3D|^nSGLDX7|^cSGLDX7|^nsTexel|^SC4DrawContext|^cSC43D|^cSC4(Render|Lighting|Camera|View3D|Model|Effects?|Particle|Visual|Decal)|^cSC4(Attractor|DynamicParticle)"),
    ("User interface", "Game windows", r"^cSC4(Win|Tool|View|Wm|Situation|Mini)|^cSC4MySimBalloon|^cSC4(Query|Preferences)"),
    ("User interface", "GZ windows", r"^cGZWin|^cIGZWin|^cGZ(Draw|Canvas|Font|Text|Color)|^cSCRes?(Cat|Catalog|Text|Bool|Sint|Uint|Float)"),
    ("Framework", "GZ framework", r"^(cGZ|cIGZ|nGZ|GZ)"),
    ("Framework", "RZ framework", r"^(cRZ|cIRZ|RZ)"),
    ("Framework", "SC resources and properties", r"^(cSC(Res|Exemplar|Property|Prop)|nSCRes|cSCRes|cSLAccessor|SLAccessor|cSL)"),
    ("Audio", "Audio", r"^(cHit|cFresh|cIHit|cIFresh|cSC4Audio|cSC4Sound|cSND|SND|cAEMS|AEMS|cAudio)"),
    ("Scripting", "Lua and scripts", r"^(cSCLua|cGZLua|nsGZLua|LUA|cSC4Lua|cSC4Script)"),
    ("Platform", "Mac glue and web", r"^(Mac|NCT_|cHTML|CQT|iTunes|IUnknown_Mac|cMac)"),
]
FALLBACK = ("Other", "Other classes")


def area_of(owner):
    if not owner:
        return ("Other", "Free functions")
    for area, subsystem, rx in AREAS:
        if re.search(rx, owner):
            return (area, subsystem)
    if owner.startswith("cSC4") or owner.startswith("cISC4") or owner.startswith("SC4"):
        return ("Simulation", "Other SC4 classes")
    return FALLBACK


def load():
    functions = {}
    for line in (ROOT / "notes/symbols-functions-demangled.txt").read_text(errors="replace").splitlines():
        address, _, name = line.partition(" ")
        functions[int(address, 16)] = name
    order = sorted(functions)
    ledger = {int(r["address"], 16): r for r in csv.DictReader(open(ROOT / "notes/ledger.csv"))}

    worklist = set()
    for path in sorted((ROOT / "work").glob("*/*.txt")):
        current, body = None, []

        def finish():
            if current is not None and audit.MARKS.search("".join(body)):
                worklist.add(current)

        for text in open(path, errors="replace"):
            m = audit.HEADER.match(text)
            if m:
                finish()
                current, body = int(m.group(1), 16), []
            else:
                body.append(text)
        finish()
    return functions, order, ledger, worklist


def step_of(row):
    """0 not started, 1 partial, 2 recovered, 3 verified, 'skipped'."""
    if row is None:
        return 0
    status = row["status"]
    if status == "skipped":
        return "skipped"
    if status in ("recovered-partial", "partial"):
        return 1
    if status != "recovered":
        return 0
    return 3 if audit.kind(row["evidence"]) in ("oracle (decompiled code run against the port on random inputs)",
                                                "checked against the disassembly", "real game data") else 2


def collect():
    functions, order, ledger, worklist = load()
    tree = collections.defaultdict(lambda: collections.defaultdict(lambda: collections.defaultdict(int)))
    steps = collections.Counter()
    total = 0
    for index, address in enumerate(order):
        name = functions[address]
        kind, _category = classify(name, address)
        if kind != "game":
            continue
        size = (order[index + 1] - address) if index + 1 < len(order) else 64
        size = max(1, min(size, 1 << 16))
        owner, _thunk = owner_of(name)
        top = owner.split("::")[0] if owner else ""
        step = step_of(ledger.get(address))
        hatch = step == 2 and address in worklist
        area, subsystem = area_of(top)
        label = top or "(free functions)"
        tree[area][subsystem][(label, step, hatch)] += size
        steps[step] += size
        total += size
    return tree, steps, total


def worst(row, w):
    s = sum(row)
    return max(max(row) * w * w / (s * s), (s * s) / (w * w * min(row))) if s and min(row) > 0 else float("inf")


def squarify(values, x, y, w, h):
    """Bruls-Huizing-van Wijk squarified layout; values sorted descending."""
    total = sum(values)
    if total <= 0 or w <= 0 or h <= 0:
        return [(x, y, 0, 0)] * len(values)
    areas = [v * w * h / total for v in values]
    out, i = [], 0
    while i < len(areas):
        side = min(w, h)
        row = [areas[i]]
        j = i + 1
        while j < len(areas) and worst(row + [areas[j]], side) <= worst(row, side):
            row.append(areas[j])
            j += 1
        s = sum(row)
        if w >= h:
            cw = s / h
            cy = y
            for a in row:
                out.append((x, cy, cw, a / cw))
                cy += a / cw
            x, w = x + cw, w - cw
        else:
            rh = s / w
            cx = x
            for a in row:
                out.append((cx, y, a / rh, rh))
                cx += a / rh
            y, h = y + rh, h - rh
        i = j
    return out


def colour(step):
    return SKIPPED[2] if step == "skipped" else STEPS[step][2]


def funnel(svg, steps, total, y0):
    e = html.escape
    bx, bw, bh, gap = 560, 860, 26, 8
    done = steps[1] + steps[2] + steps[3]
    bars = [("Code exists", "partial or complete", done, STEPS[1][2]),
            ("Recovered", "complete and covered by a test", steps[2] + steps[3], STEPS[2][2]),
            ("Verified", "against the machine code", steps[3], STEPS[3][2])]
    for n, (label, meaning, value, fill) in enumerate(bars):
        y = y0 + n * (bh + gap)
        svg.append(f'<text x="16" y="{y + 18}" font-size="16" font-weight="700" fill="{INK}">{e(label)}</text>')
        svg.append(f'<text x="150" y="{y + 18}" font-size="14" fill="{MUTED}">{e(meaning)}</text>')
        svg.append(f'<rect x="{bx}" y="{y}" width="{bw}" height="{bh}" rx="3" fill="{TRACK}"/>')
        share = value / total
        svg.append(f'<rect x="{bx}" y="{y}" width="{bw * share:.1f}" height="{bh}" rx="3" fill="{fill}"/>')
        svg.append(f'<text x="{bx + bw + 12}" y="{y + 19}" font-size="18" font-weight="700" fill="{INK}">{share:.1%}</text>')
    end = y0 + len(bars) * (bh + gap)
    for t in (25, 50, 75):
        x = bx + bw * t / 100
        svg.append(f'<line x1="{x:.1f}" y1="{y0}" x2="{x:.1f}" y2="{end - gap}" stroke="{BG}" stroke-width="1.5"/>')
    for t in (0, 50, 100):
        x = bx + bw * t / 100
        svg.append(f'<text x="{x:.1f}" y="{end + 10}" font-size="11" fill="{MUTED}" text-anchor="middle">{t}%</text>')
    return end + 14


def legend(svg, y):
    e = html.escape
    x = 16
    svg.append(f'<text x="{x}" y="{y + 14}" font-size="14" font-weight="700" fill="{INK}">Colour = furthest step reached:</text>')
    x += 236
    entries = [(label, c) for label, _m, c in STEPS] + [(SKIPPED[0], SKIPPED[2])]
    for n, (label, c) in enumerate(entries):
        svg.append(f'<rect x="{x}" y="{y}" width="18" height="18" fill="{c}" stroke="{INK}" stroke-width="0.5"/>')
        svg.append(f'<text x="{x + 25}" y="{y + 14}" font-size="14" fill="{INK}">{e(label)}</text>')
        x += 25 + int(len(label) * 7.4) + 10
        if n < len(entries) - 1:
            svg.append(f'<text x="{x}" y="{y + 14}" font-size="15" fill="{MUTED}">{"→" if n < len(STEPS) - 1 else "·"}</text>')
            x += 26
    y += 28
    svg.append(f'<rect x="16" y="{y}" width="18" height="18" fill="{STEPS[2][2]}" stroke="{INK}" stroke-width="0.5"/>')
    svg.append(f'<rect x="16" y="{y}" width="18" height="18" fill="url(#hatch)"/>')
    svg.append(f'<text x="41" y="{y + 14}" font-size="14" fill="{INK}">Hatched = recovered on a unit test alone although the decompile shows '
               'artifacts that usually hide behaviour (notes/exactness_audit.md worklist)</text>')


def render(tree, steps, total, out):
    W, H = 1600, 1200
    e = html.escape
    svg = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W}" height="{H}" '
           'font-family="Segoe UI, Helvetica, Arial, sans-serif">',
           '<defs><pattern id="hatch" width="8" height="8" patternUnits="userSpaceOnUse" patternTransform="rotate(45)">'
           '<line x1="0" y1="0" x2="0" y2="8" stroke="#ffffff" stroke-width="2.4"/></pattern></defs>',
           f'<rect width="{W}" height="{H}" fill="{BG}"/>',
           f'<text x="16" y="40" font-size="30" font-weight="700" fill="{INK}">DeSCAi4 decompilation coverage of Sim City 4 (macOS)</text>',
           f'<text x="16" y="66" font-size="15" fill="{MUTED}">Share of the game\'s {total / 1048576:.2f} MB of machine code (libraries '
           'and compiler thunks excluded) that has reached each step. The goal is 100% verified.</text>']
    y = funnel(svg, steps, total, 84)
    svg.append(f'<text x="16" y="{y + 14}" font-size="13" fill="{MUTED}">The map below: one box per class of the original, area = its '
               'bytes, grouped by area and subsystem.</text>')
    legend_h = 62
    mx, my = 16, y + 28
    mw, mh = W - 32, H - my - legend_h - 8
    size_of = lambda subs: sum(b for owners in subs.values() for b in owners.values())
    areas = sorted(tree.items(), key=lambda kv: (-size_of(kv[1]), kv[0]))
    avals = [size_of(subs) for _, subs in areas]
    for (aname, subs), (ax, ay, aw, ah), av in zip(areas, squarify(avals, mx, my, mw, mh), avals):
        pad_t = 26 if aw > 90 and ah > 60 else 2
        sx, sy, sw, sh = ax + 2, ay + pad_t, aw - 4, ah - pad_t - 2
        sitems = sorted(subs.items(), key=lambda kv: (-sum(kv[1].values()), kv[0]))
        svals = [sum(o.values()) for _, o in sitems]
        for (sname, owners), (rx, ry, rw, rh) in zip(sitems, squarify(svals, sx, sy, sw, sh)):
            by_owner = collections.defaultdict(list)
            for (label, step, hatch), b in owners.items():
                by_owner[label].append((b, step, hatch))
            oitems = sorted(by_owner.items(), key=lambda kv: (-sum(t[0] for t in kv[1]), kv[0]))
            ovals = [sum(t[0] for t in g) for _, g in oitems]
            for (label, pieces), (ox, oy, ow, oh) in zip(oitems, squarify(ovals, rx, ry, rw, rh)):
                pieces.sort(key=lambda t: (-t[0], str(t[1]), t[2]))
                for (b, step, hatch), (x, y2, w, h) in zip(pieces, squarify([t[0] for t in pieces], ox, oy, ow, oh)):
                    if w > 0.2 and h > 0.2:
                        title = f'<title>{e(label)}: {b} bytes, {"skipped" if step == "skipped" else STEPS[step][0]}{", unit test only (artifacts)" if hatch else ""}</title>'
                        svg.append(f'<rect x="{x:.1f}" y="{y2:.1f}" width="{w:.1f}" height="{h:.1f}" fill="{colour(step)}">{title}</rect>')
                        if hatch:
                            svg.append(f'<rect x="{x:.1f}" y="{y2:.1f}" width="{w:.1f}" height="{h:.1f}" fill="url(#hatch)"/>')
                if ow > 0.4 and oh > 0.4:
                    svg.append(f'<rect x="{ox:.1f}" y="{oy:.1f}" width="{ow:.1f}" height="{oh:.1f}" fill="none" stroke="{BG}" stroke-width="0.6"/>')
            svg.append(f'<rect x="{rx:.1f}" y="{ry:.1f}" width="{rw:.1f}" height="{rh:.1f}" fill="none" stroke="{BG}" stroke-width="1.8"/>')
            if rw > 80 and rh > 22:
                n = int((rw - 8) / 6.4)
                label = sname if len(sname) <= n else sname[: max(n - 1, 0)] + "…"
                if label:
                    svg.append(f'<text x="{rx + 4:.1f}" y="{ry + 14:.1f}" font-size="11.5" font-weight="600" fill="{INK}" '
                               f'stroke="{BG}" stroke-width="3" paint-order="stroke">{e(label)}</text>')
        svg.append(f'<rect x="{ax:.1f}" y="{ay:.1f}" width="{aw:.1f}" height="{ah:.1f}" fill="none" stroke="{INK}" stroke-width="2"/>')
        if pad_t > 2:
            t = f"{aname.upper()}  {av / total:.1%}"
            n = int((aw - 10) / 10)
            if len(t) > n:
                t = aname.upper()[: max(n, 0)]
            svg.append(f'<text x="{ax + 6:.1f}" y="{ay + 19:.1f}" font-size="16" font-weight="700" fill="{INK}">{e(t)}</text>')
    legend(svg, H - legend_h + 4)
    svg.append("</svg>")
    out.write_text("\n".join(svg) + "\n", encoding="utf-8")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--out", default=str(ROOT / "docs" / "coverage-map.svg"))
    args = ap.parse_args()
    tree, steps, total = collect()
    out = pathlib.Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    render(tree, steps, total, out)
    names = [s[0] for s in STEPS]
    print(f"coverage_treemap: {total} bytes; " + ", ".join(f"{names[i]} {steps[i] / total:.1%}" for i in range(4))
          + f", skipped {steps['skipped'] / total:.1%}; wrote {out}")


if __name__ == "__main__":
    main()
