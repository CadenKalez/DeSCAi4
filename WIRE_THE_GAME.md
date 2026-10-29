# WIRE_THE_GAME — the campaign prompt for DeSCAi4

> Paste into Claude Code: **"Read WIRE_THE_GAME.md and follow it until I tell you to stop."**
> This file is the mission. AGENT_RULES.md is the contract for *how* to work (ledger, tests, commits, parallel agents).
> Where the two disagree on workflow mechanics, AGENT_RULES.md wins. Where they disagree on priorities, this file wins.

---

## 1. The mission

You are continuing DeSCAi4, the hand-written C++20 decompilation of SimCity 4 (Aspyr macOS v1.2.1 build 185949,
x86_64) in this folder, with a native macOS host.

The goal is **not a higher percentage**. The goal is a native **Apple silicon (arm64) build, rendering through native
Metal**, that on first boot against the retail GameData **plays exactly like the original**:

> region screen → found a new city → terraform → build a road → zone residential → place a power plant →
> buildings grow → save → reload → the same city the original game produces.

Only after that is true do we make it faster (threads, cache-friendly data, memory efficiency).

### Where the project really stands (read this honestly)
- ~83% of game functions are *transcribed*. That is not the same as *working*.
- **307 seams** (`I*Host` interfaces) stand in for engine services. Most are implemented only by test fakes, so in the
  running game nobody answers them and features silently do nothing. **This is the main reason the game boots but
  most things don't work.**
- **~1,800 partial rows** (recovered-partial + partial) and ~21 pending. They cluster exactly where the visible bugs are:
  graphics (`cGZGraphicSystem`, `cGZDrawContext`, `SC4DrawContext`, the `RENDER3D-2` model queue), roads/tools
  (`cSC4NetworkTool`), query (`cSC4ViewInputControlQuery`), growth (`cSC4LotDeveloper`, `cSC4ResidentialSimulator`),
  traffic (`cSC4TrafficSimulator`), power (`cSC4PowerPoleOccupant`, `cSC4PowerSimulator`).
- **~45 GZCOM creators return `nullptr`** (recount — the README and handoff disagree). SC4 is defensive about null
  COM objects, so a missing creator = a silently missing feature.
- **95 "not needed on macOS" skips** are judgment calls, and some are wrong: `cSC4View3DWin` (35, the main city view)
  and `cSC4PathFinder` (6, traffic) are on the play path.
- **False positives exist**: three `cSTETerrain` functions were wrong with tests that agreed with the bugs; seven
  automaton rows claimed bodies that did not exist. A test written from the same misreading as the code proves nothing.
- "It boots" today means: the cSC4App spine, the splash and the region toolbar. The game loop in
  `src/experimental_host/pending/` is not linked, `LoadCity` stops at component creation, and nothing drives
  `cSC4App::Run` with a renderer attached.

---

## 2. Read first, in order
`AGENT_RULES.md`, `notes/SESSION_HANDOFF.md`, `notes/progress.md`, `docs/SEAMS.md`, `RENDER_STATUS.md`,
`RENDER_ARCH.md`, `src/experimental_host/README_EARLY_TEST.md`, `src/experimental_host/pending/README.md`,
`git log -30`.

---

## 3. Vocabulary — use these words precisely in every report

| Word | Meaning here | Danger |
|---|---|---|
| **recovered** | Function fully transcribed and verified against the binary | Still spot-check (false positives happened) |
| **recovered-partial / partial** | Transcribed except one piece that calls an unrecovered neighbour (named in the evidence) | Honest; the neighbour is a to-do |
| **pending** | Cannot be written until a dependency exists | Blocked work |
| **skipped** | Deliberately not written: compiler thunks/dtor aliases (fine) or "not needed on macOS" (re-check) | Judgment calls hide real gaps |
| **stub** | Placeholder that compiles but doesn't do the job (`return nullptr/0/true`) | **Worst case:** the game silently skips the feature |
| **fake** | Test-only stand-in for a seam | Fine in tests; means "nobody answers" in the real game |
| **seam** | `I*Host` interface where a real class must be plugged in | Unwired seam = dead feature |
| **thunk** | Compiler-generated pointer-adjust jump | Never a problem |
| **headless** | Runs with no window (boot_probe, PNG renders) | A headless pass does not prove it looks or plays right |

**Never report "done" without stating which stubs, fakes, partials or unwired seams remain on that path.**

---

## 4. Ground truth — non-negotiable

1. **The shipped binary is the only authority.** Verify against `otool -arch x86_64 -tV` (`scripts/disfn.py`).
   Ghidra output is a hint; the Ghidra MCP is unreliable.
2. **Existing code and existing tests are not evidence.** Prefer oracle tests (the original machine code run on the
   same inputs) or real game data.
3. **Never fake a recovery.** No stub that returns success to get past a check. If blocked, leave it partial and name
   the missing neighbour. A crash with a clear reason beats a silent skip.
4. **Faithful first.** Keep original object layouts, calling conventions, quirks and bugs (commented). No ECS, SIMD,
   threading or "improvements" in the faithful classes. Optimisation lives in a separate layer after milestone M6.
5. **Deliberate divergences** (fixing a real race, a use-after-free the original got away with) are allowed only when
   recorded in the ledger as a divergence with the reason. No silent behaviour changes.
6. Never `git add -A src/` or `git checkout -- src/` in this shared checkout. Name exact paths.

---

## 5. The equivalence standard — what "recovered" must mean

**Required: bit-exact behaviour.** For the same inputs and the same starting memory, a recovered function must produce
exactly what the original machine code produces: return value, every byte of memory it writes (object fields, out
parameters, globals), float results to the bit, and the same sequence of calls to other functions with the same
arguments. "Looks equivalent" or "the test passes" is not enough.

How to prove it, strongest first:
1. **Differential test against the original machine code.** Load the shipped x86_64 binary into an emulator harness
   (Unicorn or the existing oracle scripts, `scripts/run_bit1_oracle.sh`), run the original function and the
   recovered one on the same inputs — random, edge cases (0, negatives, max/min, NaN, empty containers, null) and
   values captured from real game data — and compare outputs and memory byte for byte. Calls to other functions are
   intercepted and logged on both sides; the call logs must match too.
2. **In-game differential** with the injection harness (section 8): swap the function into the original running under
   Rosetta, play the scenario, and compare city-state hashes with the unmodified original.
3. **Real game data** round-trips (load → save → byte-compare) for serialization code.

A function verified only by a hand-written test from the same reading as the code stays `recovered-partial` with
evidence `needs differential`. The ledger evidence column names which method proved it.

**Optional: byte-matching.** Where it is cheap, also check that the x86_64 reference build, compiled with the original
toolchain (identify it from the binary's load commands, `otool -l`), produces identical machine code for that
function. A match is the strongest proof and is recorded in the evidence; a mismatch is not a failure if the
differential test passes. Never contort readable code to force a match. Byte-matching cannot apply to the arm64
build, so on arm64 the differential tests and determinism hashes (section 11) are the proof.

**Behaviour to preserve exactly, including bugs:** integer overflow and wraparound, float→int conversion results,
rounding, uninitialised-but-deterministic reads the original relies on, evaluation order of side effects, RNG call
order, and iteration order of containers (hash maps that iterate differently change the simulation). Undefined
behaviour in the original is reproduced explicitly with defined C++ (the result x86 actually produced), with a comment.

---

## 5. Phase 0 — make "not wired" visible (do once, first)

1. Add `SC4_UNWIRED("Interface::Method")`: logs once per site with a hit counter to stderr and `work/unwired.log`;
   compiled into host/app builds.
2. Put it in: every seam fake/default path the host uses; every GZCOM creator that returns `nullptr`; the unrecovered
   branch of every recovered-partial function; every `SL::sp*` service lookup that can return null.
3. Add a scripted mode to the host: `--script scenarios/first_city.txt` drives the mission path (section 1) with
   synthetic input and exits. Runs must be repeatable and headless-capable, with an optional windowed mode.
4. Create `scripts/unwired_report.py`: ranks `work/unwired.log` by hits and maps each site to its ledger rows and
   todo ids.
5. Add **`--threads=1`** now (trivially true today) so the reference mode exists before any threading.

Commit Phase 0 before anything else.

---

## 6. Phase 1 — audit what is already claimed (false-positive sweep)

- Create `scripts/audit_bodies.py`: for every `recovered` row, confirm a body exists at the cited source with a
  matching address comment. Missing → downgrade to `pending`, evidence `audit: no body`.
- For each class in the top 30 of the unwired report, re-diff 5 random recovered functions against otool. Every
  mismatch → todo row → fix.
- Reopen the 95 "not needed on macOS" skips. Anything the Mac retail binary reaches on the play path is recovered
  (at least `cSC4View3DWin`, `cSC4PathFinder`). A skip stays only with evidence the macOS binary never calls it.
- Recount GZCOM creators returning `nullptr`; list them.
- Resolve doc contradictions you find (e.g. whether the splash is drawn by game code or painted by the host) by
  checking the code, and fix the docs.

---

## 7. Phase 2 — the wiring loop (repeat until told to stop)

1. Run the scenario. Take the highest-ranked unwired site on the critical path of the **earliest failing milestone**.
2. Classify and fix:
   - **a. Seam whose real class is recovered** → write the real adapter in the app (never a test fake), register the
     service where the original registered it (same class id / same `SL::sp` slot), keep fakes out of shipping
     targets, regenerate `docs/SEAMS.md` with `scripts/list_seams.py`, tick the entry.
   - **b. Seam whose real class is partial/unrecovered** → recover that class (per-class loop in AGENT_RULES.md),
     then (a).
   - **c. Partial function** → recover the named missing operand, re-verify the **whole** function against otool,
     close the row with `ledger_upgrade.py`.
   - **d. Null creator** → wire it; check the class id against the binary's registration code.
   - **e. Never-started function on the path** → recover it with the normal loop.
3. Re-run the scenario **and** `python3 scripts/run_tests_fast.py`. The change counts only if that site's unwired
   count is now zero, the scenario gets at least as far as before, and all tests pass.
4. Ledger, todo (`todo.py sync-ledger`, `sync-classes`), `verify_class_done.py`, commit with exact paths.

### Known high-value targets (from the 2026-10-23 handoff)
1. Finish `LoadCity`: link every `gzcom_creators/*.cpp` and what they pull in into the city probe.
2. Link the game loop from `pending/` and run `--play N`.
3. `cSC4LoadExemplarString` (0x100121132) — blocks advisor/catalog/text rows.
4. `cISC4GameDataRegistry` — blocks automaton rows and the `cSCLua` registry bridge.
5. The three window procedures `AddGUI` needs: `cSC4BudgetUIWinProc`, `cSC4GraphsViewWinProc`,
   `cSC4AdvisorPopupTracker`.
6. Partials with named operands: `cSC4TrafficSimulator` (see `notes/ledger_applied/`), `cSC4Automaton`.

### Optional power tool — run recovered code inside the original
The recovered code keeps the original layouts and calling convention, so an **x86_64** build of it can be injected
into the original game running under Rosetta, redirecting original functions one at a time (the re3 / OpenRCT2
approach). The game stays playable while functions are swapped, and any breakage points at the last swap. Seams in
this mode simply call the original's objects. Use it to validate subsystems that are hard to test otherwise. It is a
verification harness, not the shipping build.

---

## 8. Milestones — the earliest failing one sets the priority

| | Milestone | Done when |
|---|---|---|
| **M1** | Game loop linked | `--play` reaches the region screen through `cSC4App::Run` |
| **M2** | City loads | `LoadCity` opens a retail save; terrain drawn through `cMetalGDriver` |
| **M3** | City looks right | Buildings, props, automata drawn (RENDER3D queue, `SC4DrawContext`); full game UI (all `AddGUI` windows, budget, graphs, advisors) |
| **M4** | Tools work | Terraform, roads, zoning, query |
| **M5** | Simulation runs | Power, growth, traffic, monthly budget; one game year with no asserts |
| **M6** | Fidelity | Save → reload → identical city; cross-check with the original game (under Rosetta) on the same scenario, both directions |
| **M7** | Audio | A real audio device opens; region music and effects play |
| **M8** | Performance | Section 12 — only after M6 |

Write the milestone reached into `notes/SESSION_HANDOFF.md` at every stop.

---

## 9. Rendering — native Metal, no emulation or translation layers

The game draws through its own driver interface, `cIGZGDriver` (Windows had D3D7 behind it, the Mac had GL). A native
Metal implementation of that interface (`cMetalGDriver`) **is** the native path. The shipping build contains no
OpenGL, CGL, AGL, D3D7, DirectDraw, GL-on-Metal or generic GL state machine. The DX7 shim, `MetalGL` and
`GDriverOnGL` are reference/harness only. `scripts/verify_shipping_render.sh` must pass before any render milestone
is called done. Fixed-function state becomes Metal shaders **directly from SC4's own packed render state**.

Work list, in order:
1. **Texture table** with the shipped handle/ID semantics; BC1/2/3 (DXT) uploads (native on Apple silicon) and the
   16-bit formats; mipmaps.
2. **Shader variants from render state**: one Metal shader with function constants; pipeline-state cache keyed by
   packed render state + texture-stage setup; alpha test → `discard_fragment` (threshold 0.5); fog and per-vertex
   lighting in shaders; sampler-state cache; persist compiled pipelines in an `MTLBinaryArchive`.
3. **Persistent backbuffer — critical.** SC4 redraws only dirty rectangles over a backing store. Metal drawables are
   not preserved between frames, so render into our own persistent `MTLTexture` and blit it to the drawable each
   frame. Port `cSC43DRender`'s direct GL calls to the `IBackingStore` service, not to an emulation.
4. **Primitives**: quads and fans → indexed triangles (done); line lists for grid overlays.
5. **Buffers**: persistent `MTLBuffer`s for terrain/models; per-frame ring buffer for dynamic vertices, triple
   buffering with a semaphore.
6. **Coordinates**: Metal depth 0..1 matches D3D; flip Y origin and scissor rects; depth bias for decals/lot
   textures; non-sRGB pixel formats so colours match.
7. **2D UI**: recover `cGZDrawContextRenderToSimGL`'s textured-quad path on Metal (bring-up may upload the software
   buffer as one texture per frame).
8. **Read-back**: city thumbnails, region snapshots, screenshots.
9. **Window**: Retina scale, resize, fullscreen, display-linked pacing (`CAMetalLayer`/`CVDisplayLink`); stop rendering
   when paused or hidden; cap at display refresh.
10. **Tooling**: Metal validation layer and GPU frame capture on; the native-vs-reference diff harness
    (`scripts/run_render_diff.sh`) is the regression gate.
11. The real bottleneck will be CPU draw submission for thousands of models: batch by texture and render state.

---

## 10. Apple silicon (arm64) rules

The code is C++ compiled by clang, so arm64 is native. Protect behaviour where x86 and ARM differ:
- **`-ffp-contract=off`** for all game/simulation code (arm64 fuses multiply-add by default → different rounding →
  divergent simulation and saves).
- **Float→int overflow**: x86 yields `0x80000000`, ARM saturates. Find original code that relies on it; reproduce the
  x86 result explicitly and comment it.
- **Integer division by zero**: x86 traps, ARM returns 0. Make the original's behaviour explicit where it matters.
- **Memory ordering**: ARM is weaker than x86. Anything shared between threads uses `std::atomic` or locks.
- **`long double`** is 64-bit on Apple arm64 (80-bit on x86): audit uses.
- **16 KB pages**: nothing may assume 4 KB.
- Intel-only code (SSE intrinsics, `rdtsc`, inline asm) → NEON / clang vector extensions with a scalar fallback,
  `mach_absolute_time`, plain C++.
- Rebuild third-party libraries (FreeType, the patched Lua, zlib/png) from source for arm64; audio output via CoreAudio.
- Keep an **x86_64 build** too: it runs under Rosetta next to the original for comparisons and the injection harness.
  The arm64 build ships (a Universal app is fine).
- Compare arm64 vs x86_64 builds on the same save over N months: city-state hashes must match.

---

## 11. 64-bit and save files

The base is already 64-bit; the 2–4 GB wall of the 32-bit Windows game does not exist here. Protect two things:
- **64-bit pointers must never leak into the save format.** Saves keep the original field sizes (see the version-62
  32-bit id fix in the handoff). Test: save in our build → open in the original (Rosetta), and the reverse.
- Larger city tiles are **out of scope** (they break save compatibility). Possible later as an explicit opt-in mode.

---

## 12. Performance phase (only after M6)

All of this lives in a separate layer (the planned `src/ecs/`) built *from* the faithful classes and validated
against them. Every change passes the **determinism gate**: load the same save, run 12 game months with
`--threads=1` and with the optimisation on, compare a hash of the whole city state. Match → keep. Mismatch → revert.
Profile first with Instruments (Time Profiler, CPU Counters, Allocations, VM Tracker, Leaks); optimise what the
measurement shows.

### 12.1 Threading
**Step 1 (allowed early, because it fixes crashes):** build with `-fsanitize=thread` (separate from the ASan build),
play the scenario, fix every race in the original's own threads (`cGZDBSegmentDirectoryFiles` background loader,
`cGZSndSys`, name lookup; locks `cRZMutex`, `cRZCriticalSection`, `cRZThreadSignal`). This is the likely cause of
the classic "multicore crash" (`-CPUCount:1`). Record each fix as a deliberate divergence.

**Step 2 — layout.** macOS cannot pin threads to cores; assign QoS classes and let the scheduler place them. Size
pools from `sysctl hw.perflevel0.physicalcpu` / `hw.perflevel1.physicalcpu`; never hard-code 4+4.

| Thread | Work | QoS |
|---|---|---|
| Main | Input, UI, message pump, Metal encoding from the latest snapshot. Never waits for the simulation | userInteractive |
| Simulation | The authoritative tick, original order; the only thread that changes the city | userInitiated |
| Worker pool (P-cores − 2; the sim thread joins during parallel phases) | Grid maps, power/water islands, lot scoring | userInitiated |
| Traffic job | Routes computed in batches, started on tick N, collected on tick N+k (OpenTTD link-graph pattern) | utility |
| Streaming | `.dat` reads, decompression, texture prep | utility |
| Save | Background save from a change-tracked copy | background |
| Audio | MP3 decode (utility); mixing on CoreAudio's real-time thread | utility / system |

**Step 3 — the tick.**
1. *Collect (serial):* apply results due this tick (traffic, loaded resources); process messages.
2. *Parallel:* jobs read last tick's state and write only their own slice of the new state — pollution, land value,
   crime, desirability in blocks; each power/water island; candidate-lot scoring. Nothing is added or removed here.
3. *Apply (serial, original order):* developers place/demolish lots, consuming the RNG in the original order; budget;
   launch traffic jobs with a due tick.
4. *Publish:* write a render snapshot (changes only) into one of three buffers; the main thread draws the newest
   complete one.

Patterns borrowed: sim/render split (Supreme Commander, Cities: Skylines), dedicated pathfinding workers (Cities:
Skylines), jobs joined at a fixed future tick (OpenTTD), independent islands (Factorio), double-buffered grid jobs,
command buffers for structural changes (Unity DOTS). Stays serial: placement, the occupant manager, `cGZMessageServer`.
Warning example: RimThreaded — retrofitting threads onto single-threaded code without a reference breaks games.

Thermals (fanless MacBook Air): cap to display refresh, idle when paused/hidden, shrink the worker pool in Low Power Mode.

### 12.2 Cache-friendly data (M1: 128 KB L1D per P-core, 12 MB shared L2, **128-byte cache lines**)
- **Pathfinding acceleration structure**: compact CSR graph (nodes array, edges array), 32-bit indices, small cost
  types; a large tile's network is ~1–2 MB and stays in L2. Number nodes in Z-order/Hilbert order. Hot/cold split.
  Per-worker search state with a generation counter (no clearing); flat-array heap or bucket queue. Batch route
  requests by origin. Rebuild only the affected part when the network changes, in the Apply step.
  **Must give identical routes: same costs, same number types, same tie-breaking.**
- **Grid maps**: process in L1-sized tiles; fuse passes where formulas allow; keep the original's cell widths; NEON on top.
- **False sharing**: align worker slices and pad per-thread data to **128 bytes** (not 64).

### 12.3 Memory (target: biggest city under ~3 GB on an 8 GB M1; measure the original under Rosetta first as the baseline)
- Track **physical footprint** per subsystem with a debug overlay; a test fails if the reference big city exceeds budget.
- **Memory-map** the `.dat` packages (clean, evictable pages). Decompressed resource cache with a byte budget and LRU.
- **Textures**: load only the current rotation and nearby zoom levels (SC4 stores 5 zooms × 4 rotations); keep them
  BC-compressed; free the CPU copy after upload (unified memory); cached-but-invisible textures in a **purgeable**
  Metal heap.
- **Pools** per object type and **per-tick arenas** (SC4's GZ allocators are the place to do this).
- In the fast layer: 32-bit handles instead of pointers, packed flags, padding-free structs pinned by `static_assert`;
  trees/flora as compact instances; shared exemplar data; interned strings.
- Stream saves to disk; never build a full in-memory copy.
- Memory-pressure handler (dispatch memory-pressure source): shrink cache → drop distant zooms / top mips → drop
  thumbnails. Degrade, never swap.
- Scale cache budgets with installed RAM (`hw.memsize`): 8 GB lean; 16 GB all zooms + neighbour cities; 32 GB+ HD
  textures and the full region cached.
- Leaks: Instruments Leaks / `leaks` after loading and closing several cities; footprint must return to baseline.
  Watch refcount cycles.
- After any allocator change, run the full scenario under ASan: new allocators expose use-after-free bugs the
  original got away with. Record those as divergences.

---

## 13. Rules that do not bend
- Faithful classes stay faithful; optimisation only in the separate layer, only after M6, only through the
  determinism gate.
- Shipping render path: native Metal only (section 9).
- No stubs that pretend to succeed. No `recovered` without verification against the binary.
- Stop and ask before: rewriting another agent's files, changing a header included by many classes without running the
  full suite, sudo, downloads over 500 MB, deleting anything.

---

## 14. Report at every stop
1. Milestone reached (M1–M8) and the first failing step of the scenario.
2. Top 10 unwired sites before → after.
3. Rows closed, rows downgraded, false positives found (with addresses).
4. **On the path you touched: remaining stubs, fakes, partials and unwired seams — listed, not summarised.**
5. Next 3 targets and why.
