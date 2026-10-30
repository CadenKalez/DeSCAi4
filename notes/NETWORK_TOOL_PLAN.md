# TOOL-1, core streets first (decided 2026-10-30)

Goal: zoning's auto-streets (cSC4ZoneDeveloper::DrawStreet -> cSC4NetworkTool::PlaceNetworkOccupants, flags 0xc49) place real
street occupants on land, so zoned lots get road access and the tract developer can build (TRACT-1). The drag road tool
(cSC4ViewInputControlNetworkTool) comes after, on the same core.

Scope rule: transcribe the land path faithfully; bridges, tunnels, highway intersections, one-way conflict resolution,
multi-type intersections, previews / highlights and the terrain-smoothing solver stay named stubs with their own to-dos.

Sizes are the original machine code (bytes). Order = dependency order.

## Phase A: foundation classes (none of them is in the ledger yet)
| class | functions | bytes |
|---|---|---|
| cSC4NetworkWorldCache (tool +0x30: per-cell cSC4NetworkCellInfo map with Push / Pop) | 10 | 1854 |
| cSC4TileInfo | 4 | 386 |
| cSC4TileEvaluator (the RUL tile-rule lookup) | 6 | 3070 |
| cSC4NetworkTileNeighborhood | 4 | 376 |
| cSC4NetworkTypeInfo (LoadTileConflictRules, the per-type record) + SC4NetworkLoadOverrideMatrix | 12 | 5310 |
| cSC4NetworkTool::ReloadRules / ReloadNetworkTypeInfo (empty / partial today) | 2 | ~2100 |

## Phase B: cell infos and the solver
CreateCellInfosForCurrentPlacementTiles 960, ComputeAndStoreCellInfo 716, CreatePlaceholderCellInfo 508, AssignEdgeMajorTypes 818,
AddGroupingInfo 380, DoAdditionalDraws 252, BuildSetOfCellsToBeSolved 1590, ExpandToSolveSetBasedOnConnectivity 1058,
MarkNeighborConnections 200, SolveNetwork 694, AddToSet 584, AdjustTileSubsets 1488, PatchTilePair 326, AssignEdgeSubtypes 1000,
AssignSimpleEdgeSubtypes 554, AssignComplexEdgeSubtypes 536, MarkExtendingDiagonalEdges 446, BuildPaddedNetworkSet 770,
SolveUsingEvaluator 674, ApplyNetworkRules 1942, VerifyTileSubsets 1288.

## Phase C: the drag, the placement and the occupants
PlaceNetworkOccupants 2040, GetNetworkOccupant 152, DrawNetworkLine ~970, ComputeDraggedCells ~4150, PlaceNetwork 2286,
DoNetworkSpecificPlacement, IdentifyStartingConditions (partial), CheckForReservedTiles x2 (~690), CheckForGoodTerrain 242,
StartPlacing, UpdateTerrainHeights, MarkPositionChanged / MarkTerrainChangedExistingOccupantsAsNewlySolved (~980),
InsertOccupantsForSolvedCells (partial), InsertNetworkOccupant ~3400, CreateNetworkOccupant 778, SetExemplarForOccupant 176,
DetermineSupportType 242, cSC4NetworkConstructionCrew::MarkOccupantsUsable.

## Stubs kept (each its own to-do when the core works)
Bridges (IdentifyBridgeCells, IdentifyLandBridgeCells, InsertBridgePieces ...), tunnels, highway intersections, ResolveOneWayConflicts,
SolveMultiTypeIntersections, SmoothenNetwork / ComputeInitialSmoothingSet and the constraint inserters (flat-land assumption),
previews and highlights, the construction crew's animation.

## Progress
- cSC4NetworkWorldCache recovered (src/sc4/NetworkWorldCache.cpp, test sc4_networkworldcache); cSC4NetworkCellInfo's constructor
  default (solvedCellIndex -1) and RecomputeEdgesFromAllTypes. Not yet returned by the native INetworkToolHost::CreateWorldCache.
- The tile-rule engine (src/sc4/TileRules.cpp, test sc4_tilerules): cSC4FileTokenizer (text through SetTileRuleResourceLoader
  instead of SC4GetResourceAsFile), cSC4TileInfo, tCompactNeighborInfo (its rotate / mirror were not decompiled: transcribed from the
  disassembly), cSC4TileRule, cSC4TileNeighborhood, cSC4TileEvaluator (incl. TransmogrifyRuleSet).
- The per-type tile evaluators (NetworkTileEvaluators / LoadNetworkTileEvaluators: the evaluator half of ReloadNetworkTypeInfo).
  Rule files: type 0x0a5bcf4b, group 0xaa5bcf57, instances 1..24 (per type complex then simple; street = 10 / 9), recovered from
  SC4NetworkTool.cpp's static initializer 0x10019256a. Checked offline against the game data: all 24 files parse (street simple 412
  rules, street complex 326).
- Next: install the rule loader natively (SL::spRM bytes) and call LoadNetworkTileEvaluators at start-up; cSC4NetworkTileNeighborhood
  (reads the tool's cell infos); then phase B. The rest of cSC4NetworkTypeInfo (bridges, supports, placement params, tile
  conflict / pattern verifier rules) stays host-provided until needed.
