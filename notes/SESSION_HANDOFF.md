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


## Addendum 2 (same day): water first

`SC4_LOADCITY` on Konradshohe and both "New City" saves now ends with **0 classes missing**. Recovered: `cSTESurfaceWater`
(`src/sc4/SurfaceWater.*`, creator `gz/gzcom_creators/cSTESurfaceWater.cpp`, host `ISurfaceWaterHost`) and `cSC4MySimAgentSimulator`
(`src/sc4/MySimAgentSimulator.*`, host `IMySimAgentHost`). Both hosts are installed by `InstallTerrainStub()` in `src/app/NativeTerrainHost.cpp`.
The "no cIGZSerializable" lines for `cSC4EffectsManager` / `cSC4DisasterLayer` were noise: those classes answer only their own interface, as
in the original, and are made by the raw creator; the loader no longer lists them as missing.

**Unwired on this path (listed):** `cSTESurfaceWater` — `UpdateNeighborConnections` (0x10072efca), `UpdateFloodWaters` (0x10072fb5c),
`FindLocalMinimumOrWater` (0x1007303b0), `Rain` / `Rain_Static` / `GetBasinVertices` (0x100730820/882), tick registration (spSimulator
vslot 0x98 / ATM vslot 0x28: the water never animates), and the host answers for network occupants, lots and occupants (`IsNavigable`
therefore sees no bridges or lots). Divergence: the terrain model keeps channels 0 and 0x1c of every vertex in its own arrays; `SyncTerrain()`
copies them (land depth clamped to 0). `cSC4MySimAgentSimulator` — the Lua `get_sim` / `get_count` callbacks (`cISC4GameDataRegistry` not
recovered) and `RiotInProgress`. Patrol car / fire truck: no host.

**Next targets:** (1) the water simulation functions above (UpdateFloodWaters is ~2 KB of float code, FindLocalMinimumOrWater ~1.1 KB,
UpdateNeighborConnections ~1.4 KB: use `scripts/disasm.py`, test with the terrain fixture in `sc4_surfacewater_test.cpp`); (2) the game loop in
`src/experimental_host/pending/` (M1); (3) `cSC4RiotPolice` + `cSC4SimFireCrew` (44 functions) so the emergency vehicles' crews exist.

## Addendum 3: the water simulation

`cSTESurfaceWater` now has `UpdateFloodWaters`, `UpdateNeighborConnections`, `FindLocalMinimumOrWater`, `GetBasinVertices`, `Rain` / `Rain_Static` /
`AddRain` / `CreateSpring` / `EndBrushScrub`, all read from the disassembly (float order, the eight-direction table at 0x100a29ae0, the
constants). They are `recovered-partial / needs differential`: the test is written from the same reading, so it proves the algorithm hangs
together (a spring floods downhill and the buffers swap; a wide channel gets one neighbour connection per open edge), not bit-equality.
Next step for exactness: run the retail `UpdateFloodWaters` under the Rosetta oracle on a small synthetic terrain. The native terrain host
now routes `SurfaceWaterRain / AddRain / CreateSpring / EndBrushScrub` to the object and the water host reaches the neighbours simulator
(`NativeSurfaceWaterNeighbors.cpp`). Still unwired: tick registration (spSimulator vslot 0x98, ATM vslot 0x28), so `AnimateWater` is never
called; network / lot / occupant answers for `IsNavigable`. Quirks kept: the edge scan uses the x vertex count for all four edges and starts
the east / south edges at cell count - 1; `GetBasinVertices` does not put the seed in its visited set; the flood pass writes -0.0 into
empty buffers.

## Addendum 4: the game loop (M1)

Finding: the loop WIRE_THE_GAME.md asks to link already exists in the native app. `build/sc4_game` boots `cSC4App`, pumps both message servers
and `OnTick`, runs the splash -> region screen swap and opens cities through the recovered `cWinSC4App`; the `experimental_host/pending` files
are superseded (README says so). What was missing was the scripted, repeatable mode: `sc4_game --script FILE [--threads=1]` now runs a
scenario headless (`scenarios/region.txt`, `scenarios/first_city.txt`; commands frames / wait-region / wait-city / click / input / shot /
expect-region / expect-city / quit; exit 0 when every expectation held, 4 bad script, 5 failed expectation). `--threads=N` other than 1 is
refused (the reference mode is the only one). Milestone: **M1 reached** (region screen through the app's own screen chain, scripted);
**M2 reached for rendering** (a retail city loads and draws terrain, buildings, props). First failing step of the mission scenario:
**terraform** (no tool is driven yet; the scenario's tail lists the steps as TODO). Note `cSC4App::Run` itself is not recovered; the frame
loop is the port's own driver around the recovered tick, as the host owns the platform event pump.

## Queued by the user (2026-10-29, during the road-tool work)

"Later fix the renderer and the partials and stubs that it has." Not started. Scope when picked up: the Metal render path (`cMetalGDriver`,
`cSC43DRender`, the RENDER3D model queue, `SC4DrawContext`, lighting / shadows per RENDER_STATUS.md) and every stub, fake, recovered-partial
row and unwired seam on that path (use `scripts/unwired_report.py` after a `--script scenarios/first_city.txt` run, and the partial rows of the
render classes in `notes/ledger.csv`). Do it after the road tool lands.

## Addendum 5: renderer first (in progress)

Survey of the shipped render path (headless city scenario + `SC4_DEBUG` driver counters): `cMetalGDriver` reports only 2 unsupported calls ("Punt"); terrain, buildings, props, trees, lot textures, network tiles and cars already draw. What is visibly missing against the real game: **shadows** (no decal shadows under buildings / trees), the city's background picture (dark-blue backdrop), water ripples / waves (`cSTEWaterRipple` 28, `cSTEWaterWave` 4), particle effects (smoke, ~100 functions), foundations (`LoadFoundationModels`), dynamic textures. Biggest stubs: `cGZGraphicSystem` 201 partial rows (UI-side), `cGZDrawContext` 28, `cSC43DRender` 15 (picks, screen shake), `cSC4ModelMaker` 9 not recovered.

Done: `cSC4LightingManager::CalcShadowProjection` was a stub that refused every projection; it is now recovered and **bit-identical to the retail machine code** (`scripts/run_machine_oracle.sh lightingmanager`, 4000 random models, every transform flag combination; the harness runs the retail AlignDirToZWithYUp and operator*= unhooked). The oracle also found that the port's `nS3D::MultiplyTransform` added its terms left-nested; the machine code adds `c + (b + a)`; fixed (that function has other users, the suite still passes).

Next for shadows (the chain): `cSC4ModelMaker::CreateOccupantShadow` (0x100313a00, ~250 lines, SIMD-heavy: verify with the oracle) -> the model maker host's `AddShadowDecal` / `MapShadowTexture` stubs in `src/app/NativeModelMaker.cpp` (they return 0) -> `cSTEOverlayManager::AddShadowDecal` (recovered, needs the overlay manager wired in the terrain view: `GetOverlayManager` is skipped in `cSTETerrainView3D`). Then the background picture (`cSC4View3DWin::AddBackgroundImageToRenderer` 0x100787334 + `cSC4ModelBackgroundLoader` 25), then `cSTEWaterRipple`.

## Addendum 6: occupant shadows (commit 420e274)
- `cSC4ModelMaker::CreateOccupantShadow` (0x100313a00) now follows the disassembly (was an invented stub). New seams: `s3d::cIS3DResModel::GetShadowMeshes`
  (implemented in NativeModelMaker's ResModel), `IModelMakerHost::ShadowRenderQuality/ShadowMinimumHeight/ShadowViewScale/ShadowOverlay`
  (defaults make it a no-op) and `sc4::IShadowOverlay`.
- Still to do for shadows to be visible: implement `IShadowOverlay` over the native cSTEOverlayManager (AddShadow vslot 0x58 needs IOverlayTexture/
  IOverlayHost/IOverlayDrawContext wired in the terrain view) and return it, plus quality/min-height/view-scale, from the native ModelMakerHost.
- Rounding order of the 90-degree row fix-up is not oracle-checked (row is recovered-partial).

## Addendum 7: shadows debugged (2026-10-29)
The user reported the shadows did not render properly. Found and fixed on the path (each checked with screenshots, `SC4_ZOOM` / `SC4_FOCUS`
shots of the Making Money tutorial city, ~30 s per run):
- **Depth offset -2 instead of DrawShadows' +3**: the quads sat behind the terrain; at close zoom nothing drew.
- **Culling by the y = 0 ground-plane rectangle**: the terrain is ~300 m up, so at zoom 4 every shadow was culled. Now the terrain view's
  draw-frustum rows (`native::TerrainCullingInfo`, cSTETerrainView3D::GetCullingInfo), as SDSFindOverlaysToDraw reads them.
- **Shadows never removed**: the placeable host's `RemoveOverlay` / `SetOverlayVisible` were empty, every zoom change added a full new set
  (2.5k -> 5k). Now routed to the overlay manager (RemoveAll 0x100120bc2 / DoOverlayVisibilityChange 0x100120a16, vslots 0x78 / 0x80).
- **No soft-edge mask stage**: models whose texture is an atlas (most trees) cast the neighbouring pictures as extra shadows. Stage 1 now
  carries the 4x4 mask through `shadowMatrix` (vertex format 11, V3F_C4UB_2T2F).
- **Wrap mode**: GD wrap 2 is clamp and 3 repeat (nSGLDX7 ApplyState: 2 -> D3DTADDRESS_CLAMP, 3 -> WRAP); stage 0 now uses 3 as the binary.
- **UI turned white** when stage 1 was used: the pass left stage 1 current in the driver; it now ends with `SetDefaultRenderState()`.
- **Alpha test**: buildings with partial texture alpha (the coal plant) failed it because the port multiplies the strength into the vertex
  alpha; the reference is now `threshold * a * a`, which keeps the original's pass/fail set (`tex.a * mask.a >= threshold * a`).
- **Recovered DrawShadows (OverlayManager.cpp) had three transcription bugs** (false positive, ledger row 0x10072aee0 downgraded to
  recovered-partial / needs differential): the mask wrap went to stage 0, the single-stage branch used texMatrix instead of shadowMatrix,
  and `scale * falloff` was not stored into the colour's alpha before SetTexColor. Test updated (sc4_overlaymanager).
Dev switches in NativeShadowOverlay.cpp: `SC4_SHADOWDBG=1` (untextured red rectangles), `=2` (textured + masked, red, full strength),
`SC4_NOSHADOWMASK`, `SC4_SHADOWDEPTH=n`, `SC4_SHADOWLOG=tall|x,z` (per-mesh points / uv / rect). Build note: `native_autolink.py` links to
`$TMPDIR/native_link_probe` unless `NATIVE_OUT=$PWD/build/sc4_game` is set (build/sc4_game was stale before).
Not done / open: shadow RGB and blend follow the port's choice, not the original's combiner (see CENSUS); no shadows over water (the
water-pass manager is not created); a crash in the automata tick (`cSC4AutomataControllerManager::DoMessage` null controller, reached by
zooming in three times in a scenario) lives in another agent's uncommitted NativeAutomataManagers.cpp and was left alone.

## Addendum 8: the city simulation starts (2026-10-29/30)
Root causes found on the simulation path, all fixed:
- **No city component was ever initialised.** The original `cSC4City::Init` calls slot 0x18 (Init) / 0x20 (Shutdown) of every stored
  component directly; the port asked for `cIGZInitShutdown`, which no component answers. `ICityHost::InitComponent / ShutdownComponent`
  now carry the call; the native host dispatches by class with `dynamic_cast` (`src/app/NativeCityComponents.cpp`, one TU per class in
  `src/app/citycomp/`). ~30 components now initialise (simulator, history, surface water, script system, controller manager, budget,
  zone/lot/network managers, developers, most simulators). Left out: `kProbeHosted` (LandValueSimulator, TrafficNetwork: their hosts
  are still city-probe bindings, Init crashed) and `kInitByCityView` (Terrain, OccupantManager: initialised by the city view after its
  render stack; from the component pass the terrain drew black). Dev switches `SC4_COMPINIT_SKIP=a,b`, `SC4_COMPINIT_PROBES=1`.
- **The simulator never ticked**: its host was a generated stub (no message server, `RegisterService` dropped). `NativeSimulatorHost.cpp`
  is real; `TickSimulatorServices` runs from the frame loop.
- **City messages were dropped and would have dangled**: `PostMessage/SendMessage` of the native city host were empty, and
  `cSC4City` posted stack messages (the original heap-allocates them; fixed in cSC4City.cpp).
- **Every RZ timer was frozen**: `PumpTimers` stored wall-clock milliseconds (~1.7e12) into the 32-bit stub `g_ticks`; arm64 saturates the
  conversion to 0xffffffff. Now milliseconds since start-up, seeded in `main`. Consequence: the region map now needs a real double-click
  to open a city (the frozen clock made every click a double-click); `scenarios/first_city.txt` clicks twice.
- **cSC4AnimationTickManager (SL::ATM) recovered** (28 functions + creator; `src/sc4/AnimationTickManager.*`, test
  `sc4_animationtickmanager`). It is the region screen's "frame service" (EndFrame = DoAnimationTick); in a city the native frame loop
  ticks it (ATM-1). `NativeAnimationTicks.cpp` also offers it as a message server for the seams that model ATM that way (ATM-2).
- **Controller manager host** is native (`NativeControllerManagerHost.cpp`): city clock, script system, ATM ticks, view position; no
  controller creators yet (CTRL-1).
- `cSTESurfaceWater::Init` resets water to sea level after the read, as the binary does: valleys below sea level now fill.
Open, in order (todo.csv): SIM-HOST-1/2/3 (22 simulator hosts are still city-probe bindings, `src/tools/city_probe_bindings.cpp`),
SIM-INIT-1 (Init/Shutdown of power, residential, traffic, police, fire, pollution, flora, plumbing, lot developer, crime not recovered),
SIM-INIT-2, ATM-1/2, CTRL-1. UI partials surveyed: 422 recovered-partial rows in 38 UI classes; the big ones are cGZGraphicSystem (201,
driver side replaced by Metal), cSC4ViewInputControlQuery (36), cGZDrawContext (28), cGZWinCOMDirector (20), cSC4View3DWin (14,
including HandleButtonActivated's ~120-button switch only partly written).
Not touched: the other agent's NativeAutomata*/NativeTrafficHosts/AutomataManager work (uncommitted, still in the tree).

## Addendum 9: UI partials, where to start (2026-10-30)
User asked to recover all UI partials and resolve all stubs. Survey: 422 recovered-partial rows in 38 UI classes (largest: cGZGraphicSystem
201, driver side replaced by Metal; cSC4ViewInputControlQuery 36; cGZDrawContext 28; cGZWinCOMDirector 20; cSC4View3DWin 14) plus the
city menus, which are not partial but **skipped as "not needed on macOS"** (UI-MENU-1): every toolbar menu of the city view. First target.
`cSC4View3DWin::HandleButtonActivated` (0x10078afde) dispatches 86 button ids into ~45 blocks: 21 command-server commands (already in
kButtonCommands), DoLandscapeMenu (14 ids), DoTransportMenu (7), DoDisasterCopingMenu (7), DoZoningMenu (3), DoVehicleMenu (3),
ShowMayorSubPanel (3 + the 4 graph ids), 8 InvokeSecondaryMenu cases, InvokeTertiaryMenu 0xab9537b7 (+ toggle of 0xeb9541eb),
CreateCatalogView 0x69b9324a, SetWindowVisible 0x4bb92c20/21 (window 0xcc1a6983 / 0x10000005), 0x4bb92c1f / 0x2b709676 (vslot 0x70 with
0x1b), 0xea6fa936 (this vslot 0x230), and std::map inserts for 0x3 / 0x9930709 / 0x89dd5405 / 0x1501..0x1506. Re-derive with a pass over
`scripts/disfn.py 0x10078afde 2500` mapping `cmpl $id` + je/jne to target blocks.

## Addendum 10: crashes and bugs found while playing (2026-10-29, registered in notes/todo.csv)
- Fixed (250a5dd): zooming in a city with saved automata controllers (Big City Tutorial) crashed on null controllers the read could not
  create; the read now skips them (divergence until CTRL-1; follow-up CRASH-2).
- Open CRASH-1: mouse move in a city segfaults in cGZWin::DoMessage's filter loop (a freed message filter). An ASan game binary is built at
  build/sc4_game_asan (`NATIVE_DEBUG=1 NATIVE_OUT=$PWD/build/sc4_game_asan python3 scripts/native_autolink.py ...`); run it, repeat menu
  clicks / query hovering, and read the free site from the report.
- Open BUG-1 (region clicks hit different cities while the map eases), BUG-2 (panel toggle reaches the view, no panel appears), BUG-3
  (Rush Hour Tutorial still fails to load).

## Addendum 11: the city commands and the options bar (2026-10-30)
- `cSC4View3DWin::ProcessCommand` (0x10078e2e0) is fully transcribed (`ProcessViewCommand`), with `InvokeCatalogItemCommand`
  (0x10078410a) and `ObliterateCity` (0x10078fe78). Tools whose classes are not recovered are created through new
  `IView3DWinHost::Create*Tool` seams that report SC4_UNWIRED natively: network (TOOL-1), demolish (TOOL-2), obliterate (TOOL-3),
  dispatch / label / flora / intersection (TOOL-4). Place-lot and sign tools are created natively but have neutral hosts (TOOL-5).
- Speed / pause buttons work: `DoPostCityInit` now keeps SL::spSimulator at +0x238 (it was never set, so the buttons did nothing).
- The options bar ("..." at 213,740) works: `CreatePanelUIWinProc` returns the recovered `cSC4PanelUIWinProc`, so save / region /
  quit reach the app's Request* functions. Game Options opens as a real modal dialog (UI-PREFS-2: its lower pages draw blank).
- Modal dialogs run natively: `WinMgr::modalPump` is set in game_main.mm (interactive: the Cocoa pump; scenario: 30 frames, a shot
  `work/shots/modalN.png`, then cancelled). The UI-script service is now also a framework service (SC4CreateWindowFromScript found
  nothing before, so every recovered dialog failed to build).
- SAVE-1: the native save writes an incomplete city that does not load, and with no `createSavePathForCity` hook it deleted the
  city file from /tmp/sc4boot_docs. The hook is set now, and the native view host keeps the city save-disabled until saving is whole.
- Scenario command `where HEXID` prints a window's screen rectangle (to find buttons to `tap`).

## Addendum 12: the simulation runs, zoning makes real lots (2026-10-30)
- cSC4Simulator: the city-started message releases the hidden pause (vslot 0x68), so the date advances. Simulator agents register
  through `AddCitySimulatorAgent` (src/app/NativeSimulatorHost.cpp); never register one whose host is still neutral (the prop
  developer crashed on a null lot manager and stays unregistered).
- Zoning: `src/app/NativeZoneDeveloperHost.cpp` is the real IZoneDeveloperHost + ILotHost + the lot developer's base-texture
  services. Dragging a zone now parcellizes it into lots that draw the game's zone textures (cSC4LotDeveloper::SetEmptyLotBaseTexture
  and friends, newly recovered). The native flat-colour overlay only draws the drag preview now. Gaps between lots are the auto-street
  cells (the street needs cSC4NetworkTool, TOOL-1).
- Fixed porting errors in cSC4ZoneDeveloper::CreateLot / DeleteLot: building development vslot 0x120 is the lot developer (0x158 =
  SetEmptyLotBaseTexture, 0x148 = GetLotBaseTextureOccupant), not "AddLot" / "GetLotUnitId".
- `SetDBSegmentMessageServer(SL::spMS2)` was never called natively: every occupant message after load was dropped (new occupants never
  got models). Bound in NativeCityView right after the model maker's initial pass.
- Next for growth: GROWTH-1 and LEDGER-LOTDEV (the lot developer's construction half is not implemented despite the ledger).

## Addendum 13: the tract developer builds on empty zoned lots (TRACT-1, 2026-10-30)
- `cSC4TractDeveloper::Grow`'s pass loop and the existing-lot path are transcribed from the decompile + disassembly:
  ListCandidateLots_Existing, IsLotSuitable, IsLotConfigurationCompatible, TestForCornerLot, FindNetworks, Build, PickBuilding,
  IsBuildingCompatible. Unit test `TestBuildOnEmptyLot`: an empty zoned lot reaches the lot developer's StartConstruction with the
  chosen configuration and building. Not byte-exact (the engine calls go through ITractDeveloperHost; byte-match 12 % on the one
  function that pairs by name). Remaining routes (take-over, aggregation, subdivision, farms): TRACT-2.
- New ITractDeveloperHost seams (cell queries, DemolishLots, the building exemplar flag, the network manager); the native host answers
  "provided" for power / water / fire / park / land value until those simulators run natively (TRACT-HOST-1).
- Port bugs found on the way: cSC4LotConfiguration's constructor and Shutdown set the minimum slope to 90 (the original stores
  {90.0f, 0.0f} in one 8-byte write, so the minimum is 0): every lot failed the slope test. The lot configuration manager never heard
  the city-init message 0x26d31ec1 natively, so no configuration had its building styles / capacities: NativeLotConfigHost subscribes
  on its behalf. Lot road access (ILotHost::RoadAccessCells) was a neutral `false`: now the zone developer's edge cells are scanned
  for a road-type network occupant (mask 0x449).
- cSC4BuildingDevelopmentSimulator::GrowthStageFitsInZone / BuildingFitsInZone moved to src/sc4/BuildingZoneFit.cpp (the simulator's
  statics forward) so the tract developer can call them despite the header clash.
- Trace: `SC4_SIMDEBUG=1` prints each Grow with its histogram (row:outcome=count; rows per TractHistogramBucket).
- In game (SC4_SIMDEBUG, zone R$ beside the road between the two towns): 15 configurations qualify, every lot is then refused
  with outcome 21 (no road access): lots face the auto-street rows zoning plans between them, and those cells hold no road because
  drawing auto-streets needs cSC4NetworkTool (TOOL-1). So the next blocker for visible growth is TOOL-1, then layer 4
  (cSC4LotDeveloper::StartConstruction, buildings appearing).
