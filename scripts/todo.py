#!/usr/bin/env python3
"""Integration to-do registry (notes/todo.csv): what must still be done before a recovered class is truly complete / hooked up.

  todo.py list [--open|--ready|--all] [--class X]   ready = open and every blocker is done
  todo.py add ID "Class" "task" [--blocked-by ID,ID] [--where path]   (ID like TERRAIN-VIEW-3)
  todo.py done ID [ID...]
  todo.py sync-ledger        one auto row per class that still has recovered-partial / pending functions (LEDGER-<class>)
  todo.py sync-classes       one row per class with unrecovered functions (CLASS-<class>), blocked by its unfinished base classes
                             (bases from notes/class_bases.json = RTTI); rows close themselves when the class reaches 0 remaining
  todo.py report             counts per class

Rule for agents: finishing a class means adding its follow-ups here in the same commit; finishing a blocker means `todo.py done`."""
import csv, pathlib, sys, collections, re
ROOT = pathlib.Path(__file__).resolve().parent.parent
F = ROOT/"notes/todo.csv"
COLS = ["id", "class", "task", "blocked_by", "status", "where"]

def load():
    return list(csv.DictReader(F.open())) if F.exists() else []
def save(rows):
    rows.sort(key=lambda r: (r["class"], r["id"]))
    with F.open("w", newline="") as f:
        w = csv.DictWriter(f, COLS); w.writeheader(); w.writerows(rows)
def ready(r, by):
    return r["status"] == "open" and all(by.get(b, {"status": "done"})["status"] == "done" for b in filter(None, r["blocked_by"].split(",")))

def main(a):
    rows = load(); by = {r["id"]: r for r in rows}
    cmd = a[0] if a else "list"
    if cmd == "list":
        cls = a[a.index("--class") + 1] if "--class" in a else None
        mode = "ready" if "--ready" in a else "all" if "--all" in a else "open"
        for r in rows:
            if cls and r["class"] != cls: continue
            if mode == "open" and r["status"] != "open": continue
            if mode == "ready" and not ready(r, by): continue
            b = f"  [blocked by {r['blocked_by']}]" if r["blocked_by"] and not ready(r, by) and r["status"] == "open" else ""
            print(f"{r['status']:4} {r['id']:22} {r['class']:24} {r['task']}{b}")
    elif cmd == "add":
        i, c, t = a[1], a[2], a[3]
        if i in by: sys.exit(f"{i} exists")
        rows.append({"id": i, "class": c, "task": t, "status": "open",
                     "blocked_by": a[a.index("--blocked-by") + 1] if "--blocked-by" in a else "",
                     "where": a[a.index("--where") + 1] if "--where" in a else ""})
        save(rows); print("added", i)
    elif cmd == "done":
        for i in a[1:]:
            if i not in by: sys.exit(f"unknown {i}")
            by[i]["status"] = "done"
        save(rows); print("done", ", ".join(a[1:]))
    elif cmd == "sync-ledger":
        cnt = collections.Counter()
        for r in csv.DictReader((ROOT/"notes/ledger.csv").open()):
            if r["status"] in ("recovered-partial", "partial", "pending"):
                m = re.match(r"^(?:non-virtual thunk to |virtual thunk to )?([A-Za-z_][\w]*)::", r["symbol"])
                cnt[m.group(1) if m else "(free)"] += 1
        live = {f"LEDGER-{c}" for c in cnt}
        for c, n in cnt.items():
            i = f"LEDGER-{c}"
            t = f"finish {n} partial/pending function(s) (see notes/ledger.csv status recovered-partial / pending)"
            if i in by: by[i]["task"] = t; by[i]["status"] = "open"
            else: rows.append({"id": i, "class": c, "task": t, "blocked_by": "", "status": "open", "where": "notes/ledger.csv"})
        for r in rows:
            if r["id"].startswith("LEDGER-") and r["id"] not in live: r["status"] = "done"
        save(rows); print(len(cnt), "classes with partial/pending functions")
    elif cmd == "sync-classes":
        import json
        ns = {"__file__": str(ROOT/"scripts/progress.py")}
        exec((ROOT/"scripts/progress.py").read_text().split("def main():")[0], ns)
        classify, owner_of = ns["classify"], ns["owner_of"]
        done = {int(r["address"], 16): r["status"] for r in csv.DictReader((ROOT/"notes/ledger.csv").open())}
        left = collections.Counter(); notstarted = collections.Counter()
        for line in (ROOT/"notes/symbols-functions-demangled.txt").read_text(errors="replace").splitlines():
            addr, _, name = line.partition(" "); ad = int(addr, 16)
            kind, _cat = classify(name, ad)
            if kind != "game" or done.get(ad, "").startswith("recovered"): continue
            owner = (owner_of(name)[0] or "(free)").split("::")[0]
            left[owner] += 1
            if ad not in done: notstarted[owner] += 1
        bases = json.loads((ROOT/"notes/class_bases.json").read_text())
        live = set(left)
        for c, n in left.items():
            i = f"CLASS-{c}"
            blockers = sorted(f"CLASS-{b}" for b in bases.get(c, []) if b in live and b != c)
            t = f"recover {n} remaining function(s) ({notstarted[c]} not started, {n - notstarted[c]} partial/pending)"
            if blockers: t += "; bases still unfinished: " + ", ".join(b[6:] for b in blockers)
            if i in by:
                by[i].update(task=t, blocked_by=",".join(blockers), status="open")
            else:
                rows.append({"id": i, "class": c, "task": t, "blocked_by": ",".join(blockers), "status": "open",
                             "where": "scripts/export_class.py + notes/remaining_functions.csv"})
        for r in rows:
            if r["id"].startswith("CLASS-") and r["class"] not in live: r["status"] = "done"
        save(rows); print(len(left), "unfinished classes registered")
    elif cmd == "report":
        c = collections.Counter((r["class"], r["status"]) for r in rows)
        if "--summary" in a:
            ok = sum(1 for r in rows if ready(r, by))
            print(f"{sum(r['status']=='open' for r in rows)} open, {sum(r['status']=='done' for r in rows)} done, {ok} ready now"); return
        for k in sorted({k[0] for k in c}): print(f"{k:28} open {c[(k,'open')]:3}  done {c[(k,'done')]:3}")
    else:
        print(__doc__)
main(sys.argv[1:])
