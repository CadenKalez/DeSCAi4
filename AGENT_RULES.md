# SC4-Decomp — rules for any agent working on this project

## Goal
Decompile SimCity 4 (Aspyr macOS/Steam v1.2.1, x86_64) with Ghidra (ghidra-mcp at http://127.0.0.1:8089) into
hand-written, clean C++20, one function at a time, and keep the game optimized, with audio that works without bugs
and native Apple Metal rendering (no OpenGL). The user's standing instruction is "just decompile the game" — keep
going, and give a progress report whenever you stop.

## Hard rules
- Hand-write every function. Names must be unique and descriptive (never `sub_XXXX`); record each in `notes/ledger.csv`.
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
5. Commit. Regenerate `notes/progress.md` with `python3 scripts/progress.py` periodically.

## Environment notes
- arm64 macOS, zsh, Command Line Tools only: no `timeout`; chained `sleep` is blocked (use until-loops).
- `sed -i` needs `-i ''` on macOS. Tests build with ASan/UBSan (`-fsanitize=address,undefined`).
- Stack-allocated COM-style objects must not be `Release()`d to zero — heap-allocate them in tests.

## Next targets (largest unrecovered game-logic classes)
cSC4TrafficSimulator, cSC4LotDeveloper, cSC4PollutionSimulator, cSC4PoliceSimulator, cSC4ResidentialSimulator,
growth planner of cSC4BuildingDevelopmentSimulator, cSC4ZoneDeveloper, cSC4PowerPoleOccupant, cGZWin* UI family,
cSCLua, remaining free utilities (SC4 helper functions, GZCOM factories, UTF16/Thai line-break tables).
