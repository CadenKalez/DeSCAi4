# In-game UI and tools: audit (2026-10-29)

Standard: the UI and tools come from the recovered game code, and every recovered function is checked against the retail
binary (disassembly, and the machine-code oracle where the function can run under Rosetta). Layout and art are not redrawn:
the game's own UI scripts and `.dat` resources build the windows through the recovered controls, so they are exact when the
recovered window procs and controls run.

## Already recovered and run natively
- `cSC4View3DWin::AddGUI` builds the city-view windows from the UI scripts; panels wired natively: MySim, Graphs, Budget
  (`NativeUi`, `NativeCityUi`, `NativeBudgetUi`), the catalog (`NativeCatalogUi`), the region screen, the zone overlay.
- Tools that run: city scroll, query (`cSC4ViewInputControlQuery`), zoning (`NativeZoneTool`).
- Recovered window procs (all functions): Budget (3), BuildingSelect, Compass, Credits, Dialogs, EstablishCity, GamePreferences,
  Legend, MySim, Panel, RegionSignpost, TimeDisplayMini, NewsTicker (3 partial), GraphsView (2 partial).
- Recovered tool classes: ViewInputControl base (37), Effect (16), SignTool (21), PlaceLot (32 + 3 partial),
  UndergroundNetworkTool (36 + 1), PowerLineTool (27 + 3).

## Partial: recovered but with unfinished pieces
- `cSC4ViewInputControlQuery`: 74 recovered, **148 partial**. `cSC4NetworkTool`: 52 recovered, **144 partial**.
- `cSC4View3DWin`: 136 recovered, 14 partial, 38 skipped as "not needed on macOS" (a scope call, revisit).

## Not recovered yet (functions left)
Input controls: TerrainScrubber 27, Zone 24, Dispatch 21, Torch 21, LabelTool 19, GameOfLife 16, NetworkTool 16, Demolish 16,
CitySituations 14, NetworkIntxTool 12, CityScroll 11, LevelTerrain 11, EffectDisaster 10, Flora 9, SignLabelDemolish 9,
PlayerDrive 36, NetworkToolUI 35.
Windows: SnapshotAlbum 46, SubBudgetWindow 44, Tutorial 30, Advisor 28 + 20, AudioPreferences 25, PickSim 21, MySimBalloon 21,
SnapshotTaker 21, LotConfigurationChooser 20, NetworkLotChooser/Editor 17 + 17, TipOfTheDay 17, VideoPreferences 13,
QueryDialog 11, ToolTip family (GZWinToolTip 18, ToolTipMgr 16, SC4ToolTipManager 10), LuaScriptView/Console 36 + 22.

## Host seams still stubbed in `NativeView3DWinHost.cpp` (the city view's host)
Terrain scrubber and level-terrain tools, toolbar button, compass window and its win proc, panel win proc, news ticker,
building select, advisor popup tracker and advisor window, WinBMP and image buffer, map view init, command execution,
disaster / situation alerts, tutorial filter, URL and advice links, cheat codes, catalog item commands, accelerator commands,
24-hour clock hover, cursor text. The network, power-line, underground, effect and sign tools are installed as stubs in
`NativeCityHosts.cpp`.

## Order that unlocks the most
1. Finish the 148 query and 144 network-tool partials (the two tools the player uses most).
2. Terrain scrubber + level terrain + demolish (TerrainScrubber 27, Demolish 16, LevelTerrain 11), then Zone 24 so zoning is the recovered class and not `NativeZoneTool`.
3. Toolbar button + compass + panel win proc seams (they are small and make the bars appear).
4. Tooltips (GZWinToolTip family), then advisor, tutorial, dialogs.
Each step: recover from the disassembly, test, oracle where the function is leaf or can be faked, ledger, then replace the
stub in the host.
