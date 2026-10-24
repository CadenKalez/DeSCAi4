# SC4-Decomp — rules for any agent working on this project

## Goal
Decompile SimCity 4 (Aspyr macOS/Steam v1.2.1, x86_64) with Ghidra (ghidra-mcp at http://127.0.0.1:8089) into
hand-written, clean C++20, one function at a time, and keep the game optimized, with audio that works without bugs
and native Apple Metal rendering (no OpenGL). The user's standing instruction is "just decompile the game" — keep
going, and give a progress report whenever you stop.

## Hard rules
- Hand-write every function. Names must be unique **within their class** and descriptive (never `sub_XXXX`); record each in
  `notes/ledger.csv`. `scripts/check_unique_names.py` enforces the per-class rule; new rows conventionally prefix the class
  name (`cGZWinGridPaint`) so a name also reads unambiguously across the ledger.
- Commit locally and automatically; **never push**. End commit messages with
  `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`.
- The Steam folder is read-only. Game binaries, assets and Ghidra databases are never committed.
- Ask before: sudo, downloads over 500 MB, or anything outside SC4-Decomp or Homebrew.
- Do not edit `scripts/run_tests.sh` while it is running (bash reads it incrementally).

## Workflow per class
1. `python3 scripts/export_class.py <gz|sc4> '^Class$'` then read `work/<group>/Class.txt`; use
   `scripts/vtable.py` / `scripts/dec_addrs.py <addr>` for slots and single functions.
2. Implement header + cpp matching the surrounding code style. Replace `SL::sp*` globals with small host
   interfaces (seams). Keep original quirks and note them in comments.
3. Add a test in `scripts/tests/` and an entry in `scripts/run_tests.sh`. Prefer a decompiled-code oracle
   (Ghidra output turned into compilable C and compared on random inputs, see `scripts/run_bit1_oracle.sh`)
   or real game data over assumptions about behaviour.
4. Ledger: `python3 scripts/ledger_class.py --class X --source 'src/..|h' --evidence '...' [--partial 0xADDR]`
   (free functions: `scripts/ledger_add.py`). Run `python3 scripts/check_unique_names.py` (must show 0 duplicates).
5. Register what is left, then verify (see "Definition of done" below), then commit.

## Definition of done (every class, no exceptions)
A class is not done when its functions compile. It is done when the registries say so, and this is checked by a script:
1. **Honest ledger**: functions that depend on an unrecovered neighbour are `recovered-partial` (`ledger_class.py --partial 0xADDR,...`),
   never `recovered`. Every ledger row's evidence column names how it was verified, and that evidence must resolve:
   - a unit test in `scripts/tests/` that `scripts/run_tests.sh` runs (matched **by file**, whatever the entry is called), or
   - an oracle / real-data script under `scripts/` (`run_bit1_oracle.sh`, `run_simgl_oracle.sh`, the `s3d_realdata_test` tool…),
     in which case the evidence says so ("oracle", "real-data tool").
   `verify_class_done.py` accepts either and fails when a named file does not exist or a named unit test is unregistered.
   The `source` column is always `src/<name>.cpp|h` (or just one of them); `scripts/normalize_ledger_sources.py` rewrites the
   historical free-text variants, and `scripts/list_unledgered_sources.py` reports any `src/` file no row cites.
2. **Seams**: every place the original reached an unrecovered class goes through an `I*Host` / `I*Services` / `I*Api` interface that
   mirrors one original call (put the vtable slot or address in a comment). Never read an engine global directly. Run
   `python3 scripts/list_seams.py`; add a row for new seams to the hook-up table in `docs/SEAMS.md` (original call, real class to hook up).
3. **To-do rows**: for each follow-up (a neighbour to recover, a seam to hook up, something to verify against the binary, a quirk you
   kept, a value you had to assume) run `python3 scripts/todo.py add ID Class "task" --blocked-by ID --where path`. Replace every
   `TODO` / `FIXME` / "not recovered yet" comment in the source with a to-do row id (`// see TERRAIN-VIEW-4`). Close rows you finished with `todo.py done ID`.
4. **Quirks**: original bugs you kept on purpose are listed in `docs/SEAMS.md` ("Known quirks kept") and asserted in the test, with the address.
5. **Verify**: `python3 scripts/verify_class_done.py ClassA ClassB` (the classes you touched). It fails if the ledger has duplicates, a test is
   unregistered, partial functions have no to-do row, a TODO marker has no to-do row, a seam is missing from `docs/SEAMS.md`, or the generated
   files (`notes/todo.csv`, `docs/SEAMS.md`, `notes/progress.md`, README meter) are stale. `--fix` regenerates the generated files; the rest is manual.
   Do not commit a finished class until it prints `ok`.
6. **Report**: end the turn with what is left in the registry for the classes you touched (`todo.py list --class X`) and what is ready next
   (`todo.py list --ready`).

## Environment notes
- arm64 macOS, zsh, Command Line Tools only: no `timeout`; chained `sleep` is blocked (use until-loops).
- `sed -i` needs `-i ''` on macOS. Tests build with ASan/UBSan (`-fsanitize=address,undefined`).
- Stack-allocated COM-style objects must not be `Release()`d to zero — heap-allocate them in tests.

## Next targets (largest unrecovered game-logic classes)
cSC4TrafficSimulator, cSC4PoliceSimulator, cSC4ResidentialSimulator,
growth planner of cSC4BuildingDevelopmentSimulator, cSC4ZoneDeveloper, cSC4PowerPoleOccupant, cGZWin* UI family,
cSCLua, remaining free utilities (SC4 helper functions, GZCOM factories, UTF16/Thai line-break tables).

## Shared-checkout guards (added 2026-10-23 after lost ledger updates)
Several AI tools (Claude sessions, DeepSeek, background agents) commit to this one checkout. To keep the bookkeeping intact:
- **One writer for shared files.** `notes/ledger.csv`, `notes/todo.csv`, `scripts/run_tests.sh` and `docs/SEAMS.md` are
  changed only through the scripts (`apply_agent_results.py`, `ledger_class.py`, `todo.py`, `add_test.py`), never by
  rewriting the whole file from an old copy. Agents running in parallel use the pending files (`notes/ledger_pending/`,
  `notes/todo_pending/`, `scripts/tests/registry/`) and do not commit.
- **Hooks** (`git config core.hooksPath scripts/hooks`, already set in this checkout):
  `pre-commit` refuses a commit that shrinks `notes/ledger.csv` (a lost update; override with SC4_ALLOW_LEDGER_SHRINK=1
  when rows are removed on purpose), duplicate ledger names, or a malformed TESTS array (`scripts/check_test_registry.py`);
  `commit-msg` requires a `Co-Authored-By:` line naming the tool that wrote the commit (Claude, DeepSeek, ...).
- **Never revert or stage a whole source tree.** `git checkout -- src/`, `git add -A src/` and `git stash` reach into
  every other agent's uncommitted work: one `git checkout -- src/` on 2026-10-23 destroyed an agent's half-finished
  cSTETerrain part 5 and left HEAD's test uncompilable for an hour. Name the exact paths, both when staging and when
  reverting, and never assume a path is yours because you touched it last.
- **Do not reuse a file name for a different helper** (a test fake was overwritten this way), and run the full suite before
  committing a change to a header other classes include.
- **Tests:** `python3 scripts/run_tests_fast.py` is the incremental, parallel runner (object cache in $TMPDIR/sc4-obj,
  `--clean` to rebuild, test names to run a subset). `scripts/run_tests.sh` stays the from-scratch reference run (bash, not zsh).
- `SimCity 4.cfg` is written by the game and is not tracked.

## Architecture rules
See .claudecode (ECS, SIMD intrinsics with scalar fallback, cross-platform, profile first).

## Parallel-agent rules (learned the hard way)
A run with 20 parallel agents wasted a large share of the usage budget. Follow these before starting or briefing agents:
- **Check the ledger before assigning a class.** `NEXT_AGENT.md`'s "next targets" go stale. Verify with `notes/ledger.csv`
  (count recovered rows for the class's addresses; `scripts/class_range.py`, `scripts/progress.py`) that the class is really
  unrecovered. Five classes (cGZWinTextEdit, cGZWinListBox, cSC4FireOccupant, cSC4App, most of cSC4BudgetSimulator) were
  already done and their agents produced nothing.
- **Start small (3-5 agents), not 20**: one Ghidra tunnel and the weekly plan limit cannot feed more. Add agents as earlier
  ones finish and are merged.
- **Branch worktrees from the working branch**: harness worktrees start from `main`. Tell every agent to run
  `git merge <working-branch>` first (or create the worktrees from that branch) so they have the scripts and reference code.
- **Commit early and often (after each function group)**, not once per class: interrupting the lead session stops all
  background agents, and uncommitted work is lost or only rescued as patches (see `wip/`).
- **Tokens may be used in commands, but never stored in the repo.** Pass `GHIDRA_URL` / `GHIDRA_TOKEN` (and any API key)
  through the environment or as an inline `export` in a command when needed, but never write a token, API key, password or
  tunnel URL+token pair into any tracked file, commit message, test, script, note or patch (including `wip/` patches and
  `notes/`); the same goes for PR text and chat summaries. Prefer the environment secret (Authorization: Bearer header for
  `*.trycloudflare.com`, see `scripts/ghidra_tunnel.sh` / `scripts/ghidra_proxy.py`) so commands carry only `GHIDRA_URL`.
  If the permission check refuses a command that contains a credential, do not retry or work around it: ask the user. Before
  every commit run `git diff --cached | grep -iE "token|api[_-]?key|secret|bearer"` and fix anything that is a real value.
- **The lead merges, agents don't touch shared files**: agents leave `scripts/tests/registry/*.txt` and
  `notes/ledger_pending/*.txt`; the lead runs `python3 scripts/apply_agent_results.py`. Never `git add -A` over agent
  worktrees (`.claude/worktrees/` is gitignored).
- **Do not interrupt or reject the lead's tool calls casually while agents run**; it stops them.
- Scope: port only what the macOS build needs (skip DX7/Windows-only/debug/dead code, list under `SKIPPED:`).
