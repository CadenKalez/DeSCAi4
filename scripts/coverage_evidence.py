#!/usr/bin/env python3
"""Close the ledger's verification backlog with measured evidence: build registered tests with clang source coverage,
run them, and for every ledger row that names no verification yet, record the test that actually EXECUTES the
function (llvm-cov execution count > 0). Rows whose function no test runs are left alone and listed.

  scripts/coverage_evidence.py                 tests chosen automatically (those compiling a source of an unverified row)
  scripts/coverage_evidence.py NAME...         only these registered tests
  --write                                      update notes/ledger.csv (default: report only)

Matching is conservative: class name, method name and the parameter list (namespaces stripped) must all agree, and
the row's source file must be one the test compiles. Evidence added reads
"executed by scripts/tests/<test>.cpp (llvm-cov <date>)" — it says the function ran under a test, not that it was
compared with the original; oracle rows keep their own wording."""
import collections, concurrent.futures as cf, csv, datetime, json, os, pathlib, re, subprocess, sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
TMP = pathlib.Path(os.environ.get("TMPDIR", "/tmp")) / "sc4-cov"
LEDGER = ROOT / "notes/ledger.csv"
FLAGS = ["-std=c++20", "-w", "-O0", "-g", "-Isrc", "-Iscripts/tests/stubs", "-fprofile-instr-generate", "-fcoverage-mapping"]


def tool(name):
    return subprocess.run(["xcrun", "--find", name], capture_output=True, text=True, check=True).stdout.strip()


def parse_tests():
    run = (ROOT / "scripts/run_tests.sh").read_text()
    tests = {}
    for m in re.finditer(r'^\s*"([A-Za-z0-9_]+):([^"]*)"', run, re.M):
        tests[m.group(1)] = m.group(2).replace("$T", "scripts/tests").replace("$S", "src").split()
    return tests


def has_verification(ev, test_files, entries):
    if re.search(r"\b(oracle|tool|real data|real-data|executed by)\b", ev, re.I): return True
    if re.search(r"scripts/(?:run|check)_\w+\.(?:sh|py)", ev): return True
    if set(re.findall(r"([A-Za-z0-9_]+_test\.cpp)", ev)) & test_files: return True
    return any(re.search(r"\b" + re.escape(e) + r"(?:_test)?\b", ev) for e in entries)


# namespaces the port wraps classes and free functions in (the original has none of them)
NAMESPACES = {"gzcom", "sc4", "gz", "s3d", "lua5", "sgl", "gd", "render", "html", "sc4audio", "detail", "wincompat",
              "threadlib", "RZ", "Fresh", "Hit", "SC4Audio", "GZSnd3D", "nS3D", "ecs"}
NS = re.compile(r"\b(?:std::__1::|std::|[A-Za-z_][A-Za-z0-9_]*::(?=[A-Za-z_~]))")


def split_top(s, sep="::"):
    """split on `sep` outside <> and ()"""
    out, depth, cur, i = [], 0, "", 0
    while i < len(s):
        c = s[i]
        if c in "<(": depth += 1
        elif c in ">)": depth -= 1
        if depth == 0 and s.startswith(sep, i):
            out.append(cur); cur = ""; i += len(sep); continue
        cur += c; i += 1
    out.append(cur)
    return out


def norm_type(t):
    t = re.sub(r"\b(?:std::__1::|std::)", "", t)
    # drop namespace qualifiers everywhere (sc4::, gz::, nGZDrawSoftRender:: ...), keep the last name
    t = re.sub(r"\b[A-Za-z_][A-Za-z0-9_]*::(?=[A-Za-z_~])", "", t)
    t = re.sub(r"\bunsigned int\b", "unsigned", t)
    t = re.sub(r"\s+", "", t)
    return t


def key_of(demangled):
    s = re.sub(r"^(non-virtual thunk to |virtual thunk to |covariant return thunk to )", "", demangled.strip())
    s = re.sub(r"\s+const$", "", s)
    m = re.match(r"^(.*?)(\((?:[^()]|\([^()]*\))*\))$", s)
    if not m: return None
    qual, params = m.group(1), m.group(2)[1:-1]
    parts = split_top(qual)
    while len(parts) >= 2 and parts[-2] in NAMESPACES: parts.pop(-2)   # a port namespace, not a class
    cls = norm_type(parts[-2]) if len(parts) >= 2 else ""   # "" = free function
    meth = norm_type(parts[-1])
    plist = [norm_type(p) for p in split_top(params, ",") if p.strip() and p.strip() != "void"]
    return (cls, meth, tuple(plist))


def build_and_cover(name, files):
    out = TMP / name
    out.mkdir(parents=True, exist_ok=True)
    exe, prof = out / "t", out / "t.profraw"
    srcs = [f for f in files if not f.startswith("-")]
    extra = [f for f in files if f.startswith("-")]
    r = subprocess.run(["clang++", *FLAGS, *extra, *srcs, "-o", str(exe)], cwd=ROOT, capture_output=True, text=True)
    if r.returncode: return name, None, "build failed: " + r.stderr[:300]
    env = dict(os.environ, LLVM_PROFILE_FILE=str(prof))
    try:
        r = subprocess.run([str(exe)], cwd=ROOT, env=env, capture_output=True, text=True, timeout=600)
    except subprocess.TimeoutExpired:
        return name, None, "timeout"
    if r.returncode: return name, None, "test failed"
    subprocess.run([tool("llvm-profdata"), "merge", "-sparse", str(prof), "-o", str(out / "t.profdata")], check=True)
    exp = subprocess.run([tool("llvm-cov"), "export", "-format=text", "-skip-expansions", "-instr-profile",
                          str(out / "t.profdata"), str(exe)], capture_output=True, text=True, check=True).stdout
    funcs = json.loads(exp)["data"][0]["functions"]
    executed = [f["name"] for f in funcs if f.get("count", 0) > 0]
    demangled = subprocess.run(["c++filt"], input="\n".join(executed), capture_output=True, text=True).stdout.splitlines()
    keys = {k for k in (key_of(d) for d in demangled) if k}
    return name, keys, None


def main(argv):
    write = "--write" in argv
    names = [a for a in argv if not a.startswith("--")]
    tests = parse_tests()
    test_files = {f.split("/")[-1] for fs in tests.values() for f in fs if f.endswith("_test.cpp")}
    entries = set(tests)
    rows = list(csv.reader(LEDGER.open()))
    head, body = rows[0], rows[1:]
    ic = {n: i for i, n in enumerate(head)}
    todo = [r for r in body if r[ic["status"]] != "skipped" and not has_verification(r[ic["evidence"]], test_files, entries)]
    srcs_needed = {r[ic["source"]].split("|")[0] for r in todo}
    if not names:
        names = sorted(n for n, fs in tests.items() if any(f in srcs_needed for f in fs))
    print(f"{len(todo)} unverified rows; covering {len(names)} tests", flush=True)
    TMP.mkdir(parents=True, exist_ok=True)
    cover = {}
    with cf.ThreadPoolExecutor(os.cpu_count() or 4) as pool:
        for name, keys, err in pool.map(lambda n: build_and_cover(n, tests[n]), names):
            if err: print(f"  {name}: {err}")
            else: cover[name] = keys
    today = datetime.date.today().isoformat()
    updated, left = 0, collections.Counter()
    for r in todo:
        src = r[ic["source"]].split("|")[0]
        base = src[:-2] if src.endswith(".h") else src.rsplit(".", 1)[0]
        k = key_of(r[ic["symbol"]])
        hit = None
        for name in sorted(cover):
            fs = tests[name]
            if not any(f == src or f.rsplit(".", 1)[0] == base for f in fs) and not src.endswith(".h"): continue
            if k and k in cover[name]:
                hit = name; break
        if hit:
            test_file = next(f for f in tests[hit] if f.endswith("_test.cpp"))
            r[ic["evidence"]] = (r[ic["evidence"]].rstrip("; ") + f"; executed by {test_file} (llvm-cov {today})").lstrip("; ")
            updated += 1
        else:
            left[src or "(no source)"] += 1
    print(f"rows now backed by an executing test: {updated}; still without: {sum(left.values())}")
    for s, n in left.most_common(25): print(f"  {n:4d} {s}")
    if write:
        with LEDGER.open("w", newline="") as f: csv.writer(f, lineterminator="\n").writerows([head] + body)
        print("notes/ledger.csv updated")


if __name__ == "__main__":
    main(sys.argv[1:])
