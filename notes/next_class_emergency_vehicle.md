# Next class to recover: cSC4EmergencyVehicle (+ Ambulance, GetawayVan, CarjackedVehicle, MayorLimo)

Prepared 2026-10-29 while sizing "10 % of the remaining functions". Nothing here is implemented yet.

## Why this one
`cSC4EmergencyVehicle` (35 remaining functions, 0x1002f5580..0x1002f80d8 plus two at 0x10050f2d8 / 0x10050f550) is the base
of four small subclasses that are all still missing: `cSC4Ambulance` 8, `cSC4GetawayVan` 7, `cSC4CarjackedVehicle` 7 and
`cSC4MayorLimo` 12. Together that is about 70 functions on top of the already recovered `cSC4Vehicle` / `cSC4Automaton`.

## Layout seen so far (object derived from cSC4Vehicle)
+0x424..+0x448 zero-initialised block (call number, dispatch state, flags) · +0x430 and +0x440 ref-counted objects released in
Shutdown · +0x438 u8 repath counter · +0x43c u32 dispatch state (3 = en route to the site, 1 = at the site) · +0x44x trailing
bytes. The real field names come from Write/Read (0x1002f7f02 / 0x1002f7fbc): read those first.

## Automaton slots the decompile calls (resolved with scripts/vtable.py cSC4EmergencyVehicle)
0x038 GetPosition · 0x068 Remove(bool) · 0x0f8 CheckAutomataFlag(mask) · 0x120 SetSpeed(float, float) · 0x1c0 GetState ·
0x2b0 HandleReachedRouteDestination · 0x2e0 GetMarkedForRemove · 0x2f0 GetReachedDestination · 0x2f8 GetSpecialRouteFlag ·
0x458 SetPath · 0x4d0 RemoveSurroundingAutomata · 0x4d8 FindLeader · 0x560 ChooseNewDestination · 0x648 ShowEmergencyLights ·
0x650/0x658/0x660/0x668 the four tick handlers DoEnRouteTick / DoRepathingTick / DoHandlingEventTick / DoTeleportTick
(state ids 100, 0x65, 0x66, 0x67). Full slot list: `python3 scripts/vtable.py cSC4EmergencyVehicle`.

## Services it reaches (become IEmergencyVehicleHost seams)
`SL::spSS` (sound system, vslot 0x50 with message ids 0x6a451908 / 0xca45190f), `SL::spCity` (vslot 0x2f8 position -> cell,
0x140 network manager, 0x158 dispatch/route service with vslot 0x90 "find route"), the message server (path-blocked message
0xca8dcdb4 with the call number, the vehicle's repath counter and its type) and the dispatch manager.

## Float / SSE
DoTeleportTick (0x1002f5d08) blends two speeds with a clamp (`~-(fVar<0) & min(fVar,1)`) and compares a distance with
`DAT_100b219f4 << 4`: read the constants from the binary (`scripts/macho_read.py`) and check against the disassembly.

## Order of work
1. Write/Read + the getters/setters (call number 0x10050f2d8, dispatch destination 0x10050f550).
2. The tick handlers (DoStateTick dispatch, DoEnRouteTick, DoRepathingTick, DoTeleportTick, DoHandlingEventTick).
3. HandleEnterState / HandleExitState / HandleReachedRouteDestination / HandleNetworkOccupantRemoved.
4. HandleDispatched and AppendRouteToNearbyRoad (0x1002f64bc, about 4.5 KB) last.
5. The four subclasses.
Use the machine-code oracle for the float parts, a unit test with a fake host for the state machine.
