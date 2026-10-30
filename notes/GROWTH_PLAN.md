# Zone growth: the chain and the plan (mapped 2026-10-30)

Goal: a zoned, empty lot (now created natively, see SESSION_HANDOFF addendum 12) gets a building.

## The chain in the original
1. **cSC4BuildingDevelopmentSimulator** (plain class, BuildingDevelopmentSimulator.cpp) is a simulator agent.
   `DoMessage` (0x10014e84c): 0x66956814 daily -> `DoIdle` + `DoDailySimulation`; 0x66956816 / 0x66956810 monthly ->
   `DoMonthlySimulation`; 0x6695681a idle -> `DoIdle`; 0xe8bda2e7 building changed; 0x278e6003 terrain changed;
   0x99ef1142/43 civic lots (families 0x150a/0x150b) tracked in a list at +0x88.
   - `DoMonthlySimulation` (0x10014ee44): construction resources = clamp((R+C+I demand of SL::spDemandSim 0x1001/0x3001/0x4001)
     * +0x1a4, +0x19c, +0x1a0) -> resources vslot 0x90; every growth developer vslot 0xd8 (its monthly update); stage := 1.
   - `DoIdle` (0x10014ecba): stage 1/2 walks the developers calling vslot 0xe0 (map update, may need several idles);
     stage 3 -> `PlanGrowth` when +0x1c8; stage 4 -> `PlanDecline`; stage 0.
   - `DoDailySimulation` (0x10014edcc): `Grow` (or `ExecuteGrowthPlan` when +0x1c8 planned mode) then `ExecuteDeclinePlan`.
   - `Grow` (0x10014fb24, 1194 bytes): weighted random pick of a developer (weight = its demand vslot 0x90 -> 0x60, when > 5,
     + +0x1a8), its growth list (vslot 0x58) -> first tract not marked (`IsTractUnmarked`, `MarkTract`), then the
     **tract developer** (+0xd0) vslot 0x28 Grow(outcome{developer, x, z, style}, list, flag) > 0 -> last build day.
   - `PlanGrowth` (0x10015017e, 1117) / `ExecuteGrowthPlan` (0x10014f8ec, 543) / `PlanDecline` (0x10015060c, 857): the
     planned-mode variant (plans in the list at +0xf8, profiles at +0x158).
   **Port status:** the catalogue / profiles / resources / decline execution exist; DoMessage, DoIdle, Daily, Monthly, Grow,
   PlanGrowth, ExecuteGrowthPlan, PlanDecline are NOT in the code although the ledger marks them recovered (LEDGER-BDS).
   The class takes its developers / tract developer / placement / resources / suitability through injected interfaces
   (IGrowthDeveloperRef, ITractDeveloperRef, IBuildingPlacementServices, IConstructionResources, IConstructionSuitabilityHost):
   nothing natively plugs them in.
2. **cSC4GrowthDeveloper** (GrowthDeveloper.cpp, recovered, 46 functions): one per developer type (exemplar property 0x21),
   created by the BDS `CreateDeveloper` through IBuildingPlacementServices::CreateGrowthDeveloper. Its host
   (src/app/hosts/StubGrowthDeveloper.cpp) is neutral: city, land value, traffic, zone manager, residential, commercial,
   pollution, aura, crime, demand, neighbours APIs all null. Its maps (desirability, capacity) come from those simulators.
3. **cSC4TractDeveloper** (TractDeveloper.cpp, mostly recovered: Grow, Build, PickBuilding, candidate lots, aggregation,
   subdivision). Host StubTractDeveloper.cpp (21 methods) is neutral. It chooses the lot configuration and calls the lot
   developer.
4. **cSC4LotDeveloper** construction (StartConstruction 0x1001434f8, ConstructDefaultLot 0x100143ebc, props, foundations,
   business units, ~90 functions): **no code** in LotDeveloper.cpp although the ledger marks them recovered-partial
   (LEDGER-LOTDEV). The BDS's AddNewBuildingOccupant / AddNewFoundationOccupant exist.
5. Simulators feeding the maps, not initialised natively: LandValueSimulator (probe host), TrafficSimulator, ResidentialSimulator,
   PollutionSimulator, CrimeSimulator, PowerSimulator (buildings need power to grow past stage 1), PlumbingSimulator.

## Order of work (each layer runs and is committed before the next)
1. BDS DoMessage / DoIdle / DoDaily / DoMonthly + agent registration (intervals from the original Init) + a native
   IBuildingPlacementServices that creates real cSC4GrowthDeveloper objects; developers created from the exemplars.
2. Growth developer host: the APIs whose simulators run (zone manager, demand, city, aura, budget) real; the rest neutral
   but non-crashing. Check that its monthly / map updates run.
3. BDS Grow (and the planned-mode trio) + ITractDeveloperRef adapter to cSC4TractDeveloper + its host.
4. cSC4LotDeveloper::StartConstruction and the construction / building occupant creation it needs.
5. Residential simulator Init + power (so a built lot is occupied and can grow beyond stage 1).

Done so far: lot configuration hosts (1,908 lot configurations load); demand host (6d6df12): Grow draws R$ and calls the tract
developer ~350 times in a month, which builds nothing because cSC4TractDeveloper::Grow's pass loop and its candidate / pick / build
functions are `return 0` PARTIAL stubs (TRACT-1: 0x100136070 loop, 0x1001369ae, 0x1001377a0, 0x100137b04, 0x100137f44,
0x1001388c4, 0x10013845e, 0x100138b10, 0x1001392fa, 0x100139dd2...). Next: recover them (OpenSC4 docs/tract.md describes them);
layer 3 (BDS Grow, the tract developer's native host; in the city Grow draws no developer because the demand
simulator's channels are empty: its host StubDemandSimulator is neutral, so no tunables / channel values: DEMAND-1); layer 2 (the growth developers' native host; PlaceZone's 0x07699455 zone-change message now sent, so zoning
fills the developers' zone cell counts: a residential zone gives R$ a 32-tract growth list); layer 1 (c625295); zoning makes lots (b3dfaa8); the component init unwraps PlainSerializableAdapter so the BDS, the lot
configuration manager and the prop manager Init (0e2db86).

## Cross-reference: OpenSC4 (https://github.com/danhere/opensc4, LGPL-2.1)
An in-process reimplementation of SC4 Deluxe **Windows build 1.1.641** (hooked into the running exe, each function checked against
the original by a parity harness; 7,517 functions, "nearly all simulators"). Its addresses (data/addresses/*.toml) are Windows
addresses and do not apply to our macOS binary, and its classes use the Windows object layouts, but the game logic is the same
source. Its file header comments describe each function's behaviour and the vtable slots it uses. Relevant files:
`src/core/development_{object,messages,queries,occupants}.cpp` (cSC4BuildingDevelopmentSimulator: Grow, planner),
`growth_{object,messages,queries}.cpp` (cSC4GrowthDeveloper), `lot_{object,build,construct,terrain}.cpp` (cSC4LotDeveloper,
construction = layer 4), `citylot_*` (cSC4Lot), `bldgocc_sim.cpp` (building occupant), `lotconfig_*`, `residential`, `power`.
Use: read its description of a function, then transcribe from **our** disassembly / decompile (the truth source for the macOS
layout) and check that both agree. Copying its code needs LGPL-2.1 terms (compatible with this repo's GPL-3.0 through LGPL 2.1
section 3, keeping its notices); the default here is reference only.

Local copy: `scripts/fetch_opensc4_ref.sh` puts all 101 of its docs and its 55 save-record schemas in `work/opensc4_ref/`
(git-ignored). Per layer: layer 2 = docs/growth.md (the 20-state map update, the tuning, the save record: version 1, five grid
references, all 12 developers in one record) and docs/demand.md, landvalue.md, traffic.md; layer 3 = docs/development.md and
tract.md; layer 4 = docs/lot.md, citylot.md, bldgocc.md, constrocc.md, foundocc.md; layer 5 = residential.md, power.md. Saves (SAVE-1):
docs/save-format.md and save_schemas/*.py decode every city record (the format is the same on macOS).
