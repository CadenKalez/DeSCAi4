// cSC4AuraSimulator — the city's "aura" simulator: the per-cell value the occupants, the query tool and the
// advisors read (police/fire/education/health coverage feel, park and landmark effects, tax-rate mood).
// Recovered from Sim City 4 Deluxe Editionsub 1.2.1 (0x10021801e..0x10021d36a, class id 0x0990bdd8, 0x208 bytes).
//
// It is one COM object with three views (primary vtable 0x100a86fb0):
//   +0x00 cISC4AuraSimulator   iid 0xc773e880 (and iid 1)
//   +0x08 cIGZMessageTarget2   iid 0x452294aa
//   +0x10 cIGZSerializable     iid 0xe4fda3d4
// One reference count at +0x1c is shared by the three views (the AddRef/Release thunks for the +8 / +0x10 views
// adjust their `this` and touch the same field).
//
// The per-tick work is a 0..0x11 state machine (DoIdle) that runs one stage per idle message, so a whole aura cycle
// is spread over many idle callbacks:
//   1 start (remember the simulator date)      2 park map = park effect grid * park department funding
//   3 walk the per-landmark maps (+0x90)       4 reset the cell grid (+0x60) and the lot iterator
//   5 stamp every lot's base aura into +0x60   6..0xd fold the park map, the four pollution maps, the two
//     residential maps, the crime map, the two traffic maps and the vehicle aura (+0x78) into +0x60 through
//     their response curves; every source is a tract grid, the destination is a cell grid
//   0xe smooth +0x60 into the persistent aura grid (+0x68)      0xf publish +0x58 = +0x68 + transient (+0x70)
//   0x10 build the hotspot list and maybe start a riot          0x11 decay the transient grid, post the cycle message
//
// Original layout (0x208 bytes):
//   +0x18 initialised byte            +0x1c reference count
//   +0x20 cRZRandom (0x18)            +0x38 cISC4City        +0x40 cIGZMessageServer2
//   +0x48 cSC4BuildingDevelopmentSimulator (stored only)      +0x50 tract size (fixed 4, not a property)
//   +0x58 cSC4SimGrid<int8>   the published aura, id 0x69d5b600, initial 0
//   +0x60 cSC4SimGrid<int8>   the per-cycle cell work grid, id 0x69d5b606, tract size 1, cleared to -128
//   +0x68 cSC4SimGrid<int8>   the smoothed ("history") aura, id 0x69d5b604, initial -128
//   +0x70 cSC4SimGrid<int8>   the transient aura (AddTransientEffect), id 0x69d5b605, initial 0
//   +0x78 cSC4SimGrid<int8>   the vehicle aura (occupant property 0xca5b9305), id 0x49d5b607
//   +0x80 cSC4SimGrid<int16>  the raw park effect (property 0x27812850), id 0x2a674ca5
//   +0x88 cSC4SimGrid<int16>  the park map the growth developer reads, id 0x49d5b602
//   +0x90 std::map<uint32, cSC4SimGrid<int16>*> the per-department landmark maps (property 0x2781284f)
//   +0xa8 cSC4SimGrid<int16>  the published landmark map, id 0x8a674caf
//   +0xb0 cSC4SimGrid<int16>  the landmark work grid, id 0x8a674caf
//   +0xb8 int idle state               +0xbc int simulator date the idle started
//   +0xc0 cSC4SafeListIterator<cISC4Lot*> the lot pass iterator (released by Shutdown)
//   +0xc8 uint32 the landmark map key being walked (0xffffffff = none)
//   +0xd0 int saved and reset every cycle (meaning not identified)
//   +0xd4 int the mayor rating (population-weighted average aura)
//   +0xd8 int the ordinance 0xaa5b8407 value (added to every cell by the smoothing pass)
//   +0xe8 float[12] the last tax rate seen per economy type (initial -1.0)
//   +0x118 std::vector<tHotspot> this cycle's raw hotspots (count at +0x120)
//   +0x130 std::list<tHotspot> the merged hotspot list (count at +0x140)
//   +0x148 bool re-add every lot's landmark effect on the next 0x26d31ec1 message (set by Read of an old save)
//   +0x150 park curve        +0x158 std::vector<cSC4ResponseCurve*> the four pollution curves
//   +0x170 residential 0x138 curve   +0x178 residential 0x140 curve   +0x180 crime curve
//   +0x188 traffic trip-length curve +0x190 traffic congestion curve    +0x198 tax-rate-change curve
//   +0x1a0 land-value curve (added to the lot base aura)                +0x1a8 float aura smoothing (0.6)
//   +0x1ac float transient decay (0.4)                                  +0x1b0/+0x1b4/+0x1b8 int base aura per aura type
//   +0x1bc float[4] transient radius   +0x1cc float[4] transient magnitude (properties 0xea023290..93)
//   +0x1dc int hotspot merge distance (8)   +0x1e0 int hotspot value threshold (196)
//   +0x1e4 int hotspot minimum count (16)   +0x1e8 cSC4ResponseCurve riot chance (property 0xea0232a1)
//   +0x1f0 int max rating cell x, +0x1f4 z, +0x1f8 int16 max rating
//   +0x1fc int min rating cell x, +0x200 z, +0x204 int16 min rating
//
// Quirks kept:
//   * Init sets a file-scope "static initialised" byte that nothing reads (0x100b246fc, 0x10021843d).
//   * GetMayorRating / GetMaxMayorRating / GetMinMayorRating read only the low byte of an int / int16 field
//     (0x10021b376, 0x10021d2fe, 0x10021d316).
//   * The constructor lays out the four pollution-curve slots the original creates in Init (0x10021801e allots the
//     empty vector, 0x100218454 resizes it); a re-read never leaves them empty either.
//   * The smoothing pass rounds `delta * (1 - +0x1a8)` *away from zero* (the float magic at 0x10021a872 /
//     0x10021a9af is the original compiler's round-to-nearest conversion, so any non-zero delta moves at least
//     one unit), clamps to -127..127, writes -128 for a tract with no valid source cell, and leaves a cell whose
//     delta is 0 untouched.
//   * CheckForStartingRiot consumes one uniform double even when there is nothing to do (0x10021d174), and the
//     riot starts at the *tract centre* of the hotspot (cellX * tractSize + tractSize / 2).
//   * The landmark map key is 0, or the lot's x0 when the building's department 0xaa59670c is funded; that same
//     value is also the key of the budget query and the instance id of a map created on the fly
//     (0x10021b01c..0x10021b03f). With no lot the original reads an uninitialised stack slot there; the port
//     uses 0.
//   * Read of a version-8 save reads one grid, releases it and sets +0x148 (0x10021a0xx); versions below 8 fail.
//   * Write stores +0x1f8/+0x204 as 16-bit integers but the getters read a signed byte.
//   * The lot pass's "check the clock every 1024" test always fires (the counter steps by 0x400 and the mask is
//     0x3ff, 0x10021a4d3).
//   * The 0x26d31ec1 refresh removes and then re-adds each lot's landmark effect, so on a map that does not already
//     hold the value the pair cancels out (0x10021a159 / 0x10021a16a).
//   * DoMessage 0x66956814 answers "not handled" when the state machine lags more than the days since the idle
//     started, so the message server retries it (0x10021a012).
//
// The neighbours this class reaches through the SL::sp* globals are reached through IAuraHost, one method per
// original call with its vtable slot. cSC4SimGrid<T>, cSC4ResponseCurve, cSC4Lot and cISC4Occupant are used
// directly (all recovered). This class is what IGrowthAuraApi (growth developer: transient aura 0x68, park map
// 0x70, landmark map 0x78) and IQueryAuraSim (view query: GetAura 0x28) were waiting for.
#pragma once
#include "sc4/CommonIIDs.h"

#include <cstdint>
#include <list>
#include <map>
#include <vector>

#include "gz/cIGZMessage2.h"
#include "gz/cIGZSerializable.h"
#include "gz/CommandParameterSet.h"
#include "rz/cRZRandom.h"
#include "sc4/GameDataRegistry.h"
#include "sc4/Lot.h"
#include "sc4/Occupant.h"
#include "sc4/cSC4ResponseCurve.h"
#include "sc4/cSC4SimGrid.h"

namespace sc4 {

class cSC4AuraSimulator;
class cISC4AuraSimulator;

constexpr std::uint32_t kAuraSimulatorClassId = 0x0990bdd8;
constexpr std::uint32_t kGZIID_cISC4AuraSimulator = 0xc773e880;
constexpr std::uint32_t kGZIID_cISC4AuraSimulatorSerializable = 0xe4fda3d4;

// The simulator exemplar: {type 0x6534284a, group 0xe7e2c2db, instance 0xaa023079}, loaded with iid 0xa52160f5.
constexpr std::uint32_t kAuraExemplarType = 0x6534284a, kAuraExemplarGroup = 0xe7e2c2db, kAuraExemplarInstance = 0xaa023079;
constexpr std::uint32_t kAuraExemplarPropertyIid = 0xa52160f5;

// Exemplar properties read by ReadSimulatorTunableValues (all on the simulator exemplar above).
constexpr std::uint32_t kAuraPropertyParkCurve = 0xea023270;          // -> +0x150
constexpr std::uint32_t kAuraPropertyPollutionCurve0 = 0xea023271;    // ..0xea023274 -> +0x158[0..3]
constexpr std::uint32_t kAuraPropertySmoothing = 0xea02327a;          // float -> +0x1a8 (default 0.6)
constexpr std::uint32_t kAuraPropertyTransientDecay = 0xea02327b;     // float -> +0x1ac (default 0.4)
constexpr std::uint32_t kAuraPropertyBaseAuraResidential = 0xea02327c;   // int -> +0x1b0
constexpr std::uint32_t kAuraPropertyBaseAuraCommercial = 0xea02327d;    // int -> +0x1b4
constexpr std::uint32_t kAuraPropertyBaseAuraIndustrial = 0xea02327e;    // int -> +0x1b8
constexpr std::uint32_t kAuraPropertyResidentialCurve138 = 0xea023275;   // -> +0x170
constexpr std::uint32_t kAuraPropertyResidentialCurve140 = 0xea023276;   // -> +0x178
constexpr std::uint32_t kAuraPropertyCrimeCurve = 0xea023277;            // -> +0x180
constexpr std::uint32_t kAuraPropertyTrafficTripCurve = 0xea023278;      // -> +0x188
constexpr std::uint32_t kAuraPropertyTrafficCurve = 0xea023279;          // -> +0x190
constexpr std::uint32_t kAuraPropertyTaxChangeCurve = 0xea02327f;        // -> +0x198
constexpr std::uint32_t kAuraPropertyLandValueCurve = 0xea023280;        // -> +0x1a0
constexpr std::uint32_t kAuraPropertyTransientEffects = 0xea023290;      // ..93: float{radius, magnitude} -> +0x1bc/+0x1cc
constexpr std::uint32_t kAuraPropertyHotspotSettings = 0xea0232a0;       // u32{threshold, distance, minCount}
constexpr std::uint32_t kAuraPropertyRiotCurve = 0xea0232a1;             // -> +0x1e8

// Occupant/building properties this simulator reads.
constexpr std::uint32_t kAuraPropertyParkEffect = 0x27812850;        // int32{value, radius in cells}
constexpr std::uint32_t kAuraPropertyLandmarkEffect = 0x2781284f;    // int32{value, radius in cells}
constexpr std::uint32_t kAuraPropertyVehicleAura = 0xca5b9305;       // int32{value, radius in cells}
constexpr std::uint32_t kAuraPropertyNoAuraBuilding = 0x029244db;    // a holder with this property is skipped

// Budget / department ids and the messages the class subscribes to.
constexpr std::uint32_t kAuraParkDepartment = 0x8a4260a6;    // budget slot 0xd8 -> funding
constexpr std::uint32_t kAuraDepartmentProperty = 0xaa59670c;   // budget slot 0x198 (is this occupant funded?)
constexpr std::uint32_t kAuraOrdinance = 0xaa5b8407;            // ordinance slot 0xc0(id, 0)
constexpr std::uint32_t kAuraHistoryMayorRating = 0x0a5cbf37;   // history warehouse slot 0x60
constexpr std::uint32_t kAuraRiotType = 0xca380b06;             // city slot 0x240 -> 0x38(type)

constexpr std::uint32_t kAuraMessageCycleEnd = 0x2a09b21a;         // posted through the message server
constexpr std::uint32_t kAuraMessageDay = 0x66956814;
constexpr std::uint32_t kAuraMessageMonth = 0x66956816;
constexpr std::uint32_t kAuraMessageIdleTime = 0x6695681a;
constexpr std::uint32_t kAuraMessageOrdinanceChanged = 0xc2c09554;
constexpr std::uint32_t kAuraMessageReloadTunables = 0xc53d10aa;   // data1 0xe7e2c2db, data2 0xaa023079
constexpr std::uint32_t kAuraMessageLotsChanged = 0x26d31ec1;
// The two occupant messages (cISC4Occupant inserted / removed from the manager).
constexpr std::uint32_t kMessageOccupantInsertedAura = 0x99ef1142, kMessageOccupantRemovedAura = 0x99ef1143;

// The vehicle occupant type whose move adds a transient aura blob (0xc772bf98).
constexpr std::uint32_t kAuraVehicleOccupantType = 0xc772bf98;

// Grid instance ids.
constexpr std::uint32_t kAuraGridInstance = 0x69d5b600, kAuraCellGridInstance = 0x69d5b606;
constexpr std::uint32_t kAuraSmoothedGridInstance = 0x69d5b604, kAuraTransientGridInstance = 0x69d5b605;
constexpr std::uint32_t kAuraVehicleGridInstance = 0x49d5b607, kAuraParkEffectGridInstance = 0x2a674ca5;
constexpr std::uint32_t kAuraParkGridInstance = 0x49d5b602, kAuraLandmarkGridInstance = 0x8a674caf;

// cISC4AuraSimulator::TransientAuraEffect: the four blobs of properties 0xea023290..0xea023293.
enum TransientAuraEffect : std::int32_t {
    kTransientAuraEffect0 = 0,
    kTransientAuraEffect1 = 1,
    kTransientAuraEffect2 = 2,
    kTransientAuraEffect3 = 3,     // the one a moving 0xc772bf98 occupant adds
};

// cISC4AuraSimulator::eStrikeBuster (BustStrike's argument, from the Lua command "end_strike").
enum eStrikeBuster : std::int32_t {
    kStrikeBusterAll = 0,
    kStrikeBusterPolice = 1,
    kStrikeBusterFire = 2,
    kStrikeBusterHealth = 3,       // residential vslot 0x48 EndHealthStrike
    kStrikeBusterEducation = 4,    // residential vslot 0x40 EndSchoolStrike
};

// One hotspot: a tract cell plus how many raw hotspots were merged into it (0xc bytes).
struct tHotspot {
    std::int32_t x = 0;       // +0x00 tract x
    std::int32_t z = 0;       // +0x04 tract z
    std::int32_t count = 0;   // +0x08 merged count
};
static_assert(sizeof(tHotspot) == 0xc, "tHotspot must stay 0xc bytes");

// The original's SC4Point<int> (the mayor rating cell).
struct AuraPoint {
    std::int32_t x = 0;
    std::int32_t z = 0;
};

// One entry of sDeveloperTypeToBaseAuraMap (0x100b038d0, stride 0x14, three entries then a type-0 terminator).
// +1 is the aura type (0 residential, 1 commercial, 2 industrial: the base-aura tunable to use), +2 the economy
// type (0..11, the index of the tax-rate array), +3 the base aura and +4 the current aura (base + tax effect).
struct AuraTypeEntry {
    std::uint32_t type = 0;      // +0x00
    std::int32_t auraType = 0;   // +0x04
    std::int32_t economyType = 0;// +0x08
    std::int32_t baseAura = 0;   // +0x0c
    std::int32_t currentAura = 0;// +0x10
};
static_assert(sizeof(AuraTypeEntry) == 0x14, "AuraTypeEntry must stay 0x14 bytes");
constexpr int kAuraTypeEntryCount = 3;
AuraTypeEntry* AuraTypeTable();   // the three entries (the terminator's type is 0)

// cSC4MapUtilities::AddRadiusEffect, signed-char instantiation @ 0x1000d7292 (the int16 one lives in
// ProtectionSimulator.h, the uint8 one in CrimeSimulator.h). Linear falloff peak -> edge over `radius`, adding to
// the cells and clamping to -128..127.
void AddRadiusEffect(std::vector<std::vector<std::int8_t>>& grid, int width, int height, int cx, int cz,
                     float radius, float peak, float edge);

// ---------------------------------------------------------------------------------------------------
// Services: one small view per original global (AddRef/Release mirror the COM calls the original makes).
// ---------------------------------------------------------------------------------------------------
class IAuraService {
public:
    virtual ~IAuraService() = default;
    virtual void AddRef() {}       // cIGZUnknown slot 0x08
    virtual void Release() {}      // cIGZUnknown slot 0x10
};

// The disaster layer object city slot 0x240 -> 0x38(0xca380b06) returns; 0x38(cellX, cellZ) starts the riot.
class IAuraDisasterApi {
public:
    virtual ~IAuraDisasterApi() = default;
    virtual void StartAtCell(int cellX, int cellZ) = 0;      // vslot 0x38
};

class IAuraCityApi : public IAuraService {
public:
    virtual void PositionToCell(float x, float z, int& cellX, int& cellZ) = 0;   // vslot 0x2f8
    virtual float CellSizeXMeters() const = 0;                                  // vslot 0x2d8
    virtual float CellSizeZMeters() const = 0;                                  // vslot 0x2e0
    virtual IAuraDisasterApi* Disaster(std::uint32_t type) = 0;                 // vslot 0x240 -> 0x38(type)
    virtual void RemoveMessageTarget(cIGZMessageTarget2* target) = 0;           // vslot 0x190 -> 0xa8
};

// cSC4SafeListIterator<cISC4Lot*> (not recovered): the original walks a circular list with a {current, end} pair.
class IAuraLotIterator {
public:
    virtual ~IAuraLotIterator() = default;
    virtual bool AtEnd() = 0;            // current == end (or no list)
    virtual void Advance() = 0;          // current = current->next
    virtual void Restart() = 0;          // current = end->next (the first lot)
    virtual cSC4Lot* Lot() = 0;          // the current node's lot
    virtual void Release() = 0;          // cSC4SafeListIterator::Release @ 0x100140068
};

class IAuraLotManagerApi : public IAuraService {
public:
    virtual cSC4Lot* LotAtCell(int cellX, int cellZ, int unused) = 0;   // vslot 0x28
    virtual IAuraLotIterator* LotIterator() = 0;                        // vslot 0x78
    virtual int LotCount() = 0;                                         // vslot 0x80
};

class IAuraBudgetApi : public IAuraService {
public:
    // vslot 0xd8(department) -> that department's record, vslot 0x50 -> its first float (the funding fraction).
    virtual float DepartmentFunding(std::uint32_t departmentId) = 0;
    // vslot 0x198(propertyHolder, department): whether the occupant's department is funded.
    virtual bool IsDepartmentFunded(cISCPropertyHolder* holder, std::uint32_t departmentId) = 0;
    // vslot 0x120(economyType): the current tax rate as a float.
    virtual float TaxRate(std::uint32_t economyType) = 0;
};

class IAuraResidentialApi : public IAuraService {
public:
    virtual cSC4SimGrid<float>* EQCandidateMap() = 0;                    // vslot 0x138
    virtual cSC4SimGrid<float>* HQCandidateMap() = 0;                    // vslot 0x140
    virtual void PopulationGrids(cSC4SimGrid<std::uint16_t>*& wealth1,   // vslot 0xc8
                                 cSC4SimGrid<std::uint16_t>*& wealth2,
                                 cSC4SimGrid<std::uint16_t>*& wealth3) = 0;
    virtual void EndSchoolStrike() = 0;                                  // vslot 0x40
    virtual void EndHealthStrike() = 0;                                  // vslot 0x48
};

class IAuraGridApi : public IAuraService {
public:
    virtual cSC4SimGrid<std::uint8_t>* Uint8Map() = 0;
};

class IAuraPollutionApi : public IAuraService {
public:
    virtual cSC4SimGrid<std::int16_t>* TypeMap(int type) = 0;            // vslot 0x28(type 0..3)
};

class IAuraOrdinanceApi : public IAuraService {
public:
    virtual int OrdinanceValue(std::uint32_t ordinanceId, int unused) = 0;   // vslot 0xc0
};

class IAuraLandValueApi : public IAuraService {
public:
    virtual int LandValueAtCell(int cellX, int cellZ) = 0;               // vslot 0x28
};

class IAuraHistoryApi : public IAuraService {
public:
    virtual void AddHistory(std::uint32_t historyId, std::int64_t value) = 0;   // vslot 0x60
};

// SL::spPoliceSim / SL::spFireProtectionSim, reduced to the strike buster.
class IAuraStrikeApi : public IAuraService {
public:
    virtual void EndStrike() = 0;      // police vslot 0x68, fire vslot 0x70
};

// Everything outside the object. One method per original call; the global/slot is in the comment.
class IAuraHost {
public:
    virtual ~IAuraHost() = default;

    virtual cIGZMessageServer2* MessageServer() = 0;                          // SL::spMS2
    virtual IAuraCityApi* City() = 0;                                         // SL::spCity
    virtual IAuraLotManagerApi* LotManager() = 0;                             // SL::spLotManager
    virtual IAuraBudgetApi* Budget() = 0;                                     // SL::spBudgetSim
    virtual IAuraResidentialApi* Residential() = 0;                           // SL::spResidentialSim
    virtual IAuraGridApi* Crime() = 0;                                        // SL::spCrimeSim -> 0x28
    virtual IAuraGridApi* TrafficDensity() = 0;                               // SL::spTrafficSim -> 0x68
    virtual IAuraGridApi* TrafficCongestion() = 0;                            // SL::spTrafficSim -> 0x50
    virtual IAuraPollutionApi* Pollution() = 0;                               // SL::spPollutionSim
    virtual IAuraOrdinanceApi* Ordinance() = 0;                               // SL::spOrdinanceSim
    virtual IAuraLandValueApi* LandValue() = 0;                                // SL::spLandValueSim
    virtual IAuraHistoryApi* History() = 0;                                   // SL::spHistoryWarehouse
    virtual IAuraStrikeApi* PoliceStrike() = 0;                               // SL::spPoliceSim -> 0x68
    virtual IAuraStrikeApi* FireStrike() = 0;                                 // SL::spFireProtectionSim -> 0x70
    virtual IAuraService* BuildingDevelopment() = 0;                          // SL::spBuildingDevelopmentSim
    virtual IAuraService* Simulator() = 0;                                    // SL::spSimulator (null when absent)

    virtual std::int32_t SimDateNumber() = 0;                                 // simulator vslot 0x40
    // simulator vslot 0x98(target, interval, name, flags); intervals 0, 10, 4 and 6.
    virtual void AddSimulatorAgent(cIGZMessageTarget2* target, int interval, const char* name,
                                   std::uint32_t flags) = 0;
    // simulator vslot 0x40 through SL::spSimulator, but the class needs "is there a simulator" too:
    virtual bool SimulatorPresent() = 0;

    // SL::spRM vslot 0x18(key {0x6534284a, 0xe7e2c2db, 0xaa023079}, 0xa52160f5, &exemplar, 0, 0) then
    // exemplar vslot 0x30 (the property holder). *out is not counted.
    virtual bool GetTuningProperties(cISCPropertyHolder** out) = 0;

    // RZGetFramework vslot 0x28(0xebe0e860, 0xe898ed03, &registry): the game-data registry, not counted.
    virtual cISC4GameDataRegistry* GameDataRegistry() = 0;

    // cSC4Lot::GetBuilding() (lot vslot 0x180) -> cISC4BuildingOccupant::AsOccupant() (vslot 0x18), not counted.
    virtual cISC4Occupant* BuildingOccupant(cSC4Lot* lot) = 0;

    // cSC4DynamicLimitTimer(ms) + the expiry test of the lot pass. BeginSlice returns a token for SliceExpired.
    virtual std::uint64_t BeginSlice(std::uint32_t milliseconds) = 0;
    virtual bool SliceExpired(std::uint64_t token) = 0;

    // SC4WriteGZSerializable / SC4ReadGZSerializable(0xc9bd939a, ..., false) of one grid.
    virtual void WriteGrid(cIGZOStream& stream, cIGZSerializable* grid) = 0;
    virtual cIGZSerializable* ReadGrid(cIGZIStream& stream) = 0;
};
IAuraHost* AuraHost();
void SetAuraHost(IAuraHost* host);

// ---------------------------------------------------------------------------------------------------
// cISC4AuraSimulator: the interface of the primary COM view (22 slots, vtable 0x100a86fb0).
// ---------------------------------------------------------------------------------------------------
class cISC4AuraSimulator : public cIGZUnknown {
public:
    virtual bool Init() = 0;                                                              // [3]  0x18
    virtual bool Shutdown() = 0;                                                          // [4]  0x20
    virtual std::int8_t GetAuraValue(int cellX, int cellZ) = 0;                           // [5]  0x28
    virtual void AddTransientEffect(float x, float z, float radius, float magnitude) = 0;  // [6]  0x30
    virtual void AddTransientEffect(float x, float z, TransientAuraEffect effect) = 0;     // [7]  0x38
    virtual std::int32_t GetMayorRating() = 0;                                            // [8]  0x40
    virtual std::int32_t GetMaxMayorRating(AuraPoint& cell) = 0;                          // [9]  0x48
    virtual std::int32_t GetMinMayorRating(AuraPoint& cell) = 0;                          // [10] 0x50
    virtual void BustStrike(eStrikeBuster which) = 0;                                     // [11] 0x58
    virtual cSC4SimGrid<std::int8_t>* GetAuraGrid() = 0;                                  // [12] 0x60
    virtual cSC4SimGrid<std::int8_t>* GetTransientAuraGrid() = 0;                         // [13] 0x68
    virtual cSC4SimGrid<std::int16_t>* GetParkMap() = 0;                                  // [14] 0x70
    virtual cSC4SimGrid<std::int16_t>* GetLandmarkMap() = 0;                              // [15] 0x78
    virtual ~cISC4AuraSimulator() = default;                                              // [16]/[17]
};

// ---------------------------------------------------------------------------------------------------
// The object.
// ---------------------------------------------------------------------------------------------------
class cSC4AuraSimulator final : public cISC4AuraSimulator,
                                public cIGZMessageTarget2,
                                public cIGZSerializable {
public:
    cSC4AuraSimulator();
    ~cSC4AuraSimulator() override;

    // cIGZUnknown (one shared count at +0x1c)
    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override;
    std::uint32_t Release() override;

    // cISC4AuraSimulator
    bool Init() override;                                                          // @ 0x100218422
    bool Shutdown() override;                                                      // @ 0x100219384
    std::int8_t GetAuraValue(int cellX, int cellZ) override;                        // @ 0x10021b364
    void AddTransientEffect(float x, float z, float radius, float magnitude) override;   // @ 0x10021b3ba
    void AddTransientEffect(float x, float z, TransientAuraEffect effect) override; // @ 0x10021b384
    std::int32_t GetMayorRating() override;                                        // @ 0x10021b376
    std::int32_t GetMaxMayorRating(AuraPoint& cell) override;                      // @ 0x10021d2fe
    std::int32_t GetMinMayorRating(AuraPoint& cell) override;                      // @ 0x10021d316
    void BustStrike(eStrikeBuster which) override;                                 // @ 0x10021d272
    cSC4SimGrid<std::int8_t>* GetAuraGrid() override;                              // @ 0x10021d32e
    cSC4SimGrid<std::int8_t>* GetTransientAuraGrid() override;                     // @ 0x10021d338
    cSC4SimGrid<std::int16_t>* GetParkMap() override;                              // @ 0x10021d342
    cSC4SimGrid<std::int16_t>* GetLandmarkMap() override;                          // @ 0x10021d350

    // cIGZMessageTarget2 / cIGZSerializable
    bool DoMessage(cIGZMessage2* message) override;                                // @ 0x100219f62
    bool Write(cIGZOStream& stream) const override;                                // @ 0x10021997e
    bool Read(cIGZIStream& stream) override;                                       // @ 0x100219b70
    std::uint32_t GetGZCLSID() const override { return kAuraSimulatorClassId; }     // @ 0x10021d35e

    // Non-virtual helpers of the original, recovered.
    void ReadSimulatorTunableValues();                            // @ 0x100218c72
    void DoIdle(std::uint32_t budgetMilliseconds);                // @ 0x10021a1c4
    void DoOccupantChanged(cISC4Occupant* occupant, bool added);  // @ 0x10021acd2
    void DoEndOfSimulationCycle();                                // @ 0x10021b2d0
    void ComputeTaxRateChangeEffect();                            // @ 0x10021b476
    std::int8_t GetLotBaseAura(cSC4Lot* lot);                     // @ 0x10021b564
    void UpdateAuraGrid();                                        // @ 0x10021cc5c
    void ComputeHotspotList();                                    // @ 0x10021cfdc
    void CheckForStartingRiot();                                  // @ 0x10021d174
    // The two game-data command handlers (static, registered by Init).
    static bool sAddTransientEffect(void* handle, cIGZCommandParameterSet* in, cIGZCommandParameterSet* out,
                                    cISC4GameDataRegistry* registry);   // @ 0x100219220
    static bool sEndStrike(void* handle, cIGZCommandParameterSet* in, cIGZCommandParameterSet* out,
                           cISC4GameDataRegistry* registry);            // @ 0x100219316

    // Accessors the test and the neighbours use.
    bool IsInitialized() const { return initialized_; }
    std::uint32_t RefCount() const { return refCount_; }
    std::int32_t IdleState() const { return idleState_; }
    void SetIdleState(std::int32_t state) { idleState_ = state; }
    std::uint32_t CurrentLandmarkKey() const { return currentLandmarkKey_; }
    std::int32_t MayorRating() const { return mayorRating_; }
    std::int32_t OrdinanceValue() const { return ordinanceValue_; }
    std::int32_t TractSize() const { return tractSize_; }
    float AuraSmoothing() const { return auraSmoothing_; }
    float TransientDecay() const { return transientDecay_; }
    const float* TransientRadii() const { return transientRadius_; }
    const float* TransientMagnitudes() const { return transientMagnitude_; }
    void SetTransientTunable(int effect, float radius, float magnitude) {
        transientRadius_[effect] = radius;
        transientMagnitude_[effect] = magnitude;
    }
    std::int32_t HotspotMergeDistance() const { return hotspotMergeDistance_; }
    std::int32_t HotspotValueThreshold() const { return hotspotValueThreshold_; }
    std::int32_t HotspotMinCount() const { return hotspotMinCount_; }
    void SetHotspotTunables(std::int32_t valueThreshold, std::int32_t mergeDistance, std::int32_t minCount) {
        hotspotValueThreshold_ = valueThreshold;
        hotspotMergeDistance_ = mergeDistance;
        hotspotMinCount_ = minCount;
    }
    const std::vector<tHotspot>& RawHotspots() const { return hotspots_; }      // +0x118
    const std::list<tHotspot>& HotspotList() const { return hotspotList_; }     // +0x130
    const std::map<std::uint32_t, cSC4SimGrid<std::int16_t>*>& LandmarkMaps() const { return landmarkMaps_; }
    float LastTaxRate(int economyType) const { return taxRates_[economyType]; }
    const cSC4ResponseCurve* ParkCurve() const { return parkCurve_; }
    const cSC4ResponseCurve* RiotCurve() const { return riotCurve_; }
    const std::vector<cSC4ResponseCurve*>& PollutionCurves() const { return pollutionCurves_; }
    cSC4SimGrid<std::int8_t>* SmoothedGrid() const { return smoothedGrid_; }
    cSC4SimGrid<std::int8_t>* CellWorkGrid() const { return cellGrid_; }
    cSC4SimGrid<std::int8_t>* VehicleGrid() const { return vehicleGrid_; }
    cSC4SimGrid<std::int16_t>* ParkEffectGrid() const { return parkEffectGrid_; }
    cSC4SimGrid<std::int16_t>* LandmarkWorkGrid() const { return landmarkWorkGrid_; }
    bool NeedsLandmarkRefresh() const { return needsLandmarkRefresh_; }
    void SetNeedsLandmarkRefresh(bool value) { needsLandmarkRefresh_ = value; }
    cRZRandom& Random() { return random_; }

    // Original field offsets, asserted by the test.
    static constexpr std::uint32_t kOffsetInitialized = 0x18, kOffsetRefCount = 0x1c, kOffsetRandom = 0x20;
    static constexpr std::uint32_t kOffsetCity = 0x38, kOffsetMessageServer = 0x40;
    static constexpr std::uint32_t kOffsetBuildingDevelopment = 0x48, kOffsetTractSize = 0x50;
    static constexpr std::uint32_t kOffsetAuraGrid = 0x58, kOffsetCellGrid = 0x60, kOffsetSmoothedGrid = 0x68;
    static constexpr std::uint32_t kOffsetTransientGrid = 0x70, kOffsetVehicleGrid = 0x78;
    static constexpr std::uint32_t kOffsetParkEffectGrid = 0x80, kOffsetParkGrid = 0x88;
    static constexpr std::uint32_t kOffsetLandmarkMaps = 0x90, kOffsetLandmarkGrid = 0xa8;
    static constexpr std::uint32_t kOffsetLandmarkWorkGrid = 0xb0, kOffsetIdleState = 0xb8;
    static constexpr std::uint32_t kOffsetIdleStart = 0xbc, kOffsetLotIterator = 0xc0;
    static constexpr std::uint32_t kOffsetCurrentKey = 0xc8, kOffsetMayorRating = 0xd4;
    static constexpr std::uint32_t kOffsetOrdinance = 0xd8, kOffsetTaxRates = 0xe8;
    static constexpr std::uint32_t kOffsetHotspots = 0x118, kOffsetHotspotList = 0x130;
    static constexpr std::uint32_t kOffsetNeedsRefresh = 0x148, kOffsetParkCurve = 0x150;
    static constexpr std::uint32_t kOffsetPollutionCurves = 0x158, kOffsetSmoothing = 0x1a8;
    static constexpr std::uint32_t kOffsetBaseAura = 0x1b0, kOffsetTransientRadii = 0x1bc;
    static constexpr std::uint32_t kOffsetTransientMagnitudes = 0x1cc, kOffsetHotspotTunables = 0x1dc;
    static constexpr std::uint32_t kOffsetRiotCurve = 0x1e8, kOffsetMaxRating = 0x1f0, kOffsetMinRating = 0x1fc;
    static constexpr std::uint32_t kClassSize = 0x208;

private:
    cSC4SimGrid<std::int8_t>* MakeInt8Grid(std::uint32_t instanceId, std::int32_t tractSize);
    cSC4SimGrid<std::int16_t>* MakeInt16Grid(std::uint32_t instanceId, std::int32_t tractSize);
    void ReadCurveProperty(cISCPropertyHolder* holder, std::uint32_t propertyId, cSC4ResponseCurve*& curve);
    void DeleteCurve(cSC4ResponseCurve*& curve);
    void ReleaseServices();

    bool initialized_ = false;                       // +0x18
    std::uint32_t refCount_ = 0;                     // +0x1c
    cRZRandom random_;                               // +0x20
    IAuraCityApi* city_ = nullptr;                   // +0x38
    cIGZMessageServer2* messageServer_ = nullptr;    // +0x40
    IAuraService* buildingDevelopment_ = nullptr;    // +0x48 (stored only)
    std::int32_t tractSize_ = 4;                     // +0x50
    cSC4SimGrid<std::int8_t>* auraGrid_ = nullptr;   // +0x58
    cSC4SimGrid<std::int8_t>* cellGrid_ = nullptr;   // +0x60
    cSC4SimGrid<std::int8_t>* smoothedGrid_ = nullptr;   // +0x68
    cSC4SimGrid<std::int8_t>* transientGrid_ = nullptr;  // +0x70
    cSC4SimGrid<std::int8_t>* vehicleGrid_ = nullptr;    // +0x78
    cSC4SimGrid<std::int16_t>* parkEffectGrid_ = nullptr;    // +0x80
    cSC4SimGrid<std::int16_t>* parkGrid_ = nullptr;          // +0x88
    std::map<std::uint32_t, cSC4SimGrid<std::int16_t>*> landmarkMaps_;   // +0x90
    cSC4SimGrid<std::int16_t>* landmarkGrid_ = nullptr;      // +0xa8
    cSC4SimGrid<std::int16_t>* landmarkWorkGrid_ = nullptr;  // +0xb0
    std::int32_t idleState_ = 0;                     // +0xb8
    std::int32_t idleStartDate_ = 0;                 // +0xbc
    IAuraLotIterator* lotIterator_ = nullptr;        // +0xc0
    std::uint32_t currentLandmarkKey_ = 0xffffffffu; // +0xc8
    std::int32_t cycleValue_ = 0;                    // +0xd0 (saved; meaning not identified)
    std::int32_t mayorRating_ = 0;                   // +0xd4
    std::int32_t ordinanceValue_ = 0;                // +0xd8
    float taxRates_[12] = {};                        // +0xe8 (initialised to -1.0)
    std::vector<tHotspot> hotspots_;                 // +0x118
    std::list<tHotspot> hotspotList_;                // +0x130
    bool needsLandmarkRefresh_ = false;              // +0x148
    cSC4ResponseCurve* parkCurve_ = nullptr;             // +0x150
    std::vector<cSC4ResponseCurve*> pollutionCurves_;    // +0x158
    cSC4ResponseCurve* residentialCurve138_ = nullptr;   // +0x170
    cSC4ResponseCurve* residentialCurve140_ = nullptr;   // +0x178
    cSC4ResponseCurve* crimeCurve_ = nullptr;            // +0x180
    cSC4ResponseCurve* trafficTripCurve_ = nullptr;      // +0x188
    cSC4ResponseCurve* trafficCurve_ = nullptr;          // +0x190
    cSC4ResponseCurve* taxChangeCurve_ = nullptr;        // +0x198
    cSC4ResponseCurve* landValueCurve_ = nullptr;        // +0x1a0
    float auraSmoothing_ = 0.6f;                     // +0x1a8
    float transientDecay_ = 0.4f;                    // +0x1ac
    std::int32_t baseAuraResidential_ = 0;           // +0x1b0
    std::int32_t baseAuraCommercial_ = 0;            // +0x1b4
    std::int32_t baseAuraIndustrial_ = 0;            // +0x1b8
    float transientRadius_[4] = {};                  // +0x1bc
    float transientMagnitude_[4] = {};               // +0x1cc
    std::int32_t hotspotMergeDistance_ = 8;          // +0x1dc
    std::int32_t hotspotValueThreshold_ = 196;       // +0x1e0
    std::int32_t hotspotMinCount_ = 16;              // +0x1e4
    cSC4ResponseCurve* riotCurve_ = nullptr;         // +0x1e8
    std::int32_t maxRatingX_ = 0, maxRatingZ_ = 0;   // +0x1f0, +0x1f4
    std::int16_t maxRating_ = -128;                  // +0x1f8
    std::int32_t minRatingX_ = 0, minRatingZ_ = 0;   // +0x1fc, +0x200
    std::int16_t minRating_ = 127;                   // +0x204
};

}  // namespace sc4
