// cSC4LandfillDeveloper — the landfill (garbage) developer: it owns the city's cSC4LandfillZone objects,
// accumulates their capacity / stored garbage, drives the monthly garbage intake, keeps the department
// budget line and records the garbage history. Recovered from Sim City 4 Deluxe Edition 1.2.1
// (0x100060940..0x1000622b8; class id 0xea09d4ae, interface id 0x8a09d502, simulator type 0x2a398ed6).
//
// It is *not* a cSC4PortDeveloper. The Itanium RTTI of the three vtables (typeinfo 0x100a6bb10) says
//   cSC4LandfillDeveloper : cISC4LandfillDeveloper (offset 0), cIGZMessageTarget2 (+8), cIGZSerializable (+0x10)
// and cISC4LandfillDeveloper : cIGZUnknown, so only the GZCOM message-target/serializable layout is shared
// with the port developer (cSC4PropDeveloper has the same shape). The class is concrete: the factory
// (GZCOM_CreatecSC4LandfillDeveloper, 0x100060940) is the inlined constructor + operator new(0x88).
//
// Primary vtable (0x100a6ba00), i.e. the cISC4LandfillDeveloper view:
//   [ 0] 0x00 QueryInterface          [ 1] 0x08 AddRef                      [ 2] 0x10 Release
//   [ 3] 0x18 Init                    [ 4] 0x20 Shutdown                    [ 5] 0x28 GetEfficiency
//   [ 6] 0x30 GetSimulatorType        [ 7] 0x38 GetMaintenanceCosts         [ 8] 0x40 GetCurrentBudget
//   [ 9] 0x48 GetZoningCost           [10] 0x50 GetLandfillUsage            [11] 0x58 GetLandfillTileCount
//   [12] 0x60 GetLandfillZoneFromCell [13] 0x68 Write                      [14] 0x70 Read
//   [15] 0x78 GetGZCLSID              [16] 0x80 DoMessage
// The secondary vtables are cIGZMessageTarget2 at +8 (vtable 0x100a6ba98, offset-to-top -8: the QI/AddRef/
// Release/DoMessage thunks) and cIGZSerializable at +0x10 (0x100a6bac8, -0x10: QI/AddRef/Release/Write/
// Read/GetGZCLSID thunks). This C++ class declares its bases in that order, so the compiler emits the same
// three vptrs, the same adjustor thunks and the same data offsets (0x100060940's operator new size is 0x88).
//
// IID map (QueryInterface 0x100060a94): 1 and 0x8a09d502 -> the primary view (the 0x8a09d502 branch shares
// the `*out = this` path), 0x452294aa -> +8 (cIGZMessageTarget2), 0xe4fda3d4 -> +0x10 (cIGZSerializable),
// everything else -> 0. One reference count (+0x1c) is shared by the three views.
//
// Original layout (0x88 bytes):
//   +0x00 the three vptrs
//   +0x18 bool initialised (Init / Shutdown gate)
//   +0x1c uint32 reference count
//   +0x20 bool "zones must be Init'ed when the city initialises" (set by Read, consumed by 0x26d31ec1)
//   +0x28 cIGZMessageServer2   +0x30 cISC4City   +0x38 cISC4PollutionSimulator   +0x40 cSTETerrain
//   +0x48 cISC4BudgetSimulator +0x50 the garbage department budget (id 0x6a42a8f5; taken in 0x26d31ec1)
//   +0x58 std::list<cSC4LandfillZone*> (0x18 bytes: sentinel at +0x58/+0x60, size +0x68)
//   +0x70 int64 total maintenance cost (UpdateBudgetRequirements)
//   +0x78 uint32 total landfill capacity, +0x7c uint32 garbage stored in the landfills (UpdateCapacityValues)
//   +0x80 bool tuning exemplar already read
//
// Messages (AddNotification on the +8 view, 0x100060c6c; RemoveNotification, 0x100060f4c):
//   0x07699455 zone change   (void2 = SC4CellRegion<int>*, data3 = zone type)  -> DoZoneChange
//   0x26d31ec1 city initialised -> take the garbage department, Init the loaded zones, budget + capacity
//   0x0772fad4 funds changed (kBudgetMessageFundsChanged) -> enable/disable the garbage truck generator
//   0x278e6003 terrain changed (data1/data2 = packed cell rect, data4 = reason 1..3) -> DoTerrainChanged
//   0xc6cdbf1a view level changed (kMsgViewLevelInfo) -> start/stop the seagull flocks
//   0x66956816 monthly simulation -> DoMonthlySimulation
// History records go out as cRZMessage2Standard type 0x89efa536 with data1 = 0x2a8c4696 (capacity) or
// 0x2a8c4697 (garbage) and data2 = the value (RecordHistoryData).
//
// Tuning: ReadTuningParams (0x100060e34) loads the exemplar {0x6534284a, 0x67cd5fa1, 0x6a2806af} with iid
// 0xa52160f5 and reads eight properties into the class statics (see LandfillTunables in LandfillZone.h);
// the zones read the same values through ILandfillZoneHost::Tunables(), which the host points here.
//
// Quirks kept:
//  * Release destroys the object as soon as the count is below 2 and does not decrement it (0x100060b1e:
//    `if (1 < count) { --count; return count; } FinalRelease(); return 0;`); FinalRelease runs Shutdown and
//    then the inlined destructor (which clears the zone list) + operator delete.
//  * Read sets +0x20 *before* it validates the version word, so even a failed Read arms the zone Init pass.
//  * Read leaks a zone whose own Read fails: the object is constructed and AddRef'd but only inserted into
//    the list on success, and the failure path never releases it (0x1000611ca).
//  * A zone change with zone type 0xe merges into the first overlapping zone only; every other overlapping
//    zone is dezoned through HandleZoneOverlap(false, false), and only a change with *no* overlapping zone
//    creates a new cSC4LandfillZone (0x1000617ea..0x10006192d).
//  * The overlap window is inflated by one cell on every side ({x0-1, z0-1, x1+1, z1+1}, the PADDD at
//    0x1000616e2 with { -1, -1, 1, 1 }) and then shrunk back for the level-height call.
//  * DezoneArea marks the zone "known dezoned" (+0x40) when it has no valid garbage heap left, without
//    checking whether any cell is still set (0x1000620f6).
//  * UpdateCapacityValues saturates both running sums at 0xffffffff instead of wrapping (CARRY4 clamp).
//  * GetLandfillUsage reports min(garbage, capacity) as the "used" value, so the pair is always
//    used <= capacity even when a heap is over-filled (0x100062286).
//  * GetEfficiency returns 1.0 when the maintenance cost is not positive (0x100062230).
//  * GetLandfillTileCount counts the city's zone type 0xe cells (zone manager vslot 0x30) rather than the
//    zones this developer owns (0x100062252).
//  * DoMonthlySimulation clamps the garbage supply to [0, kMaxGarbageToTakePerMonth] and then scales it by
//    budget / maintenance, truncating toward zero (CVTTSS2SI at 0x100061566).
//  * The no-zone branch of DoMonthlySimulation posts both history records, then calls
//    UpdateBudgetRequirements, and only then sets the garbage capacity to 0 (tail call at 0x1000616a0).
//  * DoMessage handles kMsgViewLevelInfo (0xc6cdbf1a) but not the kMsgViewLevelInfoDone twin.
//  * 0x278e6003 builds z0 as (data2 >> 16) - 1 without masking it to 16 bits (x0 masks with 0xffff) and
//    compares the z1 clamp through float (0x100061413/0x100061568).
//  * Shutdown releases the stored city pointer first and then reaches the simulator through the *global*
//    SL::spCity (vslot 0x190 -> simulator vslot 0xa8 RemoveAgent), like cSC4PortDeveloper.
//  * The dezone arm of DoZoneChange passes updateZoneManager = false, so this developer never tells the zone
//    manager about a dezone (the caller of the 0x07699455 message does it); only RemoveZone's sweep passes
//    true (0x10006181a vs 0x100061945).
//  * A second 0x26d31ec1 overwrites the stored garbage department without releasing the previous reference
//    (0x1000613c9).
//  * The GetSeaLevel call inside GetAltitudeVariance (terrain vslot 0xe8, 0x100061d43) clamps altitudes
//    below sea level + 1 up to sea level + 1 (0x100061dcb..0x100061dd8).
//
// Deviations (documented, they cannot be preserved):
//  * UpdateBudgetRequirements calls SetFullExpenses/Release without checking that the line item exists
//    (only the AddRef is guarded, 0x1000619d6). Here a missing line item is skipped instead of crashing.
//  * GetCurrentBudget dereferences the department unchecked in the original; here a missing department or
//    line item returns 0.
//  * GetZoningCost passes an uninitialised fourth (minimum altitude) argument to GetAltitudeVariance; this
//    port passes 0 because the value is never read.
//
// Every call outside the object goes through ILandfillDeveloperHost (one method per original call, with the
// original global and vtable slot in the comment). The zones are the recovered cSC4LandfillZone
// (LandfillZone.h) built with the host's ILandfillZoneHost; the messages are real cRZMessage2Standard
// objects; the property holder is the recovered cISCPropertyHolder; the tuning statics live in
// LandfillDeveloperTunables() so that the zone host can hand the zones the same values.
#pragma once

#include <cstddef>
#include <cstdint>
#include <list>

#include "gz/cGZPersistResourceKey.h"
#include "gz/cIGZMessage2.h"
#include "gz/cIGZSerializable.h"
#include "gz/cIGZIStream.h"
#include "gz/cIGZOStream.h"
#include "sc4/LandfillZone.h"
#include "sc4/SCProperty.h"

namespace sc4 {

constexpr std::uint32_t kGZCLSID_cSC4LandfillDeveloper = 0xea09d4ae;   // GetGZCLSID @ 0x1000622a0 (thunk 0x1000622ac)
constexpr std::uint32_t kGZIID_cISC4LandfillDeveloper = 0x8a09d502;    // QueryInterface branch @ 0x100060ab8
constexpr std::uint32_t kLandfillMessageTargetIid = 0x452294aa;        // cIGZMessageTarget2 (+8)
constexpr std::uint32_t kLandfillSerializableIid = 0xe4fda3d4;         // cIGZSerializable (+0x10)
constexpr std::uint32_t kSimulatorTypeLandfill = 0x2a398ed6;           // GetSimulatorType

constexpr std::uint32_t kLandfillDeveloperStreamVersion = 3;          // Write 0x1000610f7 (a uint16)
constexpr std::uint32_t kLandfillZoneTypeLandfill = 0x0e;             // the zone type a landfill change carries

// Messages (see the list above).
constexpr std::uint32_t kLandfillMessageZoneChange = 0x07699455;
constexpr std::uint32_t kLandfillMessageCityInit = 0x26d31ec1;
constexpr std::uint32_t kLandfillMessageFundsChanged = 0x0772fad4;
constexpr std::uint32_t kLandfillMessageTerrainChanged = 0x278e6003;
constexpr std::uint32_t kLandfillMessageViewLevel = 0xc6cdbf1a;
constexpr std::uint32_t kLandfillMessageMonthlySimulation = 0x66956816;
constexpr std::uint32_t kLandfillHistoryMessageType = 0x89efa536;     // RecordHistoryData's message type
constexpr std::uint32_t kLandfillHistoryCapacity = 0x2a8c4696;
constexpr std::uint32_t kLandfillHistoryGarbage = 0x2a8c4697;

// The budget department / line item / tuning exemplar the developer uses.
constexpr std::uint32_t kLandfillBudgetDepartment = 0x6a42a8f5;       // the garbage department (0x1000613be)
constexpr std::uint32_t kLandfillBudgetLine = 0x1e;                   // GetLineItem(30) for the maintenance cost
constexpr std::uint32_t kLandfillTuningExemplarIid = 0xa52160f5;      // cISCResExemplar
constexpr cGZPersistResourceKey kLandfillTuningExemplarKey{0x6534284a, 0x67cd5fa1, 0x6a2806af};
constexpr const char* kLandfillAgentName = "cSC4LandfillDeveloper";   // simulator AddAgent name
constexpr int kLandfillAgentInterval = 6;                             // simulator AddAgent interval
constexpr std::uint32_t kLandfillAgentFlags = 2;                      // simulator AddAgent flags

// ---- seam interfaces ---------------------------------------------------------------------------
// cISC4City, vtable slots [58] 0x2e8 / [59] 0x2f0 / [60] 0x308 (cSC4City, vtable 0x100a7... )
class ILandfillCityApi : public cIGZUnknown {
public:
    virtual int CellCountX() const = 0;                                      // 0x2e8
    virtual int CellCountZ() const = 0;                                      // 0x2f0
    virtual void CellToWorld(int x, int z, float& outX, float& outZ) = 0;    // 0x308
};

// cSTETerrain (vtable 0x100aea6b0).
class ILandfillTerrainApi : public cIGZUnknown {
public:
    virtual float GetAltitude(float x, float z) const = 0;                   // [12] 0x60
    virtual float GetSeaLevel() const = 0;                                   // [29] 0xe8
    // [69] 0x228: the original calls it as terrain->0x228(terrain, 1, 1, &rect, 5).
    virtual void RefreshRect(const LandfillRect& rect) = 0;
};

// cISC4PollutionSimulator (vtable 0x100a6ce10).
class ILandfillPollutionApi : public cIGZUnknown {
public:
    virtual int GetGarbageSupply() = 0;                                      // [20] 0xa0
    virtual int GetTotalGarbageUncollected() const = 0;                      // [24] 0xc0
    virtual void SetGarbageCapacity(int capacity) = 0;                       // [30] 0xf0
};

// cSC4LineItem (vtable 0x100a88f48): the maintenance-cost line of the garbage department.
class ILandfillBudgetLine : public cIGZUnknown {
public:
    virtual std::int64_t GetCurrentExpenses() const = 0;                     // [11] 0x58
    virtual void SetFullExpenses(std::int64_t value) = 0;                    // [12] 0x60
};

// cSC4DepartmentBudget (vtable 0x100a88dc0) for the garbage department 0x6a42a8f5.
class ILandfillDepartmentBudget : public cIGZUnknown {
public:
    virtual const float& GetFundingPercentage() const = 0;                   // [10] 0x50
    virtual ILandfillBudgetLine* GetLineItem(std::uint32_t id) = 0;          // [20] 0xa0
};

// cSC4BudgetSimulator (vtable 0x100a87130).
class ILandfillBudgetApi : public cIGZUnknown {
public:
    virtual ILandfillDepartmentBudget* GetDepartmentBudget(std::uint32_t id) = 0;   // [27] 0xd8
};

// One method per original call into an unrecovered/global service.
class ILandfillDeveloperHost {
public:
    virtual ~ILandfillDeveloperHost() = default;
    virtual cIGZMessageServer2* MessageServer() = 0;                         // SL::spMS2 (0x100b212c8)
    virtual ILandfillCityApi* City() = 0;                                    // SL::spCity (0x100b21308)
    virtual ILandfillTerrainApi* Terrain() = 0;                              // SL::spTerrain (0x100b21318)
    virtual ILandfillPollutionApi* Pollution() = 0;                          // SL::spPollutionSim (0x100b21468)
    virtual ILandfillBudgetApi* Budget() = 0;                                // SL::spBudgetSim (0x100b213f8)
    // SL::spRM (0x100b21280) vslot 0x18 for kLandfillTuningExemplarKey with kLandfillTuningExemplarIid,
    // then its vslot 0x30 (AsPropertyHolder). Null when the exemplar is absent.
    virtual cISCPropertyHolder* TuningExemplar() = 0;
    // The host the cSC4LandfillZone objects are built with (they need the same city/terrain services).
    virtual ILandfillZoneHost* ZoneHost() = 0;
    // SL::spSimulator (0x100b213e8) vslot 0x98 / SL::spCity vslot 0x190 -> simulator vslot 0xa8.
    virtual void AddSimulatorAgent(cIGZMessageTarget2* target, int interval, const char* name,
                                   std::uint32_t flags) = 0;
    virtual void RemoveSimulatorAgent(cIGZMessageTarget2* target) = 0;
    virtual std::uint32_t ViewLevel() = 0;                                   // SL::spView3D 0xb0
    virtual std::int32_t LandfillTileCount() = 0;                            // SL::spZoneManager 0x30 GetZoneCount(0xe)
};
ILandfillDeveloperHost* LandfillDeveloperHost();
void SetLandfillDeveloperHost(ILandfillDeveloperHost* host);

// The class's static tuning values (the original's file-scope globals at 0x100afdff0..0x100afe00b and
// 0x100b214f8). ReadTuningParams writes them; the zone host hands the same struct to the zones.
LandfillTunables& LandfillDeveloperTunables();
void ResetLandfillDeveloperTunables();

// ---------------------------------------------------------------------------------------------------
// cISC4LandfillDeveloper: the primary COM view (17 slots, vtable 0x100a6ba00).
// ---------------------------------------------------------------------------------------------------
class cSC4LandfillDeveloper;

class cISC4LandfillDeveloper : public cIGZUnknown {
public:
    virtual bool Init() = 0;                                                          // [3]  0x18
    virtual bool Shutdown() = 0;                                                      // [4]  0x20
    virtual float GetEfficiency() = 0;                                                // [5]  0x28
    virtual std::uint32_t GetSimulatorType() = 0;                                     // [6]  0x30
    virtual std::int64_t GetMaintenanceCosts() = 0;                                   // [7]  0x38
    virtual std::int64_t GetCurrentBudget() = 0;                                      // [8]  0x40
    virtual std::int32_t GetZoningCost(const LandfillCellRegion* region) = 0;          // [9]  0x48
    virtual void GetLandfillUsage(std::uint32_t& used, std::uint32_t& capacity) = 0;  // [10] 0x50
    virtual std::int32_t GetLandfillTileCount() const = 0;                            // [11] 0x58
    virtual cSC4LandfillZone* GetLandfillZoneFromCell(int x, int z) = 0;              // [12] 0x60
};

class cSC4LandfillDeveloper final : public cISC4LandfillDeveloper, public cIGZMessageTarget2,
                                    public cIGZSerializable {
public:
    cSC4LandfillDeveloper();   // @ 0x100060940 (inlined in GZCOM_CreatecSC4LandfillDeveloper)
    ~cSC4LandfillDeveloper();

    bool QueryInterface(std::uint32_t iid, void** out) override;   // @ 0x100060a94 (+ thunks 0x100060ad8/0x100060ae6)
    std::uint32_t AddRef() override;                               // @ 0x100060af4 (+ thunks 0x100060b02/0x100060b10)
    std::uint32_t Release() override;                              // @ 0x100060b1e (+ thunks 0x100060b98/0x100060c02)

    bool Init() override;                                          // @ 0x100060c6c
    bool Shutdown() override;                                      // @ 0x100060f4c
    float GetEfficiency() override;                                // @ 0x10006221a
    std::uint32_t GetSimulatorType() override;                     // @ 0x100062270
    std::int64_t GetMaintenanceCosts() override;                   // @ 0x10006227c
    std::int64_t GetCurrentBudget() override;                      // @ 0x100062012
    std::int32_t GetZoningCost(const LandfillCellRegion* region) override;          // @ 0x100061e70
    void GetLandfillUsage(std::uint32_t& used, std::uint32_t& capacity) override;   // @ 0x100062286
    std::int32_t GetLandfillTileCount() const override;                             // @ 0x100062252
    cSC4LandfillZone* GetLandfillZoneFromCell(int x, int z) override;               // @ 0x1000621d8

    bool Write(cIGZOStream& stream) const override;                // @ 0x1000610dc (thunk 0x100061134)
    bool Read(cIGZIStream& stream) override;                       // @ 0x100061146 (thunk 0x100061228)
    std::uint32_t GetGZCLSID() const override;                     // @ 0x1000622a0 (thunk 0x1000622ac)

    bool DoMessage(cIGZMessage2* message) override;                // @ 0x100061236 (thunk 0x100061b10)

    // Non-virtual helpers (the original calls them directly).
    void FinalRelease();                                           // @ 0x100060b3a
    void UpdateCapacityValues();                                   // @ 0x1000609ec
    void UpdateBudgetRequirements();                               // @ 0x10006197a
    void DoMonthlySimulation();                                    // @ 0x1000614e4
    void DoZoneChange(const LandfillCellRegion* region, std::uint32_t zoneType);     // @ 0x1000616a2
    void DoTerrainChanged(const LandfillRect& rect);                                 // @ 0x100061a2c
    void RemoveZone(cSC4LandfillZone* zone, bool dezoneCells);                       // @ 0x100061b22
    void HandleZoneOverlap(cSC4LandfillZone* zone, const LandfillCellRegion& region,
                           bool updateZoneManager, bool removeTile);                 // @ 0x100061c12
    // average = mean altitude, maximum / minimum = the extremes; returns |maximum - minimum|.
    float GetAltitudeVariance(const LandfillRect& rect, float& average, float& maximum,
                              float& minimum);                                   // @ 0x100061cb8
    void RecordHistoryData(std::uint32_t id, std::uint32_t value);                     // @ 0x100062032
    void DezoneArea(cSC4LandfillZone& zone, const LandfillRect& rect,
                    const LandfillCellRegion& region, bool updateZoneManager,
                    bool removeTile);                                                // @ 0x100062120
    static void ReadTuningParams();                                                  // @ 0x100060e34

    // State / accessors (used by the test and by the zone host).
    bool IsInitialized() const { return initialised_; }             // +0x18
    std::uint32_t RefCount() const { return refCount_; }            // +0x1c
    bool PendingZoneInit() const { return pendingZoneInit_; }       // +0x20
    bool TunablesWereRead() const { return tunablesRead_; }         // +0x80
    std::uint32_t TotalCapacity() const { return capacity_; }       // +0x78
    std::uint32_t StoredGarbage() const { return garbage_; }        // +0x7c
    std::int64_t MaintenanceCost() const { return maintenanceCost_; }   // +0x70
    const std::list<cSC4LandfillZone*>& Zones() const { return zones_; }  // +0x58
    std::size_t ZoneCount() const { return zones_.size(); }              // +0x68
    cIGZMessageServer2* MessageServerForTest() const { return messageServer_; }   // +0x28
    ILandfillCityApi* CityForTest() const { return city_; }                       // +0x30
    ILandfillPollutionApi* PollutionForTest() const { return pollution_; }        // +0x38
    ILandfillTerrainApi* TerrainForTest() const { return terrain_; }              // +0x40
    ILandfillBudgetApi* BudgetForTest() const { return budget_; }                 // +0x48
    ILandfillDepartmentBudget* DepartmentForTest() const { return department_; }  // +0x50

    // Original field offsets / sizes, asserted by the test.
    static constexpr std::uint32_t kOffsetInitialised = 0x18;
    static constexpr std::uint32_t kOffsetRefCount = 0x1c;
    static constexpr std::uint32_t kOffsetPendingZoneInit = 0x20;
    static constexpr std::uint32_t kOffsetMessageServer = 0x28;
    static constexpr std::uint32_t kOffsetCity = 0x30;
    static constexpr std::uint32_t kOffsetPollution = 0x38;
    static constexpr std::uint32_t kOffsetTerrain = 0x40;
    static constexpr std::uint32_t kOffsetBudget = 0x48;
    static constexpr std::uint32_t kOffsetDepartment = 0x50;
    static constexpr std::uint32_t kOffsetZones = 0x58;
    static constexpr std::uint32_t kOffsetMaintenanceCost = 0x70;
    static constexpr std::uint32_t kOffsetCapacity = 0x78;
    static constexpr std::uint32_t kOffsetGarbage = 0x7c;
    static constexpr std::uint32_t kOffsetTunablesRead = 0x80;
    static constexpr std::uint32_t kClassSize = 0x88;

private:
    bool initialised_ = false;                              // +0x18
    std::uint32_t refCount_ = 0;                            // +0x1c
    bool pendingZoneInit_ = false;                          // +0x20
    cIGZMessageServer2* messageServer_ = nullptr;           // +0x28
    ILandfillCityApi* city_ = nullptr;                      // +0x30
    ILandfillPollutionApi* pollution_ = nullptr;            // +0x38
    ILandfillTerrainApi* terrain_ = nullptr;                // +0x40
    ILandfillBudgetApi* budget_ = nullptr;                  // +0x48
    ILandfillDepartmentBudget* department_ = nullptr;       // +0x50
    std::list<cSC4LandfillZone*> zones_;                    // +0x58
    std::int64_t maintenanceCost_ = 0;                      // +0x70
    std::uint32_t capacity_ = 0;                            // +0x78
    std::uint32_t garbage_ = 0;                             // +0x7c
    bool tunablesRead_ = false;                             // +0x80
};

static_assert(sizeof(cSC4LandfillDeveloper) == cSC4LandfillDeveloper::kClassSize,
              "cSC4LandfillDeveloper is 0x88 bytes in the original (operator new at 0x100060946)");

}  // namespace sc4
