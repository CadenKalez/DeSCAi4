# Road tool: what the original does and what is missing (2026-10-29)

Call chain (decompiled, nothing implemented yet):

1. **Catalog button -> `cSC4View3DWin::InvokeCatalogItemCommand` (0x10078410a), item family 0x4a2b2467.** The port's version only knows the
   landscape and zoning families and forwards the rest to the host. The network branch (needs `GetCityInitialized` = spCity vslot 0xb0, then
   closes the menu when `closeMenu`) creates `new cSC4ViewInputControlNetworkTool(type)` (0x88 bytes) and calls `SetCurrentViewInputControl(tool, 2)`:

   | item id | network type | item id | network type |
   |---|---|---|---|
   | 0x8c329937 | 0 (road) | 0x2b730c5b | 6 |
   | 0x29 | 1 | 0x2b79e398 | 8 |
   | 0x31 | 2 | 0x2be099a9 | 0xb |
   | 0xea6e379d | 3 | 0x4be099de | 9 |
   | 0x2992f72c | 4 | 0xbe099c1 | 10 |
   | 0x32 | 5 | 0xbe0998f | 0xc |
   | 0x299237bf | 7 | 0x6a47xxxx | `cSC4ViewInputControlNetworkIntxTool(id & 0xffff)` (0xe8 bytes) |

2. **`cSC4ViewInputControlNetworkTool`** (16 functions, 0x10038dea0..0x10038e8b6; fields: +0x48 terrain view, +0x50 cell size, +0x54 last point,
   +0x60 "local_tile_outline" effect, +0x68 cSC4LocalGridHelper (recovered), +0x70 network tool manager entry = `NetworkManager::GetNetworkTool(type, true)`
   (recovered), +0x78 the `cSC4NetworkToolUI` obtained from it (vslot 0x30), +0x80 type, +0x84 dragging). Init registers message 0x09c8b8ea; the mouse
   handlers forward to the UI object (vslots 0x48 down, 0x50 up, 0x58 move, 0x60 cancel, 0x68 is-active) and set the cursor from it (0x28).
3. **`cSC4NetworkToolUI`** (35 functions, 0x10071ff90..0x1007229be): the drag logic, `ScreenToCell`, `UpdateAnchorPoint`, `TestPlacement`,
   `DoPlacement`, bridges list, button handling. It drives `cSC4NetworkTool` through the slots the port's `cISC4NetworkTool` interface still omits:
   `GetUITool` [6], `PlaceNetworkOccupants` [7], `PlaceNetworkOccupantById` [8], `ClearCellPreviews` [16], `ClearOccupantHighlights` [17],
   `DrawNetworkLine` [18], `PlaceAllSegments` [19], `GetCostOfSolution` [20].
4. **Unrecovered neighbours the tool needs** (from `notes/remaining_functions.csv`): `cSC4NetworkPreview` 16, `cSC4NetworkWorldCache` 9,
   `cSC4NetworkTypeInfo` 11, `cSC4NetworkCellInfo` 2, `cSC4NetworkTileNeighborhood` 4, `cSC4NetworkConstructionCrew` 33 (the animated builders),
   `cSC4NetworkOccupantModelInstance` 9, `nSC4Networks::cIntRule` 10, the intersection tool 12. `cSC4NetworkTool` itself has 144 recovered-partial
   rows whose evidence names these.
5. **Native side:** `InstallNetworkToolStub()` (`src/app/hosts/StubNetworkTool.cpp`) answers every `INetworkToolHost` call with a neutral stub.

Suggested order: (a) the small value classes (CellInfo, TypeInfo, TileNeighborhood), (b) WorldCache + Preview, (c) the tool's omitted slots,
(d) `cSC4NetworkToolUI`, (e) `cSC4ViewInputControlNetworkTool`, (f) the View3DWin network branch + a scenario step
`input road0` + `drag@...`, (g) the construction crew (roads appear animated). Each step has its own test with fakes.
