// cSC4PortDeveloper — the port developer base class. It owns the port-building records (cPortLot), loads
// the per-port-type growth configurations (cPortTypeConfig, cPortGrowthRule) from the resource manager,
// tracks each port's stage / efficiency / usage, records the monthly history and drives growth; the
// airport and seaport developers derive from it and supply the eight virtuals that make the port type
// concrete (simulator type, trips, stage change, new lot, advisor upgrade, monthly tick, budget, funding).
//
// Recovered from Sim City 4 Deluxe Editionsub 1.2.1 (0x1000cbe6a..0x1000cde48, class id 0x0a2c4eee).
// Derivation confirmed from the Itanium RTTI (vmi typeinfo 0x100a70280) and the vtables:
//   cSC4PortDeveloper   : cISC4PortDeveloper (offset 0), cIGZMessageTarget2 (offset +8),
//                         cIGZSerializable (offset +0x10)   vtable 0x100a70110
//   cSC4AirportDeveloper: cISC4AirportDeveloper, cSC4PortDeveloper   vtable 0x100a6f6f0
//   cSC4SeaportDeveloper: cISC4SeaportDeveloper, cSC4PortDeveloper   vtable 0x100a6faa0
// and cISC4AirportDeveloper / cISC4SeaportDeveloper themselves derive cISC4PortDeveloper (typeinfos
// 0x100aff578 / 0x100aff638). cSC4LandfillDeveloper / cSC4PropDeveloper are *not* port developers: their
// RTTI (cISC4LandfillDeveloper : cIGZUnknown, cISC4PropDeveloper : cIGZUnknown) only shares the GZCOM
// message-target/serializable layout with this class, so they are independent.
//
// The primary vtable of the concrete class (0x100a70110), i.e. the cISC4PortDeveloper view:
//   [ 0] 0x00 QueryInterface          [ 1] 0x08 AddRef                    [ 2] 0x10 Release
//   [ 3] 0x18 Init                    [ 4] 0x20 Shutdown                  [ 5] 0x28 GetAvgEfficiency
//   [ 6] 0x30 GetSimulatorType (pure) [ 7] 0x38 GetLeastEfficientPort     [ 8] 0x40 GetAllPortLots
//   [ 9] 0x48 GetNumPorts             [10] 0x50 ~cSC4PortDeveloper         [11] 0x58 deleting dtor
//   [12] 0x60 FinalRelease            [13] 0x68 Write                     [14] 0x70 Read
//   [15] 0x78 GetGZCLSID              [16] 0x80 DoMessage                 [17] 0x88 GetTripsForPort (pure)
//   [18] 0x90 OnStageChange (pure)    [19] 0x98 OnNewLotCreated (pure)    [20] 0xa0 SendAdvisorUpgradeMessage (pure)
//   [21] 0xa8 DoMonthlyTick (pure)    [22] 0xb0 UpdateBudget (pure)       [23] 0xb8 GetAllocatedFunding (pure)
// The secondary vtables are cIGZMessageTarget2 at +8 (offset-to-top -8: QI/AddRef/Release/DoMessage
// thunks) and cIGZSerializable at +0x10 (-0x10: QI/AddRef/Release/Write/Read/GetGZCLSID thunks). The C++
// class below declares its virtuals in the same order, so its vtable and the data-member offsets match the
// original (sizeof 0xb0; the test asserts the sizes).
// IID map: 0x4a2c4e69 cISC4PortDeveloper (+ iid 1 on the primary), 0x452294aa cIGZMessageTarget2,
// 0xe4fda3d4 cIGZSerializable.
//
// Original layout (0xb0 bytes):
//   +0x00 the three vptrs (as laid out by this C++ class, so the members below land on the same offsets)
//   +0x18 bool tunables read, +0x19 bool initialised, +0x1c int reference count (one count for the three
//         views; the compiler's adjustor thunks move the secondary this pointers back to the primary)
//   +0x20 uint32 port developer id (the ini exemplar instance; the derived ctor sets it: airport
//         0xca270e9d, seaport 0xea29ad92) — also the gate of readTunableVars
//   +0x24 int port type count (from the exemplar's property 0x8a270fc3)
//   +0x28 cIGZMessageServer2, +0x30 cISC4ZoneManager, +0x38 cISC4LotManager, +0x40 cISC4City,
//   +0x48 cISC4BudgetSimulator
//   +0x50/+0x54 two ints: zeroed by the constructor (+0x50 also by the derived constructors), never
//         read by the recovered code (the derived budget code uses the +0x48 simulator)
//   +0x58 uint16 zone type (airport 11, seaport 12; the compiled Read stores a zero-extended uint16)
//   +0x5c uint32 history category (airport 0x6a3e9032, seaport 0xea3e904a)
//   +0x60 uint32 advisor event id (airport 0x6a4119f0, seaport 0x4a4155ab)
//   +0x64 uint32 building occupant group (airport 0x1508, seaport 0x1509) — the group a placed building
//         must be in for DoLotInserted to pick it up through message 0x99ef1142
//   +0x68 std::list<cPortLot*> (0x18 bytes: sentinel next/prev, size)
//   +0x80 std::vector<cPortTypeConfig*> (0x18)   +0x98 std::vector<uint32_t> port type ids (0x18)
//
// cPortLot (0x40 bytes, vtable 0x100a70240: the two destructor slots): one port building.
//   +0x08 int stage, +0x10 cPortTypeConfig* (borrowed), +0x18 bool "cached lot is stale" (set by Read,
//   cPortLot::GetLot re-fetches it from the lot manager and clears it), +0x1a int16 x, +0x1c int16 z,
//   +0x20 float efficiency, +0x24 bool "being rebuilt" (swapLot sets it; doLotStateChanged clears or
//   removes), +0x28 float demand usage (a fraction), +0x30 cISC4Lot* (owned; the recovered class is the concrete cSC4Lot), +0x38 int trips (written by
//   the derived developer's GetTripsForPort/UpdateTripCapacity; the base leaves it 0).
//
// cPortTypeConfig (0x50 bytes, vtable 0x100a70268): the stages of one port type.
//   +0x08 unused int (zeroed), +0x0c int type index, +0x10 uint32 stage count (property 0xca271780),
//   +0x14 float efficiency threshold (property 0xca271785 scaled by 0.01),
//   +0x18 std::vector<cPortGrowthRule> stages, +0x30 uint8 size class (property 0xca271787, default 2),
//   +0x38 std::vector<std::vector<float>> per-cell vertex heights (properties 0xca271788 rows,
//   0xca271789 columns, 0xca271800 data; only the derived seaport developer uses it).
// cPortGrowthRule (0x20 bytes): +0x00 stage index, +0x04 lot configuration id (property 0xca271781),
//   +0x08 int64 budget (0xca271783), +0x10 uint32 capacity (0xca271784), +0x18 int64 cost (0xca271786).
//
// Quirks kept:
//  * Release destroys the object as soon as the count is below 2 (FinalRelease = Shutdown + delete) and
//    does not decrement it (0x1000cc478); the two non-virtual thunks do the same from the other views.
//  * GetLeastEfficientPort does not look for the least efficient port: it takes the *first* list element.
//    With more than one port lot the original jumps to itself forever (0x1000cdbc1: EB FE); this port
//    returns the first element for any count (documented deviation; a hang cannot be preserved).
//  * GetLeastEfficientPort reads the efficiency before refreshing the cached lot (0x1000cdbc7).
//  * doLotStateChanged's first HabitationState argument is never read (the mangled signature has two:
//    __ZN17cSC4PortDeveloper17doLotStateChangedEP8cISC4LotNS0_15HabitationStateES2_); only the new state
//    (5 = reserved) is tested.
//  * DoMessage 0x99ef1142 dereferences a null GetVoid1() (0x1000cc89f) and a null AsPropertyHolder()
//    (0x1000cc8a8); this port returns true instead of crashing.
//  * DoMessage's "post city init" message 0x26d31ec1 refreshes every port's cached lot and discards the
//    result.
//  * Read indexes the configuration vector with the stored ushort without a bounds check (0x1000cc6e6),
//    and stores the zone type with a 32-bit write that also clears the padding at +0x5a.
//  * Read constructs its cPortLots with the "stale lot" flag set; DoLotInserted constructs them with the
//    lot already known.
//  * The constructor zeroes +0x18..+0x57 and writes -1 at +0x64; the original leaves +0x58/+0x5c/+0x60
//    uninitialised until the derived constructor writes the identity (+0x20/+0x58/+0x5c/+0x60/+0x64), while
//    this port zero-initialises them for determinism.
//  * readTunableVars' first read is gated on +0x20 being non-zero (the derived constructor sets it), so a
//    bare cSC4PortDeveloper would load no configurations.
//  * cPortTypeConfig::LoadConfiguration trusts the exemplar: it reads stage-count entries from the four
//    arrays without checking their lengths. This port clamps the loop to the shortest array.
//  * The configuration objects are destroyed by Shutdown through their deleting destructor
//    (0x1000cc2a8..0x1000cc2cc) while the vector keeps its buffer; the destructor frees the buffer and
//    clears the port list (delete already ran in Shutdown).
//  * Shutdown removes the simulator agent through SL::spCity->GetSimulator() *after* it has released the
//    stored city pointer, and passes the cIGZMessageTarget2 view (this+8).
//  * SendAdvisorMessage sends the advisor event 0 if the city has no advisor system (the original would
//    call through the null pointer); the host seam returns null in that case.
//
// Every call outside the object goes through IPortDeveloperHost (one method per original call, with the
// original global and vtable slot in the comment). The lots are the recovered cSC4Lot (Lot.h); the
// occupant, the property holder and the message server are the recovered interfaces (Occupant.h,
// SCProperty.h, cIGZMessage2.h). The lot configuration only exposes GetID through cISC4LotConfigurationRef,
// so the three calls the port makes are declared by IPortLotConfiguration (the real object is the game's
// cISC4LotConfiguration, vtable 0x100aacc20: GetID 0x18, GetGrowthStage 0x58,
// IsCompatibleWithZoneType 0x78).
#pragma once

#include <cstddef>
#include <cstdint>
#include <list>
#include <vector>

#include "gz/cIGZIStream.h"
#include "gz/cIGZOStream.h"
#include "gz/cIGZMessage2.h"
#include "gz/cIGZSerializable.h"
#include "sc4/Lot.h"
#include "sc4/Occupant.h"
#include "sc4/SCProperty.h"
#include "sc4/SCPropertyHelp.h"

namespace sc4 {

class cSC4PortDeveloper;
class cPortTypeConfig;
class IPortLotDeveloperApi;

constexpr std::uint32_t kPortDeveloperClassId = 0x0a2c4eee;
constexpr std::uint32_t kPortGZIIDPortDeveloper = 0x4a2c4e69;       // cISC4PortDeveloper (and iid 1)
// Named with the kPort prefix because several recovered headers declare their own
// kGZIID_cIGZMessageTarget2 / kGZIID_cIGZSerializable in this namespace.
constexpr std::uint32_t kPortGZIIDMessageTarget2 = 0x452294aa;
constexpr std::uint32_t kPortGZIIDSerializable = 0xe4fda3d4;

// The messages the developer registers for / handles (kMessageLotStateChanged lives in Lot.h).
constexpr std::uint32_t kMessagePortPostCityInit = 0x26d31ec1;      // registered, refresh the cached lots
constexpr std::uint32_t kMessagePortBuildingPlaced = 0x99ef1142;    // registered, DoLotInserted
constexpr std::uint32_t kMessagePortMonthly = 0x66956816;           // handled, DoMonthlyTick + simulation
constexpr std::uint32_t kMessagePortHistory = 0x89efa536;           // posted by RecordHistoryData
constexpr std::uint32_t kPortBuildingType = 0x278128a0;             // the building occupant type
constexpr std::uint32_t kPortExcludedProperty = 0x29244db;          // a building without it may be a port
constexpr std::uint32_t kPortTypeListProperty = 0x8a270fc3;         // the port type id array on the ini exemplar
constexpr std::uint32_t kPortExemplarType = 0x6534284a;             // cGZPersistResourceKey of the exemplar
constexpr std::uint32_t kPortExemplarGroup = 0x67cd5fa1;
constexpr std::uint32_t kPortExemplarIID = 0xa52160f5;              // iid requested from the resource
constexpr std::int32_t kPortSimulatorInterval = 6;                  // SL::spSimulator->AddAgent argument
constexpr std::uint32_t kPortSimulatorFlags = 2;
constexpr const char* kPortSimulatorName = "cSC4PortDeveloper";

// cPortGrowthRule: one stage of a port type (see the layout note).
struct cPortGrowthRule {
    std::int32_t stage = 0;                 // +0x00
    std::int32_t lotConfigurationId = 0;    // +0x04
    std::int64_t budget = 0;                // +0x08
    std::uint32_t capacity = 0;             // +0x10
    std::int64_t cost = 0;                  // +0x18
};
static_assert(sizeof(cPortGrowthRule) == 0x20, "cPortGrowthRule must stay 0x20 bytes");

// cPortTypeConfig: the stages of one port type (see the layout note).
class cPortTypeConfig {
public:
    cPortTypeConfig();
    virtual ~cPortTypeConfig();                                  // vtable 0x100a70268

    // @ 0x1000cd79e. Reads properties 0xca271780..0xca271800 from the exemplar; returns false when one
    // of the four stage arrays is missing.
    bool LoadConfiguration(int typeIndex, cISCPropertyHolder* holder);

    std::int32_t TypeIndex() const { return typeIndex_; }        // +0x0c
    std::uint32_t StageCount() const { return stageCount_; }     // +0x10
    float Threshold() const { return threshold_; }               // +0x14
    std::uint8_t SizeClass() const { return sizeClass_; }        // +0x30
    const std::vector<cPortGrowthRule>& Stages() const { return stages_; }   // +0x18
    const std::vector<std::vector<float>>& Grid() const { return grid_; }    // +0x38

    static constexpr std::uint32_t kOffsetStages = 0x18;
    static constexpr std::uint32_t kOffsetSizeClass = 0x30;
    static constexpr std::uint32_t kOffsetGrid = 0x38;
    static constexpr std::uint32_t kClassSize = 0x50;

private:
    [[maybe_unused]] std::int32_t reserved08_ = 0;    // +0x08
    std::int32_t typeIndex_ = -1;                     // +0x0c
    std::uint32_t stageCount_ = 0;                    // +0x10
    float threshold_ = 0.0f;                          // +0x14
    std::vector<cPortGrowthRule> stages_;             // +0x18
    std::uint8_t sizeClass_ = 2;                      // +0x30
    std::uint8_t reserved31_[7] = {};                 // +0x31
    std::vector<std::vector<float>> grid_;            // +0x38
};
static_assert(sizeof(cPortTypeConfig) == 0x50, "cPortTypeConfig must stay 0x50 bytes");

// cPortLot: one port building (see the layout note). Its lot is looked up lazily through the lot manager
// when the cached pointer is stale, exactly like the original.
class cPortLot {
public:
    cPortLot() = default;
    virtual ~cPortLot();                                          // vtable 0x100a70240, releases the lot

    // @ 0x1000cc3e4. Refreshes the cached lot through SL::spLotManager->GetLot(x, z, true) when +0x18 is
    // set, then returns it (borrowed).
    cSC4Lot* GetLot();

    // @ 0x1000ccf24. True when the port has a next stage and its trips reached 96% of that stage's
    // capacity and the lot is not in state 2.
    bool IsGrowthTriggered(cSC4PortDeveloper* owner);

    std::int32_t Stage() const { return stage_; }                  // +0x08
    void SetStage(std::int32_t stage) { stage_ = stage; }
    cPortTypeConfig* Config() const { return config_; }            // +0x10 (borrowed)
    void SetConfig(cPortTypeConfig* config) { config_ = config; }
    bool LotIsStale() const { return lotStale_; }                  // +0x18
    void SetLotIsStale(bool stale) { lotStale_ = stale; }
    std::int16_t X() const { return x_; }                          // +0x1a
    std::int16_t Z() const { return z_; }                          // +0x1c
    void SetCell(std::int16_t x, std::int16_t z) { x_ = x; z_ = z; }
    float Efficiency() const { return efficiency_; }               // +0x20
    void SetEfficiency(float efficiency) { efficiency_ = efficiency; }
    bool IsBeingRebuilt() const { return rebuilding_; }            // +0x24
    void SetIsBeingRebuilt(bool value) { rebuilding_ = value; }
    float Usage() const { return usage_; }                         // +0x28
    void SetUsage(float usage) { usage_ = usage; }
    cSC4Lot* CachedLot() const { return lot_; }                   // +0x30
    void SetCachedLot(cSC4Lot* lot) { lot_ = lot; }
    std::int32_t Trips() const { return trips_; }                  // +0x38
    void SetTrips(std::int32_t trips) { trips_ = trips; }

    static constexpr std::uint32_t kOffsetStage = 0x08;
    static constexpr std::uint32_t kOffsetConfig = 0x10;
    static constexpr std::uint32_t kOffsetLotIsStale = 0x18;
    static constexpr std::uint32_t kOffsetCell = 0x1a;
    static constexpr std::uint32_t kOffsetEfficiency = 0x20;
    static constexpr std::uint32_t kOffsetUsage = 0x28;
    static constexpr std::uint32_t kOffsetLot = 0x30;
    static constexpr std::uint32_t kOffsetTrips = 0x38;
    static constexpr std::uint32_t kClassSize = 0x40;

private:
    std::int32_t stage_ = 0;                       // +0x08
    [[maybe_unused]] std::int32_t reserved0c_ = 0; // +0x0c
    cPortTypeConfig* config_ = nullptr;            // +0x10
    bool lotStale_ = false;                        // +0x18
    std::uint8_t reserved19_ = 0;                  // +0x19
    std::int16_t x_ = 0;                           // +0x1a
    std::int16_t z_ = 0;                           // +0x1c
    std::uint16_t reserved1e_ = 0;                 // +0x1e
    float efficiency_ = 1.0f;                      // +0x20
    bool rebuilding_ = false;                      // +0x24
    std::uint8_t reserved25_[3] = {};              // +0x25
    float usage_ = 0.0f;                           // +0x28
    std::uint32_t reserved2c_ = 0;                 // +0x2c
    cSC4Lot* lot_ = nullptr;                      // +0x30
    std::int32_t trips_ = 0;                       // +0x38
    std::uint32_t reserved3c_ = 0;                 // +0x3c
};
static_assert(sizeof(cPortLot) == 0x40, "cPortLot must stay 0x40 bytes");

// SC4Point<int> in the original signature; named PortPoint here to avoid clashing with the several
// recovered headers that declare their own SC4Point (ZoneDeveloper.h, cSC4ResponseCurve.h, ...).
struct PortPoint {
    std::int32_t x = 0;
    std::int32_t z = 0;
};

// cISC4LotConfiguration restricted to the three calls this class makes. The real object is the game's lot
// configuration (vtable 0x100aacc20): GetID vslot 0x18 (already on cISC4LotConfigurationRef),
// GetGrowthStage vslot 0x58, IsCompatibleWithZoneType vslot 0x78.
class IPortLotConfiguration : public cISC4LotConfigurationRef {
public:
    virtual int GetGrowthStage() const = 0;                            // vslot 0x58
    virtual bool IsCompatibleWithZoneType(int zoneType) const = 0;     // vslot 0x78
};

// SC4CellRegion<int> + cRZCellMap<bool> as cSC4PortDeveloper::removeLot builds it (0x1000cd2fa..):
// bounds at +0x00..+0x0f, then the cell map (+0x10 vptr, +0x18 rowCount, +0x1c bitCount, +0x20 wordCount,
// +0x28 rows). removeLot fills every bit, i.e. the whole lot footprint is de-zoned.
class PortCellRegion {
public:
    void Fill(int x0, int z0, int x1, int z1);   // width = x1+1-x0, height = z1+1-z0, all bits set
    bool Test(int x, int z) const;

    std::int32_t x0 = 0, z0 = 0, x1 = 0, z1 = 0;             // +0x00
    std::uint32_t rowCount = 0, bitCount = 0, wordCount = 0;  // +0x10 (cell map header without the vptr)
    std::vector<std::vector<std::uint32_t>> rows;             // +0x28
};

// ---------------------------------------------------------------------------------------------------
// Services the class reaches through the SL::sp* globals. Each is a small view of the original object with
// its vtable slot; the host owns the objects and may return the same singleton to every class.
// ---------------------------------------------------------------------------------------------------
class IPortDeveloperService {
public:
    virtual ~IPortDeveloperService() = default;
    virtual void AddRef() {}     // cIGZUnknown slot 0x08
    virtual void Release() {}    // cIGZUnknown slot 0x10
};

class IPortSimulatorApi : public IPortDeveloperService {
public:
    // cSC4Simulator vslot 0x98: AddAgent(target, interval, cIGZString name, flags)
    virtual void AddAgent(cIGZMessageTarget2* target, int interval, const char* name,
                          std::uint32_t flags) = 0;
    virtual void RemoveAgent(cIGZMessageTarget2* target) = 0;         // vslot 0xa8
};

class IPortAdvisorSystemApi : public IPortDeveloperService {
public:
    virtual void PushEvent(int event, void* subject) = 0;             // cSC4AdvisorSystem vslot 0x78
};

class IPortCityApi : public IPortDeveloperService {
public:
    virtual IPortSimulatorApi* GetSimulator() = 0;                    // cSC4City vslot 0x190 (borrowed)
    virtual IPortAdvisorSystemApi* GetAdvisorSystem() = 0;            // cSC4City vslot 0x288 (borrowed)
};

class IPortZoneManagerApi : public IPortDeveloperService {
public:
    // cSC4ZoneManager vslot 0x58: PlaceZone(region, 0, true, true, false, true, false, nullptr, nullptr,
    // nullptr) — removeLot clears the zone of every cell of the lot's footprint.
    virtual void ClearZone(const PortCellRegion& region) = 0;
};

class IPortLotManagerApi : public IPortDeveloperService {
public:
    virtual cSC4Lot* GetLot(int x, int z, bool exactCell) = 0;                 // vslot 0x28 (borrowed)
    virtual cSC4Lot* GetOccupantLot(cISC4Occupant* occupant) = 0;              // vslot 0x70 (borrowed)
    // vslot 0xb8: CreateLot(x, z, sizeX, sizeZ, facing, &out); on success *out owns a reference.
    virtual bool CreateLot(int x, int z, int sizeX, int sizeZ, int facing, cSC4Lot** out) = 0;
};

class IPortBudgetApi : public IPortDeveloperService {
public:
    virtual void WithdrawFunds(std::int64_t amount) = 0;                       // cSC4BudgetSimulator vslot 0x40
};

class IPortDemandApi : public IPortDeveloperService {
public:
    // cSC4DemandSimulator vslot 0x38: UpdateOccupantEffects(buildingType, oldUsage, newUsage)
    virtual void UpdateOccupantEffects(std::uint32_t buildingType, const SC4Percentage& oldUsage,
                                       const SC4Percentage& newUsage) = 0;
};

class IPortLotConfigurationManagerApi : public IPortDeveloperService {
public:
    // cSC4LotConfigurationManager vslot 0x40 (GetLotConfiguration): the configuration of `id`, borrowed.
    virtual IPortLotConfiguration* GetLotConfiguration(std::uint32_t id) = 0;
};

class IPortLotDeveloperApi : public IPortDeveloperService {
public:
    // cSC4LotDeveloper vslot 0x38
    virtual bool ConstructDefaultLot(cSC4Lot* lot, IPortLotConfiguration* configuration,
                                     std::uint32_t objectId, bool flag) = 0;
    virtual void StartDemolishLot(cSC4Lot* lot) = 0;                           // vslot 0x60
    virtual void EndDemolishLot(cSC4Lot* lot) = 0;                             // vslot 0x68
};

class IPortBuildingDevelopmentApi : public IPortDeveloperService {
public:
    // cSC4BuildingDevelopmentSimulator vslot 0x120 (GetLotDeveloper), borrowed.
    virtual IPortLotDeveloperApi* GetLotDeveloper() = 0;
};

class IPortResourceApi : public IPortDeveloperService {
public:
    // SL::spRM vslot 0x18: GetResource({0x6534284a, 0x67cd5fa1, instance}, iid 0xa52160f5, &out, 0, 0).
    // Returns the exemplar's property holder with a reference the caller releases.
    virtual cISCPropertyHolder* LoadExemplar(std::uint32_t instance) = 0;
};

class IPortDeveloperHost {
public:
    virtual ~IPortDeveloperHost() = default;

    virtual cIGZMessageServer2* MessageServer() = 0;                  // SL::spMS2
    virtual IPortSimulatorApi* Simulator() = 0;                       // SL::spSimulator
    virtual IPortCityApi* City() = 0;                                 // SL::spCity
    virtual IPortZoneManagerApi* ZoneManager() = 0;                   // SL::spZoneManager
    virtual IPortLotManagerApi* LotManager() = 0;                     // SL::spLotManager
    virtual IPortBudgetApi* Budget() = 0;                             // SL::spBudgetSim
    virtual IPortDemandApi* Demand() = 0;                             // SL::spDemandSim
    virtual IPortBuildingDevelopmentApi* BuildingDevelopment() = 0;   // SL::spBuildingDevelopmentSim
    virtual IPortLotConfigurationManagerApi* LotConfigurationManager() = 0;  // SL::spLotConfigurationManager
    virtual IPortResourceApi* ResourceManager() = 0;                  // SL::spRM
    // The cISC4Lot payload of a lot-state message: the original QueryInterfaces the message's unknown for
    // 0xe818fced (0x1000cc82d) and drops the message when the query fails. The recovered lot (cSC4Lot) is
    // not a cIGZUnknown, so the host resolves its own payload object to the lot.
    virtual cSC4Lot* MessageLot(cIGZUnknown* payload) = 0;
};
IPortDeveloperHost* PortDeveloperHost();
void SetPortDeveloperHost(IPortDeveloperHost* host);

// ---------------------------------------------------------------------------------------------------
// cISC4PortDeveloper: the primary COM view (10 virtuals; the concrete class adds its own).
// ---------------------------------------------------------------------------------------------------
class cISC4PortDeveloper : public cIGZUnknown {
public:
    virtual bool Init() = 0;                                                          // [3]  0x18
    virtual bool Shutdown() = 0;                                                      // [4]  0x20
    virtual float GetAvgEfficiency() = 0;                                             // [5]  0x28
    virtual std::uint32_t GetSimulatorType() const = 0;                               // [6]  0x30 (pure)
    virtual bool GetLeastEfficientPort(SC4Percentage& efficiency, PortPoint& location) = 0;  // [7] 0x38
    virtual void GetAllPortLots(std::vector<cSC4Lot*>& out) = 0;                      // [8]  0x40
    virtual int GetNumPorts(int sizeMask, int stageMask) = 0;                         // [9]  0x48

protected:
    // Non-virtual like cIGZUnknown's: the concrete class declares the destructor pair at 0x50/0x58.
    ~cISC4PortDeveloper() = default;
};

// ---------------------------------------------------------------------------------------------------
// The object.
// ---------------------------------------------------------------------------------------------------
class cSC4PortDeveloper : public cISC4PortDeveloper,
                          public cIGZMessageTarget2,
                          public cIGZSerializable {
public:
    cSC4PortDeveloper();                                             // @ 0x1000cbe6a
    virtual ~cSC4PortDeveloper();                                    // @ 0x1000cbf1c (0x50/0x58)

    // cIGZUnknown (one shared count at +0x1c)
    bool QueryInterface(std::uint32_t iid, void** out) override;     // @ 0x1000cc4fe
    std::uint32_t AddRef() override;                                 // @ 0x1000cc44e
    std::uint32_t Release() override;                                // @ 0x1000cc478

    // cISC4PortDeveloper
    bool Init() override;                                            // @ 0x1000cbf9e
    bool Shutdown() override;                                        // @ 0x1000cc26e
    float GetAvgEfficiency() override;                               // @ 0x1000cce10
    bool GetLeastEfficientPort(SC4Percentage& efficiency, PortPoint& location) override;  // @ 0x1000cdb98
    void GetAllPortLots(std::vector<cSC4Lot*>& out) override;        // @ 0x1000cdbf6
    int GetNumPorts(int sizeMask, int stageMask) override;           // @ 0x1000cdb62

    // Slots 0x60..0x80 of the original vtable, in its declaration order: FinalRelease, then the two
    // secondary views' overrides (Write/Read/GetGZCLSID from cIGZSerializable, DoMessage from
    // cIGZMessageTarget2). The class declares them itself, which is why the original has them in the
    // primary vtable (and the secondary vtables hold only the adjustor thunks).
    virtual void FinalRelease();                                     // @ 0x1000cc4d4   [12] 0x60
    bool Write(cIGZOStream& stream) const override;                  // @ 0x1000cc564   [13] 0x68
    bool Read(cIGZIStream& stream) override;                         // @ 0x1000cc62a   [14] 0x70
    std::uint32_t GetGZCLSID() const override { return kPortDeveloperClassId; }   // @ 0x1000cde2e  [15] 0x78
    bool DoMessage(cIGZMessage2* message) override;                  // @ 0x1000cc7ba   [16] 0x80

    // The eight virtuals the derived developers must implement (pure in the original: the null slots at
    // 0x100a70140 and 0x100a70198..0x100a701c8). GetSimulatorType lives on cISC4PortDeveloper (slot 0x30);
    // the seven below follow DoMessage at 0x88..0xb8.
    virtual std::uint32_t GetTripsForPort(cPortLot& port) = 0;       // [17] 0x88
    virtual void OnStageChange(cPortLot& port) = 0;                  // [18] 0x90
    virtual void OnNewLotCreated(cPortLot& port) = 0;                // [19] 0x98
    virtual void SendAdvisorUpgradeMessage(cPortLot& port) = 0;      // [20] 0xa0
    virtual void DoMonthlyTick() = 0;                                // [21] 0xa8
    virtual void UpdateBudget() = 0;                                 // [22] 0xb0
    virtual std::int64_t GetAllocatedFunding() = 0;                  // [23] 0xb8

    // Non-virtual helpers of the original (public because the derived developers call them).
    void readTunableVars();                                          // @ 0x1000cc128
    std::int64_t GetRequiredBudget();                                // @ 0x1000ccddc
    bool GetPortUsageValues(const cSC4Lot* lot, std::uint32_t& usage,
                            std::uint32_t& capacity);                // @ 0x1000cce78
    void RecordHistoryData(std::uint32_t category, std::uint32_t value);  // @ 0x1000ccfaa
    void GrowPortsToNextStage();                                     // @ 0x1000cd04e
    void SendAdvisorMessage(std::uint32_t event);                    // @ 0x1000cddea
    void DoLotInserted(cSC4Lot* lot);                               // @ 0x1000ccc72
    void doLotStateChanged(cSC4Lot* lot, int oldState, int newState);   // @ 0x1000ccc0e
    cPortLot* findPortLot(cSC4Lot* lot);                            // @ 0x1000cd5fc
    int GetNumPortsBySize(int size);                                 // @ 0x1000cdb42
    bool GetPortLot(const cSC4Lot* lot, cPortLot*& out);            // @ 0x1000cdc74
    void doMonthlySimulation();                                      // @ 0x1000cc950
    bool swapLot(cPortLot& port, std::uint32_t lotConfigurationId);  // @ 0x1000cd116
    void removeLot(cPortLot* port);                                  // @ 0x1000cd28a
    bool loadLotConfiguration(int index);                            // @ 0x1000cd68a

    // State / accessors (the test and the derived developers use them).
    bool IsInitialized() const { return initialized_; }
    bool TunablesWereRead() const { return tunablesLoaded_; }
    std::uint32_t RefCount() const { return refCount_; }
    std::uint32_t PortDeveloperId() const { return portDeveloperId_; }        // +0x20
    int PortTypeCount() const { return portTypeCount_; }                      // +0x24
    std::uint16_t ZoneType() const { return zoneType_; }                      // +0x58
    std::uint32_t HistoryCategory() const { return historyCategory_; }        // +0x5c
    std::uint32_t AdvisorEventId() const { return advisorEventId_; }          // +0x60
    std::uint32_t BuildingGroup() const { return buildingGroup_; }            // +0x64
    const std::vector<cPortTypeConfig*>& Configurations() const { return configurations_; }  // +0x80
    const std::vector<std::uint32_t>& PortTypeIds() const { return portTypeIds_; }           // +0x98
    const std::list<cPortLot*>& PortLots() const { return portLots_; }        // +0x68
    std::size_t PortLotCount() const { return portLots_.size(); }
    cIGZMessageServer2* MessageServerForTest() const { return messageServer_; }

    // Original field offsets, asserted by the test.
    static constexpr std::uint32_t kOffsetPortDeveloperId = 0x20;
    static constexpr std::uint32_t kOffsetMessageServer = 0x28;
    static constexpr std::uint32_t kOffsetZoneManager = 0x30;
    static constexpr std::uint32_t kOffsetLotManager = 0x38;
    static constexpr std::uint32_t kOffsetCity = 0x40;
    static constexpr std::uint32_t kOffsetBudget = 0x48;
    static constexpr std::uint32_t kOffsetZoneType = 0x58;
    static constexpr std::uint32_t kOffsetHistoryCategory = 0x5c;
    static constexpr std::uint32_t kOffsetAdvisorEventId = 0x60;
    static constexpr std::uint32_t kOffsetBuildingGroup = 0x64;
    static constexpr std::uint32_t kOffsetPortLots = 0x68;
    static constexpr std::uint32_t kOffsetConfigurations = 0x80;
    static constexpr std::uint32_t kOffsetPortTypeIds = 0x98;
    static constexpr std::uint32_t kSeaportZoneType = 12;
    static constexpr std::uint32_t kAirportZoneType = 11;
    static constexpr std::uint32_t kClassSize = 0xb0;

protected:
    // What the derived developers' constructors write into the base object (the airport/seaport ctors):
    // the exemplar instance, the zone type, the history category, the advisor event and the occupant group.
    void SetIdentity(std::uint32_t portDeveloperId, std::uint16_t zoneType, std::uint32_t historyCategory,
                     std::uint32_t advisorEventId, std::uint32_t buildingGroup);

    // The two derived developers iterate the port list directly.
    std::list<cPortLot*>& MutablePortLots() { return portLots_; }
    std::vector<cPortTypeConfig*>& MutableConfigurations() { return configurations_; }

private:
    // +0x18
    bool tunablesLoaded_ = false;                    // +0x18
    bool initialized_ = false;                       // +0x19
    std::uint16_t reserved1a_ = 0;                   // +0x1a
    std::uint32_t refCount_ = 0;                     // +0x1c
    std::uint32_t portDeveloperId_ = 0;              // +0x20
    std::int32_t portTypeCount_ = 0;                 // +0x24
    cIGZMessageServer2* messageServer_ = nullptr;    // +0x28
    IPortZoneManagerApi* zoneManager_ = nullptr;     // +0x30
    IPortLotManagerApi* lotManager_ = nullptr;       // +0x38
    IPortCityApi* city_ = nullptr;                   // +0x40
    IPortBudgetApi* budget_ = nullptr;               // +0x48
    std::uint32_t reserved50_ = 0;                   // +0x50 (never read)
    std::uint32_t reserved54_ = 0;                   // +0x54 (never read)
    std::uint16_t zoneType_ = 0;                     // +0x58
    std::uint16_t reserved5a_ = 0;                   // +0x5a
    std::uint32_t historyCategory_ = 0;              // +0x5c
    std::uint32_t advisorEventId_ = 0;               // +0x60
    std::uint32_t buildingGroup_ = 0;                // +0x64
    std::list<cPortLot*> portLots_;                  // +0x68
    std::vector<cPortTypeConfig*> configurations_;   // +0x80
    std::vector<std::uint32_t> portTypeIds_;         // +0x98
};

}  // namespace sc4
