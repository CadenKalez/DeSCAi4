# Session handoff — 2026-10-23

Everything in this file is committed. `git status` is clean, `python3 scripts/run_tests_fast.py` reports **430 passed,
0 failed**, and `build/sc4_host` plus `build/sc4` both build.

## Where the project stands

```
necessary for macOS   22,107 / 26,597 = 83.12%
all game functions    26,880 total, 26,215 ledger rows recovered
ledger                1,731 recovered-partial · 136 partial · 24 pending · 392 skipped
```

## What landed this session

| Work | Result |
|---|---|
| `cSTETerrain` | **complete** — 213/217, the 4 left are compiler destructor aliases. Both brush layouts decoded (`tBrushParameters` 0x38, `tBrushContext` 0x38) and pinned with `static_assert`s; the whole terraforming path is in |
| `cSC4View3DWin::AddGUI` | **recovered** (0x1007892fc, 5,411 bytes) — the function that builds all 42 game windows; `RemoveGUI` too |
| Audio | **complete end to end** — region screen posts `0x2a3ad653`/`Data1=1` → `cSC4AudioEventHandler` → `SetStation("krgn", fade)` → `cRZDiskRadioStation` plays `Radio/Stations/Region/Music/*.mp3` |
| `cSC4Nation` | 29 — it was being **faked** at boot |
| `cHitResource` templates | **88 rows**, all partials closed |
| `cS3DModelProcessor` | 11 — the automaton's model/vertex-array layer |
| `cSC4LocalGridHelper`, `cSC4LotRetainingWallModel`, `cSC4WinAdviceList`, `cSC4EffectsManager` (2), `cSC4TrafficSimulator` (13), `cSC4AutomataController`, `cRZRadio`'s caller chain | each with tests |
| GZCOM | **41 class creators wired** (one TU per class) + `SetCityHost` |

## The two fixes that moved the boot

1. **`cSC4App::LoadCity` never loaded the file's class table.** The serializer's `classes_` is filled by
   `LoadClassObjects`, and with it empty `OnGetGZSerializable` answered *false for every class id*. The record had been
   parsing correctly all along — `id`, then class `0x8990c372` (`cSC4City`) — and then nothing was created. It now loads
   the table after the version check.
2. **`cSC4AutoLoadStream` never published its record version.** The serializer read a null user data as "current
   format" and parsed ids 64-bit where a version-62 file stores them 32-bit. Now published as user data, which is what
   the save side already did.

`LoadCity` now reaches component creation. It still fails, and the reason is the probe's link list, not decompilation.

## What is NOT done — read this before planning

- **1,867 partial rows** (1,731 recovered-partial + 136 partial) and **24 pending**.
- **392 skipped**, of which 297 are genuinely not code (182 thunks, 73 libc++ instances, 41 dtor aliases, 1 init) and
  **95 are "not needed on macOS" judgement calls** — `cSC4View3DWin` 35, `cGZWinFileBrowser` 15,
  `cSC4WinLotConfigurationEditor` 15, `cSC4PathFinder` 6, `cSC4EffectsManager` 6. Writing "not needed on macOS" is a
  scope decision, not a recovery. Revisit these if the archive must be complete.
- **Game core by group (recovered only):** app/city/terrain/render 89.7 %, simulators 79.2 %, occupants 77.5 %,
  automata 74.2 %, developers 71.4 %.
- **The game still does not load a city**, and there is **no host audio device** — every line that decides the region
  music is recovered, but nothing opens a speaker.

## The boot loop is written but not linked

`src/experimental_host/pending/` holds the game loop (`HostGame.cpp`) and the `IWinSC4AppHost` adapter
(`HostWinSC4App.h`). They are parked on purpose: linking pulls in `cWinSC4App` → `cSC4View3DWin`, which was being
edited at the time. `pending/README.md` lists the four steps to switch it on. `AddGUI` is done now, so this is unblocked.

## Highest-value next moves, in order

1. **Finish `LoadCity`** — link the component creators into `city_probe` (it needs every `gzcom_creators/*.cpp` plus the
   sources they pull: `RegionalCity.cpp`, `cGZLuaScriptServer.cpp`, …). This is mechanical and it is the gate to a city.
2. **Link the game loop** from `pending/` and run `--play N`.
3. **`cSC4LoadExemplarString` (0x100121132)** — unrecovered, and it blocks advisor/catalog/text rows across several
   classes (including the advice-list follow-ups).
4. **`cISC4GameDataRegistry`** — blocks the 7 automaton rows downgraded to pending plus the `cSCLua` registry bridge.
5. **Finish the partials with named operands** rather than new classes: `cSC4TrafficSimulator` (74, each with its missing
   operand listed in `notes/ledger_applied/cSC4TrafficSimulator.txt`), `cSC4Automaton` (its model/anim layer).

## Working notes that will save you time

- **Ghidra MCP (127.0.0.1:8089) is unreliable** — it returned 404 for most of this session. `otool -arch x86_64 -tV` on
  `game-binaries/MacOS/Sim City 4 Deluxe Editionsub` (via `scripts/disfn.py`) is the authority and was used for nearly
  everything here.
- **The real game data** is at `~/Library/Application Support/Steam/steamapps/common/SimCity 4 Deluxe/Sim City 4 Deluxe
  Edition.app/Contents/GameData` — not `~/Applications/SimCity 4 Deluxe.app`, which is an empty bundle.
- **`ledger_upgrade.py` had a bug that blocked the whole closing pass**: it skipped any status starting with
  `"recovered"`, which includes `recovered-partial`. Fixed. If partials are not closing, check that first.
- **Never `git checkout -- src/` or `git add -A src/` in this shared checkout.** I did both and destroyed an agent's
  uncommitted terrain work once. Name exact paths. The rule is in `AGENT_RULES.md`.
- **`apply_agent_results.py` cannot refresh the evidence of an already-partial row**, and it ignores `SKIPPED:` sections
  for rows that exist. Those need `ledger_upgrade.py` / `ledger_class.py` or a manual edit.
- **Verify against the binary, not against the existing code.** Three `cSTETerrain` functions were marked recovered and
  were wrong (with tests that agreed with the bugs), and 7 automaton rows claimed bodies that did not exist anywhere in
  `src/`. A test written from the same misreading as the code proves nothing.


## Addendum 2026-10-29 (WIRE_THE_GAME.md campaign, first stretch)

**Milestone reached: M2 partly.** `SC4_LOADCITY=<file> build/sc4_game` now reads every class object of a retail-style developed city
(`Regions/Berlin/City - Konradshohe.sc4`, 1.5 MB) and of the "New City" saves; before, both failed in the traffic simulator, the occupant
manager, the traffic network map and the vehicle manager. First failing step of the scenario: the city loads, then **4 classes are still
missing** (`cSTESurfaceWater` 0x0b8da7db, `cSC4MySimAgentSimulator` 0x29e37dec: creators return null; `cSC4EffectsManager` and
`cSC4DisasterLayer`: "no cIGZSerializable"). The game loop in `src/experimental_host/pending/` is still not linked.

**Phase 0 done:** `src/platform/Unwired.h` (`SC4_UNWIRED("what")`, header-only, logs once to stderr, writes `work/unwired.log` or
`$SC4_UNWIRED_LOG` at exit), `scripts/unwired_report.py` ranks the log, every null `GZCOM_Create*` fallback and the new emergency-vehicle
creators call it. Not done: the `--script scenarios/first_city.txt` mode, `--threads=1`, Phase 1 audit (`audit_bodies.py`).

**Root causes found:** `cSC4TrafficSimulator::Read` accepted only versions >= 0x1d and had the fourth 16-bit key set modelled as "one extra
u32"; both fixed from the decompile (versions 0x13..0x1c: old float density rows, legacy rule list, sections gated at 0x15/0x16/0x1b/0x1e).
Cities saved by this Mac build carry version 0x13. The real cause of the other three failures was class 0x4a222b98 (patrol car) missing from
the creators.

**Recovered this stretch:** `cSC4PatrolCar` (33 rows), `cSC4FireTruck` (37 rows), both behind new seams (`IPatrolCarHost`,
`IFireTruckHost`); creators for patrol car / fire truck / ambulance in `src/tools/city_probe_creators8.cpp` (registered in
`NativeCity.cpp`). They are created without hosts: sirens, routes, crews, the police/fire/crime simulators do nothing yet (logged as
`UNWIRED: IPatrolCarHost ...`).

**Not done on the path (listed):**
- unwired seams: `IEmergencyVehicleHost`, `IPatrolCarHost`, `IFireTruckHost`, the riot-police / fire-crew automata (`cSC4RiotPolice` 18
  functions, `cSC4SimFireCrew` 26, both unrecovered), plus everything in `docs/SEAMS.md` without a native adapter.
- `cSTESurfaceWater` (35 functions; read/write/init layout decoded in the session notes below), `cSC4MySimAgentSimulator` (28),
  `cSC4EffectsManager`/`cSC4DisasterLayer` (QueryInterface for `cIGZSerializable` missing).
- other agents' uncommitted work (do not commit for them): `NativeAutomata*.{cpp,h}`, `NativeAutomataManagers.*`, `NativeTrafficHosts.cpp`,
  `AutomataManager.*`, `city_probe_creators5/7.cpp`; `scripts/native_game_link_list.txt` has their two lines uncommitted (my line for
  `city_probe_creators8.cpp` is committed through `git update-index`).

**Decoded for the next agent (cSTESurfaceWater, class id 0x0b8da7db, iid 0xab8da759/0x452294aa, 0xa8 bytes):** `+0x18` initialised flag,
`+0x1c` refcount, `+0x20` counted cSTETerrain, `+0x28` vector of tWaterInfo (0x24 bytes: depth float, 3 floats, cS3DVector3 at +0x10,
float at +0x1c), `+0x58` std::list<int>, `+0x70/0x74` flags, `+0x78` cRZCellMap<bool>, `+0x80` vector<float>, `+0x98` cSC4SimGrid<u16>
(class 0x49b9e604, 0x4be3618b). Write: u16 version 1, terrain serializable, then `SaveWaterInfo` (autosave stream 0x8a91e7ef/0xe98f9525,
version 4: vertex count, then per vertex 4 floats, a cS3DVector3, 1 float). Read accepts version < 2. Messages registered:
0x26d31ec1/4/5, 0x89d3e9f3, 0x99ef1142/3. `IsNavigable` is a spiral search over city cells using six services.
