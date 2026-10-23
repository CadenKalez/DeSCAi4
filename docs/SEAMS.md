# Seams: how the recovered pieces get joined

Recovered classes never call the engine's globals (`SL::sp*`) directly. Where the original reached an unrecovered neighbour, the
code talks to a small interface (a "seam": `I*Host`, `I*Services`, ...). A test supplies a fake; the real game supplies the recovered
class. When a neighbour is finished, write the adapter, replace the fake in the app, and tick the entry below.

Rules every agent follows: (1) a seam method mirrors one original call (note its vtable slot / address in a comment);
(2) a function that depends on a seam it cannot satisfy is `recovered-partial` in `notes/ledger.csv`, never `recovered`;
(3) after adding or removing a seam run `python3 scripts/list_seams.py` and commit `docs/SEAMS.md`.

## cSTETerrainView3D (src/sc4/cSTETerrainView3D*)

The view is complete except for the neighbours below. Everything it needs from them goes through `ITerrainViewHost` and
`ITerrainDrawContext` (both in `cSTETerrainView3D.h`); `ITerrainVertBufExtn` is the optional GL vertex-buffer fast path.

| Seam method(s) | Original it stands for | Real class to hook up | Status |
|---|---|---|---|
| `ITerrainDrawContext` (all) | `SC4DrawContext` (`SetRenderState`, `SetTexture`, `SetVertexBuffer`, `DrawPrimsIndexed`, ...) | `SC4DrawContext` (58 functions left) over `cMetalGDriver` | adapter prototype in `scripts/terrain_shot.mm` |
| `DrawFrustum()` / `ViewFrustum()` / `GetVisibleEdges()` | `SC4DrawContext +0x28` = `SC4ViewUtilities` (offsets in the header comments) | `SC4ViewUtilities` | not started |
| `Pass()`, `DrawEnabled()`, `GridSuppressed()`, `RenderFlag738()` | draw context fields `+0x80`, render properties `+0x3f8` / `+0x738` | `SC4DrawContext` / `cSC4RenderProperties` | not started |
| `GetNormalLight`, `GetPointLight`, `HasLighting`, `ApplyTerrainLighting` | `SL::spLightingManager` slots `0xa0`/`0xc0`, `cSTETerrain::DoLighting` | lighting manager; `cSTETerrain::DoLighting` (see below) | not started |
| `LoadTerrainTexture`, `LoadWaterTexture`, `LoadGridTexture`, `LoadLotTexture`, `TexturesUsedInTerrainType` | `spTextureBindingFactory` (key type `0x7ab50e44`, group `0x891b0e1a`, instance = texture id + zoom * 0x100), `cSTETerrain::sTexturesUsedInTerrainType` | texture binding factory + `cSTETerrain::ReadTextureInfos` | not started |
| `GetBuffer` | `spGimexFactory` slot `0x50` (gradients `0xc9efb727`, `0x69efb6f8`, glare mask `0x69efb7da`, displacement maps `0xca5cc9c0/1`) | Gimex image factory | not started |
| `ReadTuning*`, `ReadPlumbingColor`, `HasTuningExemplar` | tuning exemplar (type `0x6534284a`, group `0x88cd66e9`, id `0xa52160f5`) read with `cSCPropertyHelp::GetPropertyValue` | resource manager + property holders | not started |
| `AddRenderLayer` / `RemoveRenderLayer` | `SL::spRenderer` slots `0x100` / `0x108` (layers 0, 2, 5; priorities 0, 1000, 0x44c) | city renderer | not started |
| `RegisterMessage` / `UnregisterMessage` | `SL::spMS2` slots `0x28` / `0x30`; ids `0x277a9364 0x278e6003 0x678e6e3a 0xa6b79601 0xa6b79611 0xc9da96ea` | message server | not started |
| `CreateSelection` -> `ITerrainSelection` | `cSTESelectedTerrainArea` | `cSTESelectedTerrainArea` | not started |
| (missing) overlay managers, `cSTEWaterRipple`, `cSTEWaterWave` | created in `Init` (4x `cSTEOverlayManager`, ripple, wave); `GetOverlayManager` is **recovered-partial** | those classes | not started: add `ITerrainViewHost::CreateOverlayManager` etc. when they exist |
| `PlumbingCellFlags`, `PlumbingQuery58/40`, `NetworkHiddenTriangles`, `CellHasOccupantType`, `GetBoolPlumbingDisplay` | `spPlumbingSim` slots `0x38/0x58/0x40`, `NetworkManager` slots `0x50/0x58`, occupant manager slot `0x148`, `cSTETerrain::sbDisplayPlumbingInfo` | plumbing simulator, network manager, occupant manager | not started |
| `WaterSurfaceDisplayState`, `RedisplayTerrain`, `PreferenceDisplayGrid`, `StorePreferenceDisplayGrid` | `cSTETerrain::GetWaterSurfaceDisplayState` / `RedisplayTerrain`, `SL::spPreferences` | `cSTETerrain` (see below), preferences | not started |
| `ColorToLuminance` | `nS3D::kColorToLuminance` (**assumed 0.299/0.587/0.114**, the real constants are computed at start-up and were not readable) | S3D static init | verify against the binary |

### What `cSTETerrain` must grow (it holds the data this view reads)
`Init`, `Shutdown`, `DoLighting(rect)`, `RedisplayTerrain(...)`, `GetView()`, `GetWaterSurfaceDisplayState` / `SetWaterSurfaceDisplayState`,
`ReadTextureInfos` + `sTexturesUsedInTerrainType`, `AssignSurfaceTexturesAndColors` (fills the three texture layers the view reads: u16 key at
vertex `+0x16/0x1a/0x1e`, alpha at `+0x18/0x1c/0x20`, colours at `+0x10` and `+0x13`). Until then the view works on a terrain whose surface bytes
the caller fills (see `scripts/terrain_shot.mm`).

### Known quirks kept on purpose (do not "fix")
* `StaticInit` tests `sWaterColorDepthGradientHt != 0` instead of the gradient pointer, so the height stays 0 and the water colour is always
  gradient row 0 (confirmed in the disassembly at `0x10074d666`).
* `DrawWaterBox` takes the ray origin's z from the second box point; `DrawMixedMeshSubsetInViewFrustum` uses the draw frustum's column table;
  `DrawTerrainMeshSubsetInViewFrustum` bounds rows with the whole frustum rows, the draw variant with the terrain rows.

### Hook-up checklist (when the neighbours exist)
1. Write `SC4DrawContext` over `cMetalGDriver`; make it implement `ITerrainDrawContext` (start from the adapter in `scripts/terrain_shot.mm`).
2. Write the `ITerrainViewHost` adapter once (it is one class) and pass it to `cSTETerrainView3D::StaticInit`.
3. Implement the missing `cSTETerrain` functions above, then delete the caller-filled surface bytes in the demo.
4. Run `scripts/run_terrain_shot.sh` (expect textured land) and `zsh scripts/runone.sh sc4_terrainview3d`.
5. Flip the ledger entry `GetOverlayManager` to `recovered` and regenerate this file.

<!-- generated:start -->
## Seam registry (generated by scripts/list_seams.py)

193 seams, 6 with no implementation at all, 167 with only a test fake.

| Seam | Declared in | Used by | Status | Implemented by |
|---|---|---|---|---|
| `IAdviceHost` | `src/sc4/Advice.h` | src/sc4/Advice.cpp, src/sc4/RewardAdvice.cpp | test fake only | scripts/tests/sc4_advice_subclasses_test.cpp, scripts/tests/sc4_advice_test.cpp |
| `IAdviceSubjectHost` | `src/sc4/AdviceSubject.h` | src/sc4/AdviceSubject.cpp | test fake only | scripts/tests/sc4_advice_subclasses_test.cpp, scripts/tests/sc4_advicesubject_test.cpp |
| `IAdvisorHost` | `src/sc4/Advisor.h` | src/sc4/Advisor.cpp | test fake only | scripts/tests/sc4_advisor_test.cpp, scripts/tests/sc4_mysim_test.cpp |
| `IAmbienceLayerHost` | `src/sc4audio/AmbienceLayer.h` | src/sc4audio/AmbienceLayer.cpp | test fake only | scripts/tests/sc4audio_ambiencelayer_test.cpp |
| `IAmbienceStateHost` | `src/sc4/AmbienceStateFilters.h` | src/sc4/AmbienceStateFilters.cpp | test fake only | scripts/tests/sc4_ambiencestatefilters_test.cpp |
| `IAnimatedModelHost` | `src/s3d/AnimatedInstance.h` |  | test fake only | scripts/tests/s3d_animatedinstance_test.cpp |
| `IAudioQueueApi` | `src/audio/DirectSoundMac.h` | src/audio/DirectSoundMac.cpp | test fake only | scripts/tests/audio_directsoundmac_test.cpp |
| `IAutomataAttractorHost` | `src/sc4/AutomataAttractor.h` | src/sc4/AutomataAttractor.cpp | test fake only | scripts/tests/sc4_automataattractor_test.cpp |
| `IAutomataControllerHost` | `src/sc4/AutomataController.h` | src/sc4/AutomataController.cpp, src/sc4/AutomataControllerGrid.cpp | test fake only | scripts/tests/sc4_automataattractor_test.cpp, scripts/tests/sc4_automatacontroller_test.cpp |
| `IAutomataGeneratorHost` | `src/sc4/AutomataGenerator.h` | src/sc4/AutomataGenerator.cpp | test fake only | scripts/tests/sc4_automatagenerator_test.cpp |
| `IAutomataManagerHost` | `src/sc4/AutomataManager.h` | src/sc4/AutomataManager.cpp | test fake only | scripts/tests/sc4_automatamanager_test.cpp |
| `IAutomataScriptHost` | `src/sc4/AutomataScriptSystem.h` |  | NOT IMPLEMENTED |  |
| `IAutomatonHost` | `src/sc4/Automaton.h` | src/sc4/Automaton.cpp | test fake only | scripts/tests/sc4_automaton_test.cpp |
| `IBaseLotDeveloperHost` | `src/sc4/LotDeveloper.h` | src/gz/SimHostRegistry.h | test fake only | scripts/tests/sc4_lotdeveloper_test.cpp |
| `IBudgetDialogHost` | `src/sc4/BudgetDialogs.h` | src/sc4/BudgetDialogs.cpp | test fake only | scripts/tests/sc4_budgetdialogs_test.cpp |
| `IBudgetHost` | `src/sc4/Budget.h` | src/sc4/Budget.cpp | implemented | scripts/tests/sc4_budget_test.cpp, src/sc4/BudgetSimulator.h |
| `IBudgetSimServices` | `src/sc4/BudgetSimulator.h` | src/sc4/BudgetSimulator.cpp | test fake only | scripts/tests/sc4_budgetsimulator2_test.cpp |
| `IBufferHost` | `src/gz/Buffer.h` | src/gz/Buffer.cpp | test fake only | scripts/tests/gz_persistbufferresource_test.cpp, scripts/tests/gz_winfolders_test.cpp |
| `IBuildingDevelopmentHost` | `src/sc4/BuildingOccupant.h` | src/sc4/BuildingOccupant.cpp | test fake only | scripts/tests/sc4_ambiencefilters_test.cpp |
| `IBuildingDevelopmentSimHost` | `src/sc4/BuildingDevelopmentSimulator.h` | src/sc4/BuildingDevelopmentSimulator.cpp | test fake only | scripts/tests/sc4_buildingdevelopment_test.cpp |
| `ICameraControlHost` | `src/sc4/CameraControl.h` | src/sc4/CameraControl.cpp | test fake only | scripts/tests/sc4_cameracontrol_test.cpp |
| `ICatalogViewHost` | `src/sc4/WinCatalogView.h` | src/sc4/WinCatalogView.cpp | test fake only | scripts/tests/sc4_wincatalogview_test.cpp |
| `ICityHost` | `src/sc4/cSC4City.h` | src/gz/SimHostRegistry.h, src/sc4/cSC4City.cpp | test fake only | scripts/tests/gzcomcreators_core_part2.cpp, scripts/tests/sc4_city_test.cpp |
| `ICitySituationAdviceHost` | `src/sc4/CitySituationAdvice.h` | src/sc4/CitySituationAdvice.cpp | test fake only | scripts/tests/sc4_citysituationadvice_test.cpp |
| `ICitySituationManagerHost` | `src/sc4/CitySituationManager.h` | src/sc4/CitySituationManager.cpp | test fake only | scripts/tests/sc4_citysituation_test.cpp |
| `IClockHost` | `src/sc4/cSC424HourClock.h` | src/sc4/cSC424HourClock.cpp | test fake only | scripts/tests/sc4_clock_test.cpp |
| `ICommercialHost` | `src/sc4/CommercialSimulator.h` | src/gz/SimHostRegistry.h | test fake only | scripts/tests/sc4_commercialsimulator_test.cpp |
| `ICompassHost` | `src/sc4/WinProcs.h` |  | test fake only | scripts/tests/sc4_winprocs_test.cpp |
| `IControllerManagerHost` | `src/sc4/AutomataControllerManager.h` | src/sc4/AutomataControllerManager.cpp | test fake only | scripts/tests/sc4_automatacontrollermanager_test.cpp |
| `ICookieHTTPHost` | `src/rz/CookieManager.h` | src/rz/CookieManager.cpp | test fake only | scripts/tests/rz_cookiemanager_test.cpp |
| `ICrimeHost` | `src/sc4/CrimeSimulator.h` | src/sc4/CrimeSimulator.cpp | test fake only | scripts/tests/sc4_crimesimulator_test.cpp |
| `IDX7Host` | `src/render/DirectDraw7.h` | src/render/DX7Host.h, src/render/DirectDraw7.cpp | implemented | scripts/tests/render_directdraw7_test.cpp, src/render/DX7Host.cpp |
| `IDemandHost` | `src/sc4/cSC4Demand.h` |  | test fake only | scripts/tests/sc4_demand_test.cpp |
| `IDemolitionHost` | `src/sc4/Demolition.h` |  | test fake only | scripts/tests/sc4_demolition_test.cpp |
| `IDetokenizerHost` | `src/sc4/StringDetokenizer.h` | src/sc4/StringDetokenizer.cpp | test fake only | scripts/tests/sc4_stringdetokenizer_test.cpp |
| `IDisasterHost` | `src/sc4/Disaster.h` | src/sc4/Disaster.cpp, src/sc4/DisasterManagers.cpp, src/sc4/DisasterManagers.h ... | test fake only | scripts/tests/sc4_disaster_test.cpp, scripts/tests/sc4_disastermanagers_test.cpp |
| `IDispatchHost` | `src/sc4/DispatchManager.h` | src/sc4/DispatchManager.cpp | test fake only | scripts/tests/sc4_dispatchmanager_test.cpp |
| `IDrawContextHost` | `src/gz/DrawContext.h` | src/gz/DrawContext.cpp | test fake only | scripts/tests/gz_drawcontext_test.cpp, scripts/tests/gz_drawsystem_test.cpp |
| `IDrawFontRendererHost` | `src/gz/DrawFontRenderer.h` | src/gz/DrawFontRenderer.cpp | test fake only | scripts/tests/gz_drawfontrenderer_test.cpp |
| `IDrawSystemHost` | `src/gz/DrawSystem.h` | src/gz/DrawSystem.cpp | test fake only | scripts/tests/gz_drawsystem_test.cpp |
| `IEffectToolHost` | `src/sc4/ViewInputControlEffect.h` | src/sc4/ViewInputControlEffect.cpp | test fake only | scripts/tests/sc4_viewinputcontrol_effect_test.cpp |
| `IEffectsCollectionHost` | `src/sc4/EffectsCollection.h` | src/sc4/EffectsCollection.cpp | test fake only | scripts/tests/sc4_effectscollection_test.cpp |
| `IEffectsHost` | `src/sc4/EffectComponents.h` | src/sc4/EffectComponents.cpp | test fake only | scripts/tests/sc4_effectcomponents_test.cpp |
| `IEffectsManagerHost` | `src/sc4/EffectsManager.h` | src/sc4/EffectsManager.cpp | test fake only | scripts/tests/sc4_effectsmanager_test.cpp |
| `IEstablishCityHost` | `src/sc4/EstablishCityWinProc.h` | src/sc4/EstablishCityWinProc.cpp | NOT IMPLEMENTED |  |
| `IFeatureHost` | `src/sc4/BusinessFeature.h` | src/sc4/BusinessFeature.cpp | test fake only | scripts/tests/sc4_businessfeature_test.cpp |
| `IFileBrowserHost` | `src/gz/WinFileBrowser.h` |  | test fake only | scripts/tests/gz_winfilebrowser_test.cpp |
| `IFileSystemHost` | `src/gz/FileSystem.h` | src/gz/FileSystem.cpp | test fake only | scripts/tests/gz_filesystem_test.cpp |
| `IFireHost` | `src/sc4/FireProtectionSimulator.h` | src/gz/SimHostRegistry.h, src/sc4/FireProtectionSimulator.cpp | test fake only | scripts/tests/sc4_fireprotectionsimulator_test.cpp |
| `IFlammabilityHost` | `src/sc4/cSC4FlammabilitySimulator.h` | src/gz/SimHostRegistry.h, src/sc4/cSC4FlammabilitySimulator.cpp | test fake only | scripts/tests/sc4_flammability_test.cpp |
| `IFloraHost` | `src/sc4/FloraOccupant.h` | src/sc4/FloraOccupant.cpp | test fake only | scripts/tests/sc4_floraoccupant_test.cpp |
| `IFloraSimulatorHost` | `src/sc4/FloraSimulator.h` | src/sc4/FloraSimulator.cpp | test fake only | scripts/tests/sc4_florasimulator_test.cpp |
| `IFontHost` | `src/gz/FontServices.h` | src/gz/GZFont.cpp, src/gz/GZFont.h | implemented | scripts/tests/gz_gzfont_test.cpp, src/gz/FontSys.h |
| `IFontSysHost` | `src/gz/FontSys.h` | src/gz/FontSys.cpp | test fake only | scripts/tests/gz_fontsys_test.cpp |
| `IGLApi` | `src/render/GLApi.h` | src/render/D3DDevice7.cpp, src/render/D3DDevice7.h, src/render/D3DVertexBuffer7.cpp ... | implemented | scripts/tests/gl_trace.h, src/render/GLApi.cpp |
| `IGLCacheHost` | `src/gz/BufferSimGLCache.h` | src/gz/BufferSimGLCache.cpp | test fake only | scripts/tests/gz_buffersimglcache_test.cpp |
| `IGZCOMHost` | `src/gz/cGZCOM.h` |  | implemented | scripts/tests/gz_com_test.cpp, src/gz/GZFramework.h |
| `IGraphHost` | `src/gz/Graph.h` | src/gz/BarGraph.cpp, src/gz/Graph.cpp, src/gz/LineGraph.cpp ... | test fake only | scripts/tests/fake_graph.h |
| `IGraphicSystemHost` | `src/gz/GraphicSystemW32.h` | src/gz/GraphicSystemW32.cpp | test fake only | scripts/tests/gz_graphicsystemw32_test.cpp |
| `IHardwareRenderHost` | `src/gz/DrawContextRenderToHardware.h` |  | test fake only | scripts/tests/gz_drawcontextrendertohardware_test.cpp |
| `IHitDirectorHost` | `src/audio/HitManDirector.h` | src/audio/HitManDirector.cpp | test fake only | scripts/tests/audio_hitmandirector_test.cpp |
| `IHitIniHost` | `src/audio/HitIniFile.h` | src/audio/HitIniFile.cpp | test fake only | scripts/tests/audio_hitinifile_test.cpp |
| `IHtmlDocumentServices` | `src/html/HTMLTypes.h` | src/html/HTMLElements.cpp, src/html/HTMLElements.h | implemented | scripts/tests/html_elements_test.cpp, src/html/HTMLDocument.h |
| `IIMEProxyHost` | `src/gz/WinIMEProxy.h` | src/gz/WinIMEProxy.cpp | test fake only | scripts/tests/gz_winimeproxy_test.cpp |
| `ILandValueHost` | `src/sc4/LandValueSimulator.h` | src/gz/SimHostRegistry.h | test fake only | scripts/tests/sc4_landvaluesimulator_test.cpp |
| `ILandfillZoneHost` | `src/sc4/LandfillZone.h` | src/sc4/LandfillZone.cpp | test fake only | scripts/tests/sc4_landfillzone_test.cpp |
| `ILoadHeightFieldHost` | `src/sc4/WinLoadHeightField.h` | src/sc4/WinLoadHeightField.cpp | test fake only | scripts/tests/sc4_winloadheightfield_test.cpp |
| `ILogOutputBackEndHost` | `src/gz/LogService.h` | src/gz/LogService.cpp | test fake only | scripts/tests/gz_logservice_test.cpp |
| `ILogServiceHost` | `src/gz/LogService.h` | src/gz/LogService.cpp | test fake only | scripts/tests/gz_logservice_test.cpp |
| `ILotBaseTextureHost` | `src/sc4/LotBaseTextureOccupant.h` | src/sc4/LotBaseTextureOccupant.cpp | test fake only | scripts/tests/sc4_lotbasetextureoccupant_test.cpp |
| `ILotConfigurationHost` | `src/sc4/LotConfiguration.h` | src/sc4/LotConfiguration.cpp | test fake only | scripts/tests/sc4_lotconfiguration_test.cpp |
| `ILotConfigurationManagerHost` | `src/sc4/LotConfigurationManager.h` | src/sc4/LotConfigurationManager.cpp | test fake only | scripts/tests/sc4_lotconfigurationmanager_test.cpp |
| `ILotHost` | `src/sc4/Lot.h` | src/sc4/Lot.cpp | test fake only | scripts/tests/sc4_lot_test.cpp |
| `ILotRetainingWallHost` | `src/sc4/LotRetainingWallOccupant.h` | src/sc4/LotRetainingWallOccupant.cpp | test fake only | scripts/tests/sc4_lotretainingwalloccupant_test.cpp |
| `ILuaApi` | `src/lua/Lua5Thread.h` | src/gz/LuaScriptServer.h, src/lua/Lua5.cpp, src/lua/Lua5.h ... | test fake only | scripts/tests/lua_lua5thread_test.cpp, scripts/tests/lua_minilua.h |
| `ILuaCommandGeneratorApi` | `src/gz/LuaScriptServer.h` |  | test fake only | scripts/tests/gz_luascriptserver_test.cpp |
| `ILuaScriptServerHost` | `src/gz/LuaScriptServer.h` | src/gz/LuaScriptServer.cpp | test fake only | scripts/tests/gz_luascriptserver_test.cpp |
| `IMacToolsHost` | `src/platform/MacTools.h` | src/platform/MacTools.cpp | test fake only | scripts/tests/mactools_test.cpp |
| `IMeshBuilderHost` | `src/sc4/DrawContext.h` | src/sc4/DrawContext.cpp | test fake only | scripts/tests/sc4_drawcontext_test.cpp |
| `IMeshMaterialHost` | `src/sc4/ViewUtilities.h` | src/sc4/ViewUtilities.cpp | test fake only | scripts/tests/sc4_viewutilities_test.cpp |
| `IMeshPickHost` | `src/sc4/ViewUtilities.h` | src/sc4/ViewUtilities.cpp | test fake only | scripts/tests/sc4_viewutilities_test.cpp |
| `IModelInstanceHost` | `src/s3d/ModelInstance.h` | src/s3d/ModelInstance.cpp | test fake only | scripts/tests/s3d_modelinstance_test.cpp |
| `IModelMakerHost` | `src/sc4/ModelMaker.h` |  | NOT IMPLEMENTED |  |
| `IMySimAdviceHost` | `src/sc4/MySimAdvice.h` | src/sc4/MySimAdvice.cpp | test fake only | scripts/tests/sc4_advice_subclasses_test.cpp |
| `IMySimFilterHost` | `src/sc4/MySimFilters.h` | src/sc4/MySimFilters.cpp | test fake only | scripts/tests/sc4_mysimfilters_test.cpp |
| `IMySimHost` | `src/sc4/MySim.h` | src/sc4/MySim.cpp | test fake only | scripts/tests/sc4_mysim_test.cpp |
| `INeighborConnectionHost` | `src/sc4/NeighborConnection.h` | src/sc4/NeighborConnection.cpp | test fake only | scripts/tests/sc4_neighborconnection_test.cpp, scripts/tests/sc4_neighborssimulator_test.cpp |
| `INeighborDealHost` | `src/sc4/NeighborDeal.h` | src/sc4/NeighborDeal.cpp | test fake only | scripts/tests/sc4_neighbordeal_test.cpp, scripts/tests/sc4_neighborssimulator_test.cpp |
| `INeighborsHost` | `src/sc4/NeighborsSimulator.h` | src/sc4/NeighborsSimulator.cpp | test fake only | scripts/tests/sc4_neighborssimulator_test.cpp |
| `INetworkOccupantHost` | `src/sc4/NetworkOccupant.h` | src/sc4/NetworkOccupant.cpp | test fake only | scripts/tests/sc4_networkoccupant_test.cpp, scripts/tests/sc4_networkoccupantwithprebuiltmodel_test.cpp |
| `INetworkToolHost` | `src/sc4/NetworkTool.h` | src/sc4/NetworkTool.cpp, src/sc4/UndergroundNetworkTool.cpp | test fake only | scripts/tests/sc4_networktool_test.cpp, scripts/tests/sc4_powerlinetool_test.cpp |
| `IOccupantGroupHost` | `src/sc4/OccupantGroupTemplate.h` | src/sc4/OccupantGroupTemplate.cpp | test fake only | scripts/tests/sc4_occupantgrouptemplate_test.cpp |
| `IOrdinanceHost` | `src/sc4/Ordinance.h` | src/sc4/Ordinance.cpp | test fake only | scripts/tests/sc4_ordinance_test.cpp, scripts/tests/sc4_ordinancetypes_test.cpp |
| `IOrdinanceSimHost` | `src/sc4/OrdinanceTypes.h` | src/sc4/OrdinanceTypes.cpp | test fake only | scripts/tests/sc4_ordinancetypes_test.cpp |
| `IPathFinderHost` | `src/sc4/PathFinder.h` | src/sc4/PathFinder.cpp | test fake only | scripts/tests/sc4_pathfinder_test.cpp |
| `IPathInfoServices` | `src/sc4/PathInfo.h` | src/sc4/PathInfo.cpp | test fake only | scripts/tests/sc4_pathinfo_test.cpp |
| `IPersistBufferHost` | `src/gz/PersistBufferResource.h` | src/gz/PersistBufferResourceFactory.cpp, src/gz/PersistBufferResourceFactory.h | implemented | scripts/tests/gz_persistbufferresource_test.cpp, scripts/tests/gz_persistbufferresourcefactory_test.cpp |
| `IPersistHost` | `src/gz/PersistStreams.h` | src/gz/PersistASCIIWriter.cpp, src/gz/PersistBinaryReader.cpp, src/gz/PersistBinaryWriter.cpp ... | test fake only | scripts/tests/gz_persist_test.cpp |
| `IPipeBurstHost` | `src/sc4/PipeBurstInstance.h` | src/sc4/PipeBurstInstance.cpp | test fake only | scripts/tests/sc4_pipeburstinstance_test.cpp |
| `IPlaceableHost` | `src/sc4/PlaceableObject.h` | src/sc4/PlaceableObject.cpp | test fake only | scripts/tests/sc4_placeableobject_test.cpp |
| `IPlumbingHost` | `src/sc4/PlumbingSimulator.h` | src/sc4/PlumbingSimulator.cpp | test fake only | scripts/tests/sc4_plumbingsimulator_test.cpp |
| `IPoliceHost` | `src/sc4/PoliceSimulator.h` | src/gz/SimHostRegistry.h, src/sc4/PoliceSimulator.cpp | test fake only | scripts/tests/sc4_policesimulator_test.cpp |
| `IPollutionHost` | `src/sc4/PollutionSimulator.h` | src/gz/SimHostRegistry.h, src/sc4/PollutionSimulator.cpp | test fake only | scripts/tests/sc4_pollutionsimulator_test.cpp |
| `IPowerHost` | `src/sc4/PowerSimulator.h` | src/gz/SimHostRegistry.h, src/sc4/PowerSimulator.cpp | test fake only | scripts/tests/sc4_powersimulator_test.cpp |
| `IPowerLineToolHost` | `src/sc4/PowerLineTool.h` | src/sc4/PowerLineTool.cpp | test fake only | scripts/tests/sc4_powerlinetool_test.cpp |
| `IPreBuiltModelOccupantHost` | `src/sc4/NetworkOccupantWithPreBuiltModel.h` | src/sc4/NetworkOccupantWithPreBuiltModel.cpp | test fake only | scripts/tests/sc4_networkoccupantwithprebuiltmodel_test.cpp |
| `IPrimitiveHost` | `src/render/gd/PrimitiveManager.h` |  | test fake only | scripts/tests/render_primitivemanager_test.cpp |
| `IPrimitiveManagerHost` | `src/render/gd/SGLDX7Driver.h` | src/render/gd/SGLDX7Driver.cpp | implemented | scripts/tests/render_sgldx7driver_test.cpp, src/render/gd/PrimitiveManager.h |
| `IPropControllerHost` | `src/sc4/PropOccupantAutomataController.h` | src/sc4/PropOccupantAutomataController.cpp | test fake only | scripts/tests/sc4_propoccupantautomatacontroller_test.cpp |
| `IPropHost` | `src/sc4/PropOccupant.h` | src/sc4/PropManager.cpp, src/sc4/PropOccupant.cpp | test fake only | scripts/tests/sc4_propmanager_test.cpp, scripts/tests/sc4_propoccupant_test.cpp |
| `IPropManagerHost` | `src/sc4/PropManager.h` | src/sc4/PropManager.cpp | test fake only | scripts/tests/sc4_propmanager_test.cpp |
| `IPropVisualEffectHost` | `src/sc4/PropOccupantVisualEffect.h` | src/sc4/PropOccupantVisualEffect.cpp | implemented | scripts/tests/sc4_propoccupantvisualeffect_test.cpp, src/sc4/EffectsManager.h |
| `IPropertyViewHost` | `src/sc4/PropertyView.h` | src/sc4/PropertyView.cpp | test fake only | scripts/tests/sc4_propertyview_test.cpp |
| `IProtectionHost` | `src/sc4/ProtectionSimulator.h` | src/sc4/ProtectionSimulator.cpp | implemented | scripts/tests/sc4_protectionsimulator_test.cpp, src/sc4/FireProtectionSimulator.h |
| `IQueryHost` | `src/sc4/ViewInputControlQuery.h` | src/sc4/ViewInputControlQuery.cpp | test fake only | scripts/tests/sc4_viewinputcontrolquery_test.cpp |
| `IRadioHost` | `src/rz/Radio.h` |  | implemented | scripts/tests/rz_radio_test.cpp, src/rz/Radio.cpp |
| `IRadioStationHost` | `src/rz/Radio.h` | src/rz/DiskRadioStation.cpp, src/rz/Radio.cpp | implemented | scripts/tests/rz_diskradiostation_test.cpp, scripts/tests/rz_radio_test.cpp |
| `IRegionCloudsHost` | `src/sc4/WinRegionClouds.h` | src/sc4/WinRegionClouds.cpp | test fake only | scripts/tests/sc4_winregionclouds_test.cpp |
| `IRegionFrameService` | `src/sc4/WinRegionScreen.h` |  | test fake only | scripts/tests/sc4_winregionscreen_test.cpp |
| `IRegionHost` | `src/sc4/Region.h` | src/sc4/Region.cpp | implemented | scripts/tests/sc4_region_test.cpp, src/sc4/RegionalCity.h |
| `IRegionScreenHost` | `src/sc4/WinRegionScreen.h` | src/sc4/WinRegionScreen.cpp | test fake only | scripts/tests/sc4_winregionscreen_test.cpp |
| `IRegionScrollHost` | `src/sc4/WinRegionScroll.h` | src/sc4/WinRegionScroll.cpp | test fake only | scripts/tests/sc4_winregionscroll_test.cpp |
| `IRegionViewHost` | `src/sc4/WinRegionView.h` | src/sc4/WinRegionView.cpp | test fake only | scripts/tests/sc4_winregionview_test.cpp |
| `IRegionalCityHost` | `src/sc4/RegionalCity.h` | src/sc4/NeighborsSimulator.h, src/sc4/RegionalCity.cpp | test fake only | scripts/tests/sc4_regionalcity_test.cpp |
| `IRender3DHost` | `src/sc4/Render3D.h` | src/sc4/Render3D.cpp | test fake only | scripts/tests/sc4_3drender_test.cpp |
| `IResidentialHost` | `src/sc4/ResidentialSimulator.h` | src/gz/SimHostRegistry.h, src/sc4/ResidentialSimulator.cpp | test fake only | scripts/tests/sc4_residentialsimulator_test.cpp |
| `IResizableOutlineHost` | `src/sc4/WinResizableOutline.h` | src/sc4/WinResizableOutline.cpp | test fake only | scripts/tests/sc4_winresizableoutline_test.cpp |
| `IResourceCatalogHost` | `src/sc4/ResourceCatalogView.h` | src/sc4/ResourceCatalogView.cpp | test fake only | scripts/tests/sc4_resourcecatalogview_test.cpp |
| `IRewardAdviceHost` | `src/sc4/RewardAdvice.h` | src/sc4/RewardAdvice.cpp | test fake only | scripts/tests/sc4_advice_subclasses_test.cpp |
| `ISCLuaHost` | `src/lua/SCLua.h` | src/lua/SCLua.cpp | test fake only | scripts/tests/lua_luasystem_test.cpp |
| `ISLBufferHost` | `src/sc4/SLBuffer.h` | src/sc4/SLBuffer.cpp | test fake only | scripts/tests/sc4_advice_subclasses_test.cpp, scripts/tests/sc4_region_test.cpp |
| `ISLStringHost` | `src/sc4/SLString.h` | src/sc4/SLString.cpp | test fake only | scripts/tests/sc4_advice_subclasses_test.cpp, scripts/tests/sc4_advice_test.cpp |
| `IScriptControlHost` | `src/gz/LegacyControl.h` | src/gz/LegacyControl.cpp | test fake only | scripts/tests/gz_legacycontrol_test.cpp |
| `IScriptResourceHost` | `src/sc4/UIResourceObjects.h` |  | test fake only | scripts/tests/sc4_uiresource_test.cpp |
| `IScriptTerpHost` | `src/gz/ScriptTerp.h` |  | implemented | scripts/tests/gz_scriptterp_test.cpp, scripts/tests/gz_uiscriptservice_test.cpp |
| `ISfxHost` | `src/audio/SFX.h` | src/audio/SFX.cpp | test fake only | scripts/tests/sfx_test.cpp |
| `ISignToolHost` | `src/sc4/ViewInputControlSignTool.h` | src/sc4/ViewInputControlSignTool.cpp | test fake only | scripts/tests/sc4_viewinputcontrol_signtool_test.cpp |
| `ISignpostHost` | `src/sc4/SignpostOccupant.h` | src/sc4/SignpostOccupant.cpp | test fake only | scripts/tests/sc4_signpostoccupant_test.cpp |
| `ISimulatorHost` | `src/sc4/Simulator.h` | src/sc4/Simulator.cpp | test fake only | scripts/tests/sc4_simulator_test.cpp |
| `ISoundBankHost` | `src/sc4/AudioAEMSSoundBank.h` | src/sc4/AudioAEMSSoundBank.cpp | test fake only | scripts/tests/sc4_audioaemssoundbank_test.cpp |
| `ISpriteInstanceHost` | `src/s3d/ModelInstanceAnimatedSprite.h` | src/s3d/ModelInstanceAnimatedSprite.cpp | test fake only | scripts/tests/s3d_modelinstanceanimatedsprite_test.cpp |
| `ITerrainBrush` | `src/sc4/EffectComponents.h` | src/sc4/EffectComponents.cpp | test fake only | scripts/tests/sc4_effectcomponents_test.cpp |
| `ITerrainDrawContext` | `src/sc4/cSTETerrainView3D.h` | src/sc4/cSTETerrainView3D.cpp, src/sc4/cSTETerrainView3DDraw.cpp | test fake only | scripts/terrain_shot.mm, scripts/tests/sc4_terrainview3d_test.cpp |
| `ITerrainHost` | `src/sc4/cSTETerrain.h` | src/sc4/cSTETerrain.cpp | test fake only | scripts/tests/sc4_steterrain_test.cpp |
| `ITerrainMapSource` | `src/sc4/EffectMaps.h` | src/sc4/EffectMaps.cpp | test fake only | scripts/tests/sc4_effectmaps_test.cpp |
| `ITerrainSelection` | `src/sc4/cSTETerrainView3D.h` |  | NOT IMPLEMENTED |  |
| `ITerrainVertBufExtn` | `src/sc4/cSTETerrainView3D.h` | src/sc4/cSTETerrainView3DDraw.cpp | NOT IMPLEMENTED |  |
| `ITerrainView` | `src/sc4/cSTETerrain.h` | src/sc4/ServiceLocator.cpp, src/sc4/ServiceLocator.h, src/sc4/cSTETerrain.cpp ... | test fake only | scripts/tests/sc4_steterrain_test.cpp |
| `ITerrainViewHost` | `src/sc4/cSTETerrainView3D.h` | src/sc4/cSTETerrainView3D.cpp | test fake only | scripts/terrain_shot.mm, scripts/tests/sc4_terrainview3d_test.cpp |
| `ITerrainViewSource` | `src/sc4/ServiceLocator.h` | src/sc4/ServiceLocator.cpp | test fake only | scripts/tests/sc4_servicelocator_test.cpp |
| `ITextDecalHost` | `src/sc4/TextDecalOccupant.h` | src/sc4/TextDecalOccupant.cpp | test fake only | scripts/tests/sc4_textdecaloccupant_test.cpp |
| `ITextureManagerHost` | `src/render/gd/SGLDX7Driver.h` | src/render/gd/SGLDX7Driver.cpp | test fake only | scripts/tests/render_sgldx7driver_test.cpp |
| `ITextureStageHost` | `src/render/TextureStageData.h` | src/render/TextureStageData.cpp, src/render/gd/SGLDX7Driver.h | implemented | scripts/tests/render_texturestage_oracle_driver.cpp, src/render/gd/MetalGDriver.mm |
| `IToxicHost` | `src/sc4/ToxicOccupant.h` | src/sc4/ToxicOccupant.cpp | test fake only | scripts/tests/sc4_disaster_test.cpp, scripts/tests/sc4_miscoccupants_test.cpp |
| `ITrafficEffectHost` | `src/sc4/TrafficEffectManager.h` |  | test fake only | scripts/tests/sc4_trafficeffectmanager_test.cpp |
| `ITrafficHost` | `src/sc4/TrafficSimulator.h` | src/gz/SimHostRegistry.h, src/sc4/NeighborsSimulator.h, src/sc4/TrafficSimulator.cpp | test fake only | scripts/tests/sc4_trafficsimulator_test.cpp |
| `ITrafficNetworkMapHost` | `src/sc4/TrafficNetworkMap.h` | src/sc4/TrafficNetworkMap.cpp | test fake only | scripts/tests/sc4_trafficnetworkmap_test.cpp |
| `ITreeViewServices` | `src/gz/WinTreeView.h` |  | implemented | scripts/tests/gz_wintreeview_test.cpp, src/gz/WinTreeView.cpp |
| `ITutorialTaskHost` | `src/sc4/TutorialTask.h` | src/sc4/TutorialTask.cpp | test fake only | scripts/tests/sc4_tutorialtask_test.cpp |
| `IUndergroundNetworkToolHost` | `src/sc4/UndergroundNetworkTool.h` | src/sc4/UndergroundNetworkTool.cpp | test fake only | scripts/tests/sc4_undergroundnetworktool_test.cpp |
| `IUndergroundOccupantHost` | `src/sc4/UndergroundOccupant.h` | src/sc4/UndergroundOccupant.cpp | test fake only | scripts/tests/sc4_undergroundoccupant_test.cpp |
| `IVertexBufferHost` | `src/render/gd/PrimitiveManager.h` | src/render/gd/PrimitiveManager.cpp | test fake only | scripts/tests/render_primitivemanager_test.cpp |
| `IVertexHtConstraintHost` | `src/sc4/VertexHtConstraintSatisfier.h` |  | implemented | scripts/tests/sc4_vertexhtconstraintsatisfier_test.cpp, src/sc4/VertexHtConstraintSatisfier.cpp |
| `IView3DWinHost` | `src/sc4/View3DWin.h` | src/sc4/View3DWin.cpp | test fake only | scripts/tests/sc4_view3dwin_test.cpp |
| `IViewInputHost` | `src/sc4/ViewInputControl.h` | src/sc4/ViewInputControl.cpp, src/sc4/ViewInputControlQuery.cpp, src/sc4/ViewInputControlQuery.h | test fake only | scripts/tests/sc4_viewinputcontrol_effect_test.cpp, scripts/tests/sc4_viewinputcontrol_signtool_test.cpp |
| `IViewUtilitiesHost` | `src/sc4/ViewUtilities.h` |  | implemented | scripts/tests/sc4_viewutilities_test.cpp, src/sc4/Render3D.h |
| `IVocabularyServices` | `src/gz/LegacyControl.h` | src/gz/LegacyControl.cpp | test fake only | scripts/tests/gz_legacycontrol_test.cpp |
| `IWealthHost` | `src/sc4/WealthRequester.h` | src/gz/SimHostRegistry.h | test fake only | scripts/tests/sc4_smallsim_test.cpp |
| `IWinAnimationRecorderHost` | `src/sc4/WinAnimationRecorder.h` | src/sc4/WinAnimationRecorder.cpp | test fake only | scripts/tests/sc4_winanimationrecorder_test.cpp |
| `IWinBMPViewHost` | `src/sc4/WinBMPView.h` |  | implemented | scripts/tests/sc4_winbmpview_test.cpp, src/sc4/WinBMPView.cpp |
| `IWinButtonHost` | `src/gz/WinBtn.h` | src/gz/WinBtn.cpp, src/gz/WinCombo.cpp, src/gz/WinLineInput.cpp ... | test fake only | scripts/tests/gz_winbtn_test.cpp, scripts/tests/gz_wincombo_test.cpp |
| `IWinCalloutBoxServices` | `src/sc4/WinCalloutBox.h` | src/sc4/WinCalloutBox.cpp | test fake only | scripts/tests/sc4_wincalloutbox_test.cpp |
| `IWinCompatHost` | `src/platform/WinCompat.h` | src/platform/WinCompat.cpp | test fake only | scripts/tests/rz_netsocket_test.cpp |
| `IWinFoldersHost` | `src/gz/WinFolders.h` |  | implemented | scripts/tests/gz_winfolders_test.cpp, src/gz/WinFolders.cpp |
| `IWinGenHost` | `src/gz/WinGen.h` | src/gz/WinGen.cpp | NOT IMPLEMENTED |  |
| `IWinGridHost` | `src/gz/WinGrid.h` | src/gz/WinGrid.cpp | test fake only | scripts/tests/gz_wingrid_test.cpp |
| `IWinLotConfigurationEditorHost` | `src/sc4/WinLotConfigurationEditor.h` | src/sc4/WinLotConfigurationEditor.cpp | test fake only | scripts/tests/sc4_winlotconfigurationeditor_test.cpp |
| `IWinMgrBaseHost` | `src/gz/WinMgrBase.h` | src/gz/WinMgrBase.cpp | test fake only | scripts/tests/gz_winmgrbase_test.cpp |
| `IWinTertiaryMenuFrameHost` | `src/sc4/WinTertiaryMenuFrame.h` | src/sc4/WinTertiaryMenuFrame.cpp | test fake only | scripts/tests/sc4_wintertiarymenuframe_test.cpp |
| `IWinTextEditHost` | `src/gz/WinTextEdit.h` | src/gz/WinTextEdit.cpp | test fake only | scripts/tests/gz_wincombo_test.cpp, scripts/tests/gz_wintextedit_test.cpp |
| `IWinTextTickerHost` | `src/gz/WinTextTicker.h` | src/gz/WinTextTicker.cpp | test fake only | scripts/tests/gz_winttextticker_test.cpp |
| `IZoneDeveloperBuildingDevelopmentApi` | `src/sc4/ZoneDeveloper.h` |  | test fake only | scripts/tests/sc4_zonedeveloper_test.cpp |
| `IZoneDeveloperCityApi` | `src/sc4/ZoneDeveloper.h` |  | test fake only | scripts/tests/sc4_zonedeveloper_test.cpp |
| `IZoneDeveloperHost` | `src/sc4/ZoneDeveloper.h` | src/sc4/ZoneDeveloper.cpp | test fake only | scripts/tests/sc4_zonedeveloper_test.cpp |
| `IZoneDeveloperLotDeveloperApi` | `src/sc4/ZoneDeveloper.h` |  | test fake only | scripts/tests/sc4_zonedeveloper_test.cpp |
| `IZoneDeveloperNetworkApi` | `src/sc4/ZoneDeveloper.h` | src/sc4/ZoneDeveloper.cpp | test fake only | scripts/tests/sc4_zonedeveloper_test.cpp |
| `IZoneDeveloperOccupantManagerApi` | `src/sc4/ZoneDeveloper.h` |  | test fake only | scripts/tests/sc4_zonedeveloper_test.cpp |
| `IZoneDeveloperSafeListApi` | `src/sc4/ZoneDeveloper.h` | src/sc4/ZoneDeveloper.cpp | test fake only | scripts/tests/sc4_zonedeveloper_test.cpp |
| `IZoneDeveloperService` | `src/sc4/ZoneDeveloper.h` |  | test fake only | scripts/tests/sc4_zonedeveloper_test.cpp |
| `IZoneDeveloperTerrainApi` | `src/sc4/ZoneDeveloper.h` |  | test fake only | scripts/tests/sc4_zonedeveloper_test.cpp |
| `IZoneDeveloperTerrainViewApi` | `src/sc4/ZoneDeveloper.h` |  | test fake only | scripts/tests/sc4_zonedeveloper_test.cpp |
| `IZoneDeveloperZoneManagerApi` | `src/sc4/ZoneDeveloper.h` |  | test fake only | scripts/tests/sc4_zonedeveloper_test.cpp |
| `IZoneManagerHost` | `src/sc4/cSC4ZoneManager.h` | src/gz/SimHostRegistry.h, src/sc4/cSC4ZoneManager.cpp | test fake only | scripts/tests/sc4_zonemanager_test.cpp |
<!-- generated:end -->
