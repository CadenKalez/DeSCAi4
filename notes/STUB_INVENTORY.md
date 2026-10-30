# Stub and partial inventory (measured 2026-10-30, commit 7144c29)

Every playability failure found on 2026-10-30 traced to one of these: a neutral stub host, an unset hook, or a component with no
recovered Init. The recovered game code itself was mostly faithful.

| What | Count | Where |
|---|---|---|
| Generated stub host files | 95 | `src/app/hosts/Stub*.cpp` |
| Stub host methods | 1266 (1230 return null / 0 / false / nothing) | same |
| City-probe seams still used by the game | 341 | `src/tools/city_probe_bindings.cpp` (`Seam(...)`) |
| `SC4_UNWIRED` sites in the native app | 34 | `src/app/**` |
| City components with no recovered Init (natively) | ~20 | `src/app/NativeCityComponents.cpp` (Power, Plumbing, Residential, Traffic, LotDeveloper, Crime, Fire, Police, Pollution, Flora, ...) |
| Ledger functions `recovered-partial` / `partial` / `pending` | 1646 / 128 / 23 | `notes/ledger.csv`, 163 classes |
| Ledger functions `skipped` | 374 | mostly Windows-only / tools / editors |
| Functions never started | 4386 in 549 classes | `notes/remaining_functions.csv` |

Partials by class (top): cGZGraphicSystem 201, cSC4NetworkTool 144, cSC4PowerPoleOccupant 110, cSC4LotDeveloper 102,
cSC4TrafficSimulator 73, cSC4ResidentialSimulator 70, cSC4CitySituationManager 67, cSC4FloraSimulator 45, cSC4PowerSimulator 42.

## The critical path to "playable"
1. Plop a building: StubPlaceLot (26 methods, 14 sub-services) -> lot manager, lot configuration manager Init, zone manager,
   budget; TOOL-5.
2. Zones grow: cSC4LotDeveloper (102 partial), cSC4ResidentialSimulator (70), BuildingDevelopmentSimulator Init, the growth /
   prop developer hosts (prop developer agent left unregistered: its LotManager() is a stub and UpdateTract crashed).
3. Utilities: cSC4PowerSimulator (42) + PowerPoleOccupant (110), plumbing.
4. Roads: cSC4NetworkTool (144 partial) + cSC4ViewInputControlNetworkTool + cSC4NetworkToolUI (TOOL-1); traffic simulator (73).
5. Save: SAVE-1.

Rule while fixing: never register a simulator agent / enable a tool whose host is still neutral; fix the host first.
