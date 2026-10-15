# Start-here prompt for the next agent

Paste this as your first message / system prompt:

> You are continuing a hand-written clean C++20 decompilation of SimCity 4 (Aspyr macOS/Steam v1.2.1, x86_64)
> in /Users/fernandojosesampaio/Downloads/SC4-Decomp. Read AGENT_RULES.md, notes/progress.md and `git log -20`
> first. Do not stop after one class: loop (pick the next class -> export -> implement -> test -> ledger ->
> commit) until told to stop, and give a short progress report each time you stop.

## Rules (summary; full text in AGENT_RULES.md)
- Hand-write every function, unique descriptive names, record in notes/ledger.csv; zero duplicates.
- Commit locally after each class, never push. End messages with
  `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`.
- Steam folder is read-only; never commit game binaries, assets or Ghidra DBs.
- Ask before sudo, downloads >500 MB, or touching anything outside SC4-Decomp / Homebrew.
- Never edit scripts/run_tests.sh while it runs. Heap-allocate COM-style objects in tests.
- Environment: arm64 macOS, zsh, CLT only (no `timeout`, `sed -i ''`), tests use ASan/UBSan.

## Per-class loop (the "skill")
1. `python3 scripts/export_class.py <gz|sc4> '^cSC4Name$'` -> read `work/<group>/cSC4Name.txt`;
   `scripts/vtable.py`, `scripts/dec_addrs.py <addr>` for slots/single functions.
2. Write `src/<group>/Name.{h,cpp}` in the style of neighbours; replace `SL::sp*` globals with small host
   interfaces; keep original quirks, comment them.
3. Test in `scripts/tests/` + entry in `scripts/run_tests.sh`; prefer oracle (Ghidra output -> C, compare on
   random inputs: scripts/run_bit1_oracle.sh) or real game data.
4. `python3 scripts/ledger_class.py --class X --source 'src/..|h' --evidence '...' [--partial 0xADDR]`
   (free functions: `scripts/ledger_add.py addr:UniqueName`), then `python3 scripts/check_unique_names.py`.
5. Commit; periodically `python3 scripts/progress.py`.

## Current state / next targets
- Status at handoff: 10,146 / 28,371 game functions recovered (35.76%); 89/89 tests pass
  (run via a repo-local snapshot copy of scripts/run_tests.sh, delete it afterwards; full run takes ~5 min).
- Shortcuts: `/tmp/show.py` and `/tmp/first.py` were session-temporary helpers (print decompile blocks by
  address / first body of each function from work/<g>/<Class>.txt); recreate if needed. Disassembly for ambiguous
  float/SIMD code: `otool -arch x86_64 -tV <game binary>`. zsh does not word-split `$var`; use `${=var}`.
- Seams used: Lua C API (`src/lua/Lua5Thread.h` ILuaApi, test fake `scripts/tests/lua_minilua.h`), protection
  simulators (IProtectionHost), occupants over `cSC4Occupant` (`src/sc4/Occupant.h`), stream helpers in
  `gz/StreamOps.h`, `SC4Read/WriteGZSerializable` (needs a DB segment stream; plain memory streams skip nested objects).
- Done this session: Lua stack (cGZLua5Thread, cGZLua5, thread registry/coroutine lib, Lua4 compat, cSCLua),
  cSC4FireProtectionSimulator, Foundation/Landfill/PowerLine/Toxic occupants, cGZAlphaBlt, cSC4RegionalTerrainMap,
  cRZRefCount, cGZApp, cSC4AdviceSubject.
- Partials left: cSCLua Game Data Registry bridge (needs cISC4GameDataRegistry), Fire sim Init/streams/DoMessage,
  cGZLua5::default_error_reporter (output compiled out), nsGZLua5ThreadLib needs the patched Lua (state[-1] slot).
- Skipped for missing bases: cSC4SeaportDeveloper (cSC4PortDeveloper/cPortLot), cSC4DisasterManagerFire
  (cSC4DisasterManager), Pipe/Subway/UndergroundConnection (cSC4UndergroundOccupant), NetworkTunnelOccupant
  (cSC4NetworkOccupantWithPreBuiltModel). Recovering those bases unlocks ~300 functions.
- Good next targets (by remaining count): cGZWin* UI family (TextEdit 235, Grid 210, cGZWin 183, ListBox 165),
  cSC4App 223, cSC4MySim 211, cSC4Automaton 189, cSC4NetworkTool 196, cSC4FireOccupant 61, cSC4TextDecalOccupant 69,
  cSC4TrafficNetworkMap 97, cSC4BudgetSimulator 87, cGZDBSegmentDirectoryFiles 88, free utilities (5.7k "(free)").
- Playable client: src/app/main.mm (Metal, scripts/build_app.sh). Still missing: zoning/placement, click-to-inspect,
  simulators driving growth. Skip DX7 emulation per notes/graphics_scope.md.
