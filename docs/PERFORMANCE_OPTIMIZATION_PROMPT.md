# Repo audit (2026-10-23) and the performance / optimization prompt

This file has two parts:
1. An audit of the repo after the DeepSeek / parallel-session work, and where the project stands.
2. A ready-to-paste prompt for the agent that does the performance and optimization work.

---

## Part 1 — Audit

### Progress

The figures come from `scripts/progress.py`.

**Recovered so far: 18,968 of 26,880 game functions, or 70.57%.**
- 7,912 functions remain.
- At the start of the 2026-10-22 session the count was 15,414 (57.34%).

| Layer | Functions | Recovered | Remaining |
|---|---:|---:|---:|
| cSC4 game logic (cSC4*, SC4*, cSTE*, cSL*) | 13,473 | 7,406 | 6,067 |
| cGZ framework | 6,941 | 6,288 | 653 |
| other game classes | 2,129 | 1,486 | 643 |
| cRZ framework | 1,549 | 1,422 | 127 |
| free C functions | 1,090 | 706 | 384 |
| Audio, Aspyr/Mac glue | 1,112 | 1,112 | 0 |
| Graphics driver layer | 520 | 484 | 36 |

Ledger statuses:

| Status | Rows |
|---|---:|
| recovered | 22,926 |
| recovered-partial | 1,431 |
| partial | 126 |
| pending | 171 |
| skipped | 133 |

The remaining work is mostly cSC4 game logic: simulators, tools, views and occupants. There is also a backlog of about 1,700 rows that are partial or pending. These rows are blocked on seams or on neighbouring classes that have not been recovered yet.

### What the DeepSeek / other-session work looks like

Since this session started, 50 commits on `main` carry no `Co-Authored-By` trailer. They come from DeepSeek and the parallel "Decompilation continuation" session. Typical recent commits:
- `cRZURL`, `cSC4MySim`, `cGZWinDragAndDropManager`
- `cSC4PowerLineTool`, `cSC4SignpostOccupant`, `cGZScriptTerp`
- `cSC4EffectsCollection`, `cRZDiskRadioStation`, `cGZUIScriptService`, `nGZLog::cService`
- the IME classes, `cRZCookieManager`, `cSC4WinTertiaryMenuFrame`, `cSC4PropOccupantVisualEffect`, `cSC4WinAnimationRecorder`

There were also audit fixes: a provenance audit (`scripts/ledger_audit.py`) and a stricter `verify_class_done.py`.

Quality checks on that work:

| Check | Result |
|---|---|
| Hand-written code | Yes. The 40 new source files (14.5k lines) contain no decompiler output: no `param_N`, `undefined8`, `CONCAT` or raw `this + 0x..` casts. |
| Unique names | `check_unique_names.py`: 24,787 names, 0 duplicates. |
| `sub_`/`FUN_` names, rows without evidence | None. |
| Tests | Every new class comes with a test. The 2026-10-23 run of all 361 registered tests was still in progress when this file was written, with no failures so far. |
| TODO markers without a to-do id | None in the new files. |

### Issues found (most important first)

1. **Concurrent writers corrupt the shared bookkeeping.** Several tools and agents committed to `main` from one checkout at the same time. This caused three kinds of damage:
   - A lost update that dropped about 200 `notes/ledger.csv` rows; they were restored in `2d99dc4`.
   - Malformed or duplicated `scripts/run_tests.sh` entries: an unquoted duplicate of `gz_luascriptserver`, and entries without the leading space.
   - A test helper overwritten by a different file with the same name. `scripts/tests/fake_filebrowser.h` was replaced, so `gz_winfilebrowser` broke; it was restored as `fake_filebrowser_buffers.h`.

   A further problem came from a header change: a constant rename in `src/gz/WinFlatRect.h` broke `gz_winflatrect`.

   **Rule from now on:** only one writer edits `notes/ledger.csv`, `notes/todo.csv`, `scripts/run_tests.sh` and `docs/SEAMS.md`, through `scripts/apply_agent_results.py`. Everyone else uses the pending files. Run the full suite before every commit that touches a shared header.
2. **Missing attribution.** 50 commits lack the required `Co-Authored-By` line (AGENT_RULES.md, hard rules). That makes it impossible to tell later which tool wrote what.
3. **A tool is still active.** At audit time, `scripts/verify_class_done.py`, `notes/ledger.csv` and the new `scripts/ledger_audit.py` were being edited and were uncommitted. Finish or stop that tool before anyone else commits.
4. **Ledger hygiene:**
   - Six rows name `s3d/IndexGeneration.h` without the `src/` prefix.
   - `s3d_realdata_test.cpp` is named as evidence but is not registered in `run_tests.sh`.
   - Ghidra local names are left in comments (for example `// iVar7` in `src/sc4/WinTertiaryMenuFrame.cpp:214-223`). This is cosmetic.
5. **`SimCity 4.cfg` is tracked** but rewritten every time the game runs. Untrack it (`git rm --cached`) and add it to `.gitignore`.
6. **The test suite is slow.** `scripts/run_tests.sh` rebuilds every test from scratch, one at a time, with ASan/UBSan. With 361 tests, one run takes well over 30 minutes. This is the biggest drag on the development loop (see Part 2, task A).
7. **The app build is basic.** `scripts/build_app.sh` is a single arm64 `-O2` command line with no LTO, no PGO, no release/debug split and no x86_64 slice. The ECS layer the architecture rules ask for (`src/ecs/`) is only two headers so far: `JobSystem.h` and `SimdKernels.h`.

---

## Part 2 — Performance and optimization prompt (paste as the agent's first message)

> You are optimizing the SimCity 4 macOS rebuild in /Users/fernandojosesampaio/Downloads/SC4-Decomp (C++20, arm64 first,
> x86_64 second, native Metal rendering). Read `.claudecode`, `AGENT_RULES.md`, `RENDER_STATUS.md`, `RENDER_ARCH.md` and
> `docs/PERFORMANCE_OPTIMIZATION_PROMPT.md` (Part 1) first.
>
> **Ground rules (non-negotiable)**
> - The classes in `src/sc4`, `src/gz`, `src/rz`, `src/s3d`, `src/lua` are the *faithful reference* recovered one function
>   at a time from the binary. Do not change their behaviour, layout comments or quirks for speed. Performance work goes
>   in the shipping layer (`src/ecs/` and the Metal renderer), and every optimized path is validated against the faithful
>   class with a test that compares results. Simulation must match bit for bit; rendering is checked with the diff harness
>   in `scripts/run_render_diff.sh`.
> - Profile first (`xcrun xctrace record --template 'Time Profiler'`, Instruments, `-fsave-optimization-record`). No
>   optimization without a measured hotspot and a before/after number recorded in `notes/perf.md`.
> - SIMD through `<arm_neon.h>` / SSE2/AVX2 intrinsics or Clang vector extensions with a compile-time switch *and* a scalar
>   fallback; never inline assembly. No FMA contraction or reassociation in simulation kernels
>   (`-ffp-contract=off` for those files) so the paths stay bit-identical (see `src/ecs/SimdKernels.h`).
> - Data-oriented design for simulation data: structure-of-arrays, contiguous storage, no per-entity heap allocation, no
>   virtual calls in hot loops.
> - Single writer for shared bookkeeping: only commit through the normal flow, end every commit with the
>   `Co-Authored-By` line, never push, never commit game assets, binaries or Ghidra databases, ask before sudo or large downloads.
>
> **Tasks, in order**
> A. *Developer loop.* Make `scripts/run_tests.sh` incremental and parallel:
>    - compile each source once per flag set into a cache (`$TMPDIR/sc4-obj/<hash>.o`, keyed on file content and flags);
>    - link per test;
>    - run tests with `xargs -P $(sysctl -n hw.ncpu)`;
>    - keep a `--clean` switch;
>    - keep the output format (`PASS`/`FAIL name`) and the TESTS array format.
>
>    Add a `scripts/check_test_registry.py` lint for the TESTS array. It checks for:
>    - duplicate names;
>    - unquoted lines;
>    - missing files;
>    - entries without the leading space.
>
>    Target: a full run in under 5 minutes, and under 1 minute when nothing changed. Report both times.
> B. *Build.* Replace the single command in `scripts/build_app.sh` with CMake + Ninja (or an equivalent Makefile). It needs:
>    - `Debug` (ASan/UBSan) and `Release` configurations;
>    - `-O3 -flto=thin -fvisibility=hidden -fno-plt -dead_strip` for Release;
>    - an optional universal arm64/x86_64 build;
>    - PGO hooks (`-fprofile-instr-generate` / `-fprofile-instr-use`).
>
>    Measure binary size and startup time before and after.
> C. *Measure the game.* Add a headless benchmark (`scripts/bench_sim.sh`, `src/tools/bench_sim.cpp`) that loads a real
>    city from the user's saves (read-only) and runs N simulation months on the faithful simulators, then prints time
>    per simulator per month. The simulators to cover are power, plumbing, traffic, pollution, land value, crime, fire,
>    police, budget, flora and building development. Profile it and write the top 10 hotspots into `notes/perf.md`.
> D. *ECS shipping layer for the measured hotspots.* For each hotspot (expected candidates: grid decay/diffusion in
>    `cSC4SimGrid` users, power/water flood fills, the traffic path finder `cSC4PathFinder`/`cSC4PathInfo`, occupant
>    queries by rectangle):
>    - pack the data SoA in `src/ecs/`;
>    - parallelize with `ecs::JobSystem::ParallelFor` where the original order does not matter;
>    - vectorize the inner loop.
>
>    For each, add a test that runs the faithful class and the ECS version on the same random and real inputs and
>    asserts identical results (bit-identical for floats). Keep the faithful class as the oracle.
> E. *Rendering.* With the Metal driver (`src/render/gd/MetalGDriver.*`):
>    - persistent MTLBuffers (triple-buffered ring, no per-draw allocation);
>    - batch draws by packed render state (`SC4RenderStateCache`);
>    - a texture upload cache keyed by resource;
>    - argument buffers or texture arrays for terrain;
>    - `MTLHeap` for transient resources.
>
>    Measure frame time with `MTLCaptureManager` and `os_signpost` in a terrain plus city scene. Keep the render
>    diff harness green after every change.
> F. *Memory.* Replace per-object `new` in hot occupant, automata and particle code paths of the shipping layer with
>    pools/arenas. Track the peak RSS of the benchmark, and run with ASan + LeakSanitizer in Debug.
> G. *Audio and I/O.*
>    - Check that the audio thread never allocates or locks; measure the callback time.
>    - Make DBPF reads use `mmap` and cache decompressed RefPack records.
>    - Measure load time of a large city before and after.
>
> **Definition of done for each optimization**
> - A measured before/after number is recorded in `notes/perf.md`, with machine, commit and command.
> - An oracle test against the faithful class is registered in `scripts/run_tests.sh` and passes under ASan/UBSan.
> - The scalar fallback is tested on both arm64 and the x86_64 build (Rosetta is acceptable for the check).
> - The full test suite and the render diff harness pass; the commit ends with the attribution line.
>
> End each session with a short report:
> - what was measured;
> - what changed;
> - the speedups;
> - what is next in `notes/perf.md`.
