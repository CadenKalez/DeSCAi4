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
- Done: FloraSimulator, LotDeveloper, PollutionSimulator. Next targets below (skip these).
  FloraOccupant.h: halfExtent +0x38, height +0x44).
- Next: cSC4TrafficSimulator, cSC4PoliceSimulator,
  cSC4ResidentialSimulator, growth planner of cSC4BuildingDevelopmentSimulator, cSC4ZoneDeveloper,
  cSC4PowerPoleOccupant, cGZWin* UI family, cSCLua, free utilities.
- Partials in ledger: network occupant Draw/Pick/GetPathInfo family, PropManager::DoMessage.
- Playable client: src/app/main.mm (Metal, build with scripts/build_app.sh, `build/sc4 [--screenshot f.png]`).
  Missing for real gameplay: zoning/placement, click-to-inspect, simulators driving growth.
- Status at handoff: 7,342 / 28,371 game functions recovered (25.9%).
