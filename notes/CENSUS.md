# Census of stubs, partials and missing classes
Generated from notes/ledger.csv and a source scan (2026-10-29).

## Ledger totals
- recovered: 26830
- recovered-partial: 1622
- skipped: 392
- partial: 129
- pending: 23

## Pending (not recovered): classes with the most rows

| rows | class |
|---|---|
| 7 | cSC4Automaton |
| 4 | cSTETerrain |
| 3 | nS3D |
| 2 | cSC4PlumbingSimulator |
| 2 | cSC4MySimAgentSimulator |
| 2 | cRZDate |
| 2 | (free) |
| 1 | cSC4PollutionSimulator |

## Partial (hand-written, known gaps): classes with the most rows

| rows | class |
|---|---|
| 103 | (free) |
| 6 | cS3DShapeFile |
| 4 | cSC4Demolition |
| 4 | cFreshnessCOMDirector |
| 3 | cGZSoundCOMDirector |
| 2 | cSC4ResourceLibCOMDirector |
| 1 | cGZUIScriptCOMDirector |
| 1 | cGZToolsCOMDirector |
| 1 | cGZScriptCOMDirector |
| 1 | cGZResManCOMDirector |
| 1 | cGZGraphicCOMDirector |
| 1 | cGZDrawCOMDirector |
| 1 | cFontCOMDirector |

## Recovered-partial (recovered, needs differential/oracle or has seams): classes with the most rows

| rows | class |
|---|---|
| 257 | (free) |
| 144 | cSC4NetworkTool |
| 93 | cSC4LotDeveloper |
| 92 | cGZGraphicSystem |
| 65 | cSC4CitySituationManager |
| 63 | cSC4TrafficSimulator |
| 61 | cSC4PowerPoleOccupant |
| 60 | cSC4ResidentialSimulator |
| 36 | cSC4ViewInputControlQuery |
| 35 | cSC4FloraSimulator |
| 33 | cSC4PowerSimulator |
| 30 | cSC4AutomataCOMDirector |
| 28 | cGZDrawContext |
| 23 | cSC4Automaton |
| 20 | cGZWinCOMDirector |
| 17 | nSGLDX7 |
| 17 | cSC4TractDeveloper |
| 17 | cSC4ResourceLibCOMDirector |
| 17 | cSC4App |
| 15 | cSC43DRender |
| 15 | cGZResManCOMDirector |
| 14 | nGZLog |
| 14 | cSC4TrafficAutomataManager |
| 12 | cSTESurfaceWater |
| 12 | cSC4View3DWin |
| 12 | cSC4ScriptedWindowCOMDirector |
| 12 | cSC4CitySituationAdvice |
| 12 | cRZString |
| 12 | cGZGraphicSystemW32 |
| 11 | cSC4Watercraft |
| 11 | cSC4EffectsManager |
| 11 | cGZLegacyControl |
| 10 | cSC4WinLotConfigurationEditor |
| 10 | cGZWinGrid |
| 9 | cSC4ProtectionSimulator |
| 9 | cSC4ModelMaker |
| 9 | cS3DModelInstanceAnimated |
| 8 | cSC4WinRegionScreen |
| 8 | cSC4WinMapView |
| 8 | cSC4AudioAmbienceLayer |

## Skipped (deliberately not ported): classes with the most rows

| rows | class |
|---|---|
| 192 | (free) |
| 56 | std |
| 38 | cSC4View3DWin |
| 21 | cSC4WinLotConfigurationEditor |
| 15 | cGZWinFileBrowser |
| 7 | cSC4PathFinder |
| 6 | cSC4EffectsManager |
| 3 | cSC4WinAnimationRecorder |
| 3 | cSC4WatercraftManager |
| 3 | cSC4PowerLineTool |
| 3 | cSC4ModelMaker |
| 3 | cSC4LightingManager |
| 3 | cSC4Automaton |
| 3 | cSC4Airplane |
| 2 | cSC4ZoneDeveloper |
| 2 | cSC4VertexHtConstraintSatisfier |
| 2 | cSC4TrafficNetworkMap |
| 2 | cSC4TrafficAutomaton |
| 2 | cSC4TrafficAutomataManager |
| 2 | cSC4SeaportDeveloper |
| 2 | cSC4PylonOccupant |
| 2 | cSC4PortDeveloper |
| 2 | cSC4EffectsCollection |
| 2 | cSC4DemandSimulator |
| 2 | cSC4ConstructionOccupant |
| 2 | cSC4AutomataController |
| 2 | cSC4Aircraft |
| 2 | cSC43DRender |
| 2 | cRZSpanList |
| 2 | cGZDrawContextRenderToHardware |
| 1 | cSCPropertyView |
| 1 | cSC4RegionalCity |
| 1 | cGZWinFolderRow |
| 1 | cGZBufferResourceFactory |

## Source markers (grep, 2026-10-29)

- `PARTIAL` markers: 397 across 90 files. Heaviest: TrafficAutomataManager.cpp (20), TractDeveloper.cpp (19), render/gd/SGLDX7Driver.cpp (18),
  WinRegionScreen.cpp (17), Render3D.h (16), gz/LegacyControl.cpp (14), EffectsManager.cpp/.h (25), Automaton.cpp (12), ViewInputControlQuery.cpp (11),
  Watercraft.cpp (10), View3DWin.h (10), WinLotConfigurationEditor.* (15), ViewInputControlPlaceLot.cpp (8), NetworkManager.* (15), PowerLineTool.cpp (7).
- `SC4_UNWIRED` call sites in src/gz/GZCOM.cpp: 196 (null/weak GZCOM creators that log when reached).
- Unwired host seams the report names: IPatrolCarHost / IEmergencyVehicleHost / IFireTruckHost (no native hosts: vehicles have no crime, police or siren services).

## Missing or null classes (known)

- City load reports 0 classes missing on Konradshohe, both New City saves and the tutorial cities; the remaining gaps are *behaviour*, not factories.
- No object for: cSC4RiotPolice (18 fns), cSC4SimFireCrew (26), cSC4Automaton remaining 7 pending rows, cSC4PlumbingSimulator (2), cSC4PollutionSimulator (1), cSC4MySimAgentSimulator (2 rows).
- Network tool family (cSC4NetworkTool 144 recovered-partial rows; NetworkToolUI / ViewInputControlNetworkTool not recovered): road tool is the first failing mission step.
- Renderer: cSTEWaterRipple (28), cSTEWaterWave (4), particles (~100 fns), cSC4ModelBackgroundLoader (25), AddBackgroundImageToRenderer, LoadFoundationModels,
  cGZGraphicSystem (92 partial rows), cGZDrawContext (28), cSC43DRender picks and screen shake, cSTEOverlayManager DrawShadows/DrawDecals not driven natively.

## Fakes and shortcuts on the native path

- Lighting time of day now follows the recovered cSC424HourClock (visual time = game time, advanced by OnIdle each frame at the 100x magnification). The city carries no clock component natively, so NativeLighting.cpp runs its own instance started at 10:00 (the HUD's value); the loaded city's saved clock time, the clock's hour messages and the HUD readout are not connected to it. SC4_TIMEOFDAY still overrides.
- Occupant shadows (2026-10-29, second pass): drawn by the port's own terrain-conforming quad pass (NativeShadowOverlay.cpp; the original
  draws the terrain mesh subsets with camera-space texgen) with the DrawShadows state on both stages: stage 0 the model texture through
  texMatrix (repeat), stage 1 the 4x4 soft-edge mask 0xaa592b77/0x1abe787d through shadowMatrix (clamp), culled by the terrain view's
  draw-frustum rows, removed / hidden with their occupant (placeable host RemoveOverlay / SetOverlayVisible). Port choices still open: the
  shadow RGB is the lighting manager's shadow colour in the vertex colour (the original's combiner selects the terrain vertex colour and
  is not reproduced), the blend alpha carries strength * falloff (the alpha test is rescaled so the pass/fail set matches the original),
  batches by (texture, strength) instead of per overlay, and no water pass (shadows over water are not drawn).
- Water tick not registered with the simulator (AnimateWater does not run); IsNavigable answers (network/lot/occupant) are stubs.
- Region and city UI panels beyond the tutorial path (many WinRegionScreen / WinMapView PARTIAL markers).
