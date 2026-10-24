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
