#!/usr/bin/env python3
"""Incremental, parallel version of scripts/run_tests.sh (same TESTS array, same flags, same PASS/FAIL output).

Each source file is compiled once per flag set into an object cache ($TMPDIR/sc4-obj) and reused until the file or one
of the headers it includes changes (clang -MMD dependency files); tests link from the cache and run in parallel.

  scripts/run_tests_fast.py              build what changed, run every test
  scripts/run_tests_fast.py NAME...      only these tests
  scripts/run_tests_fast.py --clean      drop the object cache first
  scripts/run_tests_fast.py -j 4         limit parallelism (default: number of CPUs)
scripts/run_tests.sh stays the from-scratch reference run."""
import concurrent.futures as cf, hashlib, os, pathlib, re, shutil, subprocess, sys, threading, time

ROOT = pathlib.Path(__file__).resolve().parent.parent
TMP = pathlib.Path(os.environ.get("TMPDIR", "/tmp"))
OBJ = TMP / "sc4-obj"
OUT = TMP / "sc4-tests-fast"
FLAGS = ["-std=c++20", "-Wall", "-Wextra", "-O1", "-fsanitize=address,undefined", "-Isrc", "-Iscripts/tests/stubs"]
TEST_TIMEOUT = 600


def parse_tests():
    text = (ROOT / "scripts/run_tests.sh").read_text()
    body = text.split("declare -a TESTS=(", 1)[1].split("\n)", 1)[0]
    tests = []
    for line in body.splitlines():
        m = re.fullmatch(r'\s*"([A-Za-z0-9_]+):([^"]*)"\s*', line)
        if m:
            files = m.group(2).replace("$T", "scripts/tests").replace("$S", "src").split()
            tests.append((m.group(1), files))
    return tests


_locks, _locks_guard = {}, threading.Lock()


def lock_for(key):
    with _locks_guard:
        return _locks.setdefault(key, threading.Lock())


def up_to_date(obj, dep):
    if not obj.exists() or not dep.exists(): return False
    t = obj.stat().st_mtime
    deps = dep.read_text().replace("\\\n", " ").split(":", 1)[-1].split()
    try:
        return all((ROOT / d).stat().st_mtime <= t for d in deps)
    except FileNotFoundError:
        return False


def compile_one(src, cflags):
    """-> (object path, error text or None)"""
    key = hashlib.sha1(("\0".join([src] + cflags)).encode()).hexdigest()[:16]
    obj = OBJ / f"{pathlib.Path(src).stem}-{key}.o"
    dep = obj.with_suffix(".d")
    with lock_for(key):
        if up_to_date(obj, dep): return obj, None
        cmd = ["clang++", *FLAGS, *cflags, "-MMD", "-MF", str(dep), "-c", src, "-o", str(obj)]
        r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
        if r.returncode:
            obj.unlink(missing_ok=True)
            return None, r.stderr
        return obj, None


def run_test(name, files, pool):
    cflags = [f for f in files if f.startswith("-I") or f.startswith("-D")]
    lflags = [f for f in files if f.startswith("-l") or f.startswith("-L")]
    srcs = [f for f in files if not f.startswith("-")]
    log = OUT / f"{name}.log"
    objs, errors = [], []
    for fut in [pool.submit(compile_one, s, cflags) for s in srcs]:
        o, e = fut.result()
        (objs.append(o) if o else errors.append(e))
    if errors:
        log.write_text("".join(errors))
        return name, False, "".join(errors)
    exe = OUT / name
    r = subprocess.run(["clang++", *FLAGS, *map(str, objs), *lflags, "-o", str(exe)], cwd=ROOT, capture_output=True, text=True)
    if r.returncode:
        log.write_text(r.stderr)
        return name, False, r.stderr
    try:
        r = subprocess.run([str(exe)], cwd=ROOT, capture_output=True, text=True, timeout=TEST_TIMEOUT)
        out = r.stdout + r.stderr
        (OUT / f"{name}.out").write_text(out)
        return name, r.returncode == 0, out
    except subprocess.TimeoutExpired:
        return name, False, f"timed out after {TEST_TIMEOUT}s"


def main(argv):
    jobs = os.cpu_count() or 4
    names = []
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == "--clean": shutil.rmtree(OBJ, ignore_errors=True)
        elif a == "-j": jobs = int(argv[i + 1]); i += 1
        else: names.append(a)
        i += 1
    OBJ.mkdir(parents=True, exist_ok=True)
    OUT.mkdir(parents=True, exist_ok=True)
    tests = parse_tests()
    if names:
        tests = [t for t in tests if t[0] in names]
        missing = set(names) - {t[0] for t in tests}
        if missing: print("unknown tests:", " ".join(sorted(missing))); return 2
    start = time.time()
    failed = []
    # compiles go to a shared pool; each test waits on its own objects from a second pool
    with cf.ThreadPoolExecutor(jobs) as compile_pool, cf.ThreadPoolExecutor(jobs) as test_pool:
        futures = [test_pool.submit(run_test, n, f, compile_pool) for n, f in tests]
        for fut in futures:   # report in registry order
            name, ok, out = fut.result()
            if ok:
                last = out.strip().splitlines()[-1] if out.strip() else ""
                print(f"PASS  {name:<16} {last}", flush=True)
            else:
                failed.append(name)
                print(f"FAIL  {name}", flush=True)
                print("\n".join(out.splitlines()[:15]), flush=True)
    print(f"{len(tests) - len(failed)} passed, {len(failed)} failed in {time.time() - start:.0f}s"
          + (": " + " ".join(failed) if failed else ""))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
