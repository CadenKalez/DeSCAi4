# Session handoff — 2026-10-29 (ledger honesty pass)

Everything below is committed except the last batch noted under "Uncommitted". Nothing was pushed. The full suite
(`python3 scripts/run_tests_fast.py`) passed 437/437 at the last full run. Other agents share this checkout: files such as
`src/app/NativeAutomata*`, `src/app/NativeAutomataManagers.*`, `src/app/NativeTrafficHosts.cpp`, `src/sc4/AutomataManager.*`,
`src/tools/city_probe_creators5/7.cpp` and `scripts/native_game_link_list.txt` belong to someone else's work: never `git add -A`,
commit exact paths.

## Why this session happened

The user asked whether the earlier recovered functions are byte exact. They were verified by unit tests (85 % of rows), not
proven. Two audits followed, and both found rows that claimed more than the code delivered.

## Where the numbers are now

```
game code to recover   26,880
recovered              22,241  (82.74 %)      remaining 4,639
ledger rows            recovered 26,379 · recovered-partial 1,727 · partial 129 · pending 21 · skipped 392
exactness worklist     4,348 functions with decompiler artifacts and only a unit test (notes/exactness_audit.md)
```

## What was found and fixed

1. **Exactness re-check of float / SSE functions** (against the disassembly, `scripts/disfn.py`):
   - `GetMaxCapacity` returns a float (the stored value truncated to int, converted back); the port returned a `uint32`.
   - `DispatchToolCanDispatch` is `GetAltitude(x, z) > GetSeaLevel()`; the port called an invented "cell free" host method.
   - `cSC4PropDeveloper::Write` skipped three service references on a host-less stream while `Read` expected them.
   - `PlumbingSimulator::Write` did not emit the scanning iterator in the original's position.
   - Ten unit tests no longer linked (nobody noticed, a test that does not link never runs); link lines fixed.
   - Verified, no change: `CheckRailAccident`, `GetEfficiency`, `eatGarbage`, `IsAreaFlat`, `SimulateCellTemperature`.
2. **Ledger rows marked `recovered` with no code** (scan: for every recovered row look for the function name, the port name in
   column 3, and the address in a comment). Details and the verdict per group are in `notes/ledger_missing_code.md`.
   - Recovered now, with tests: `cS3DTransform::Rotate` (half angle × -0.5, rotation × R, summation order from the disassembly),
     22 `BuildingDevelopmentSimulator` functions (placement, foundations, developers, decline plan, type filter), 14
     `PowerSimulator` getters, Police / Pollution / OccupantManager / PropManager rows (39), and about 25 small rows (loaders,
     setters, key-list callback, FSH current frame, Gimex dither, property-set SAX no-ops, park lot lookup, ...).
   - Restored as correct (code exists under another name or via macro): 54 rows (`cRZVariant` accessors from the `RZV_*`
     macros, `GZSndMgr` overloads, button/slider paint, index generators, ...). **Lesson: search the address in comments and
     macro-generated names before declaring a row missing.**
   - Kept `recovered` on purpose: 76 `cRegk*` render-property constructors (one table), 45 `cSC4City` getters (enum-keyed
     accessor), 24 `GZCOM_Interface<>` and 19 `ProcessTwoGridsWithCallback<>` template instances.

## Known quirks kept on purpose

- `SetOccupantListUsedCapacity` re-spreads the remaining amount **before** subtracting the current occupant's share, so the
  unsigned remainder can wrap (test pins it).
- `AddNewBuildingOccupant(type, pos, rot)` treats `pos[1] < 0` as "use the surface under the footprint".
- `ProtectionProcessCell` is a no-op in the original.

## Open work, highest value first

1. **`UpdatePollutionVisualEffect` (0x1000986d8)** — still `pending`; `AddRadiationToCity` is `recovered-partial` until it
   exists (it runs through `SetVisualEffectUpdate`). About 350 lines of per-8x8-block particle effects; needs the effects
   manager seams.
2. **No native host implements the new seams yet** (`IBuildingPlacementServices`, `IPollutionServices`, `IGrowthDeveloperRef`,
   `ITractDeveloperRef`, `IParkEnvironment::LotWhereOccupantResides`, `IOrdinanceHost::ForEachOrdinanceKey`). Until
   `IBuildingPlacementServices` has a native implementation, growth cannot place buildings in the running game.
3. **Remaining exactness worklist** — about 140 float/SSE functions first (`notes/exactness_audit.md`), gameplay ones first:
   `CalculateRoadAccess`, `DoDemandIndexLinks`, `CivicBuildingSimulator::DoMessage`, traffic automaton ticks, vehicles,
   `LightingManager`. `ClampScrollToBounds` and `GetCenterLane` were read and matched but not finished. There is now a Rosetta
   machine-code oracle (`scripts/run_machine_oracle.sh`, see the memory note "machine-code oracle") that runs retail code
   against the port bit for bit; prefer it over reading Ghidra's float order, which is not trustworthy.
4. **Earlier open gaps** (unchanged): `PlumbingSimulator` Init/DoIdle (flood fill), `StubSimulator` not replaced so the city
   clock does not run, native growth host `BuildingFitsInZone` stub returns false, roads (~396) and city UI (~378) classes.

## Tooling notes

- `scripts/ledger_promote.py --source S --evidence E [--status recovered-partial] 0xaddr ...` edits ledger rows line by line
  (the file mixes CRLF and LF: never rewrite it with a csv writer).
- After ledger edits: `python3 scripts/gen_remaining.py`, `python3 scripts/verify_class_done.py --fix`,
  `python3 scripts/audit_exactness.py`.
- One test: `zsh scripts/runone.sh <name-as-in-run_tests.sh>` (names are the registered ones, e.g. `propertyset_io`).
- `scripts/disfn.py` needs `$TMPDIR/sc4.asm` from `otool -arch x86_64 -tV "game-binaries/MacOS/Sim City 4 Deluxe Editionsub"`.
- A Ghidra headless MCP server may still be running on 127.0.0.1:8089 (`scripts/ghidra-mcp-headless.sh`).
- The OpenSC4 specs (`~/Downloads/OpenSC4-main`, GPL-3) are a reading aid only; nothing of theirs is copied into `src/`.

## Uncommitted

(none: everything is committed.)

## Addendum (same day, later)

- `cSTEVolcanoLava` flow core recovered from the disassembly (`src/sc4/VolcanoLava.*`, test `sc4_volcanolava`); drawing, steam,
  decals and burning stay behind `ILavaHost` (37 functions left in that class).
- `cSC4LotDeveloper`: the lot wrappers (`IsLotDeveloped`, `IsLotFlat`, `GetLotAltitudeRange`, `GetLotSlope`,
  `GetOccupantSlope`) and the `TestCondition` gate are recovered. 58 absent `LotDeveloper` rows remain: the other Test* gates
  (`TestPower`, `TestWater`, `TestDesirability`, `TestTravel`, `TestCoffeeBar`), foundations, base textures, props, business-unit
  joining (`PickUnitToJoin`, `UpdateTravelDesignates`, `UpdateTravelPopulations`) and the construction / demolition functions.
- What actually blocks the remaining partials, by kind: (1) lifecycle rows (`Init`, `Shutdown`, `Read`, `Write`, `DoMessage` of the
  Protection / Police / Power / FireProtection simulators) are partial only because the `SL::sp*` services behind them are host seams,
  so they close when the native hosts exist, not by more decompiling; (2) rows that need an adapter between the port's concrete
  lot and the business unit's `IBusinessLot`; (3) rows that need unrecovered neighbours (lot manager queries, the lot
  configuration, growth developers).
- Next in `LotDeveloper`: add an `IBusinessLot` adapter on `cSC4Lot`, then `UpdateTravelPopulations`, `RemoveLotFromItsBusinessUnit`,
  `PickUnitToJoin`; then resolve the growth-developer slot 0xa0 and lot-configuration slot 0x58 that `TestWater` / `TestPower` call.
