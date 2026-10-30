#!/usr/bin/env python3
"""Code-existence audit of notes/ledger.csv: does a row that claims a recovery have code behind it?

ledger_audit.py checks that rows NAME a verification and coverage_evidence.py proves a function RAN under a test; neither
asks whether the function was written at all. Rows were found claiming "recovered" with no code in the source (the building
development simulator's planner, ~90 lot developer functions), their evidence text copied from other functions of the class.

For every row with status recovered / recovered-partial / partial the audit looks for, in order:
  address   the row's address (0x1...) cited anywhere under src/
  defined   a definition of the function in the row's source files: `Class::method(` (out of line) or an inline body
            `method(...) ... {` inside the class's header (constructors / destructors included)
  thunk     "non-virtual thunk to X": X has code by one of the rules above
  free      a free function (no class): `name(` followed by a body in the row's files
  implicit  a compiler-generated / defaulted constructor, destructor, operator new / delete / = of a declared class
  macro     the name used in code (not comments) of a row file that defines macros (SoftBlit.h's generated operations)
Definitions are searched under all of src/ (out of line) and in the class's declaring headers and the row file's siblings,
with comments stripped, because the ledger's source column is sometimes only the header.
and otherwise reports the row as `none`. A `none` row is not proof of absence (a function can be inlined into another, or
written under a different name), so the report lists them for review; --mark sets those rows to status `pending` with the
reason, which is how they re-enter the recovery queue.

  scripts/ledger_code_audit.py            report only: notes/ledger_code_audit.md and notes/ledger_code_audit_none.csv
  scripts/ledger_code_audit.py --mark     also mark the `none` rows pending in notes/ledger.csv (byte-wise: line endings kept)
"""
import collections, csv, io, pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
LEDGER = ROOT / "notes/ledger.csv"
REPORT = ROOT / "notes/ledger_code_audit.md"
NONE_CSV = ROOT / "notes/ledger_code_audit_none.csv"
CLAIMS = {"recovered", "recovered-partial", "partial"}

_text_cache = {}


def text(path):
    if path not in _text_cache:
        p = ROOT / path
        _text_cache[path] = p.read_text(errors="replace") if p.is_file() else ""
    return _text_cache[path]


def source_files(field):
    """'src/a/B.cpp|h' -> ['src/a/B.cpp', 'src/a/B.h']; 'src/x.h' -> ['src/x.h']."""
    out = []
    for part in field.split("|"):
        part = part.strip()
        if not part:
            continue
        if "/" not in part and out:   # 'h' / 'cpp' suffix shorthand
            base = out[0].rsplit(".", 1)[0]
            out.append(f"{base}.{part}")
        else:
            out.append(part)
    return out


def split_symbol(symbol):
    """'ns::cA::Method(int, x::y)' -> (['ns', 'cA'], 'Method'); operators and templates kept crude."""
    s = re.sub(r"^non-virtual thunk to ", "", symbol)
    s = re.sub(r"^virtual thunk to ", "", s)
    depth, cut = 0, len(s)
    for i, ch in enumerate(s):       # the parameter list starts at the first '(' outside template brackets
        if ch == "<":
            depth += 1
        elif ch == ">":
            depth -= 1
        elif ch == "(" and depth == 0:
            cut = i
            break
    qual = s[:cut]
    parts, depth, cur = [], 0, ""
    for ch in qual:
        if ch == "<":
            depth += 1
        elif ch == ">":
            depth -= 1
        if ch == ":" and depth == 0:
            if cur:
                parts.append(cur)
            cur = ""
            continue
        cur += ch
    if cur:
        parts.append(cur)
    parts = [re.sub(r"<.*", "", p) for p in parts]   # drop template arguments
    if not parts:
        return [], ""
    return parts[:-1], parts[-1]


def strip_comments(t):
    t = re.sub(r"/\*.*?\*/", " ", t, flags=re.S)
    return re.sub(r"//[^\n]*", "", t)


_code_cache = {}


def code(path):
    if path not in _code_cache:
        _code_cache[path] = strip_comments(text(path))
    return _code_cache[path]


def siblings(files):
    out = list(files)
    for f in files:
        base, _, ext = f.rpartition(".")
        for other in ("cpp", "h", "mm", "hpp", "c"):
            if other != ext:
                cand = f"{base}.{other}"
                if cand not in out and (ROOT / cand).is_file():
                    out.append(cand)
    return out


OUT_OF_LINE = None   # every `Class::method(` (Class without template args) seen in code under src/, minus comments
CLASS_FILES = None   # class name -> files declaring `class X` / `struct X`


def build_index():
    global OUT_OF_LINE, CLASS_FILES
    OUT_OF_LINE, CLASS_FILES = set(), collections.defaultdict(list)
    for p in (ROOT / "src").rglob("*"):
        if p.suffix not in {".cpp", ".h", ".mm", ".c", ".hpp"}:
            continue
        rel = str(p.relative_to(ROOT))
        t = code(rel)
        for m in re.finditer(r"\b([A-Za-z_]\w*)(?:<[^;{}()]*>)?::(~?[A-Za-z_]\w*|operator\s*[^\s(]+)\s*\(", t):
            OUT_OF_LINE.add((m.group(1), m.group(2).replace(" ", "")))
        for m in re.finditer(r"\b(?:class|struct)\s+([A-Za-z_]\w*)\b[^;]*?\{", t):
            CLASS_FILES[m.group(1)].append(rel)


def has_definition(files, scopes, method):
    """'defined' / 'implicit' / 'named' / None."""
    if not method:
        return None
    m = re.escape(method)
    cls = scopes[-1] if scopes else None
    if cls and (cls, method.replace(" ", "")) in OUT_OF_LINE:
        return "defined"
    special = cls and (method in (cls, "~" + cls) or method.startswith("operator new") or method.startswith("operator delete")
                       or method == "operator=")
    search = siblings(files) + (CLASS_FILES.get(cls, []) if cls else [])
    declared = bool(cls and CLASS_FILES.get(cls))
    for f in dict.fromkeys(search):
        t = code(f)
        if not t:
            continue
        # an inline body (possibly over several lines): `method(...) ... {` before any ';'
        if re.search(rf"(?<![A-Za-z0-9_~]){m}\s*\([^;{{}}]*\)[^;{{}}]*\{{", t):
            return "defined"
        if re.search(rf"(?<![A-Za-z0-9_~]){m}\s*\([^;{{}}]*\)[^;{{}}]*=\s*default\s*;", t):
            return "implicit"
    if special and declared:
        return "implicit"   # compiler-generated (or = default) special member of a class the port declares
    # generated by a macro (e.g. SoftBlit.h's operations): the name used in code, not only in comments, of the row's files
    for f in siblings(files):
        t = code(f)
        if t and "#define" in t and re.search(rf"(?<![A-Za-z0-9_~]){m}\b", t):
            return "named"
    return None


def main():
    mark = "--mark" in sys.argv
    src_text = []
    for p in (ROOT / "src").rglob("*"):
        if p.suffix in {".cpp", ".h", ".mm", ".c", ".hpp"}:
            src_text.append(p.read_text(errors="replace").lower())
    cited = set(re.findall(r"0x(1[0-9a-f]{8})\b", "\n".join(src_text)))

    build_index()
    raw = LEDGER.read_bytes()
    lines = raw.split(b"\n")
    rows = list(csv.reader(io.StringIO(raw.decode("utf-8", "replace"))))
    head = rows[0]
    col = {name: i for i, name in enumerate(head)}

    by_symbol = {}
    for r in rows[1:]:
        if len(r) > col["source"]:
            by_symbol.setdefault(r[col["symbol"]], r)

    result = {}
    def classify(r):
        addr = r[col["address"]].lower()[2:].lstrip("0")
        if addr in cited:
            return "address"
        files = source_files(r[col["source"]])
        symbol = r[col["symbol"]]
        scopes, method = split_symbol(symbol)
        if symbol.startswith(("non-virtual thunk to ", "virtual thunk to ")):
            target = re.sub(r"^(non-virtual |virtual )thunk to ", "", symbol)
            tr = by_symbol.get(target)
            if tr is not None and tr is not r and classify(tr) != "none":
                return "thunk"
            return "thunk" if has_definition(files, scopes, method) else "none"
        if not files:
            return "none"
        found = has_definition(files, scopes, method)
        if found in ("implicit", "named"):
            return found
        if found:
            return "defined" if scopes else "free"
        return "none"

    counts = collections.Counter()
    per_class = collections.defaultdict(collections.Counter)
    none_rows = []
    for i, r in enumerate(rows[1:], start=1):
        if len(r) <= col["status"] or r[col["status"]].strip() not in CLAIMS:
            continue
        kind = classify(r)
        counts[(r[col["status"]].strip(), kind)] += 1
        scopes, _ = split_symbol(r[col["symbol"]])
        cls = "::".join(scopes) or "(free)"
        per_class[cls][kind] += 1
        if kind == "none":
            none_rows.append((i, r))

    total_claims = sum(v for (s, k), v in counts.items())
    with NONE_CSV.open("w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["address", "symbol", "source", "status", "evidence"])
        for _, r in none_rows:
            w.writerow([r[col["address"]], r[col["symbol"]], r[col["source"]], r[col["status"]], r[col["evidence"]] if len(r) > col["evidence"] else ""])

    worst = sorted(per_class.items(), key=lambda kv: -kv[1]["none"])
    out = ["# Ledger code-existence audit", "",
           "Generated by `scripts/ledger_code_audit.py`. A row that claims a recovery (recovered / recovered-partial / partial) has code",
           "when its address is cited under src/ or its function is defined in its source files. `none` rows are listed in",
           "`notes/ledger_code_audit_none.csv` for review; `--mark` sets them to pending.", "",
           f"Rows claiming a recovery: {total_claims}", "", "| status | address cited | defined | implicit | macro | thunk | free | none |", "|---|---|---|---|---|---|---|---|"]
    for st in ("recovered", "recovered-partial", "partial"):
        out.append(f"| {st} | " + " | ".join(str(counts[(st, k)]) for k in ("address", "defined", "implicit", "named", "thunk", "free", "none")) + " |")
    none_total = sum(v for (s, k), v in counts.items() if k == "none")
    out += ["", f"With code: {total_claims - none_total} ({100 * (total_claims - none_total) / max(total_claims, 1):.1f}%). No code found: {none_total}.", "",
            "## Classes with the most rows without code", "", "| class | none | with code |", "|---|---|---|"]
    for cls, c in worst[:60]:
        if c["none"] == 0:
            break
        out.append(f"| {cls} | {c['none']} | {sum(c.values()) - c['none']} |")
    REPORT.write_text("\n".join(out) + "\n")
    print("\n".join(out[7:13]))
    print(f"with code {total_claims - none_total} / {total_claims}; none {none_total}")

    if mark and none_rows:
        reason = "code audit: no definition or cited address found (scripts/ledger_code_audit.py)"
        targets = {i for i, _ in none_rows}
        for i in targets:
            line = lines[i]
            cr = line.endswith(b"\r")
            body = line[:-1] if cr else line
            r = next(csv.reader([body.decode("utf-8", "replace")]))
            r[col["status"]] = "pending"
            r[col["evidence"]] = reason
            buf = io.StringIO()
            csv.writer(buf, lineterminator="").writerow(r)
            lines[i] = buf.getvalue().encode("utf-8") + (b"\r" if cr else b"")
        LEDGER.write_bytes(b"\n".join(lines))
        print(f"marked {len(targets)} rows pending")


if __name__ == "__main__":
    main()
