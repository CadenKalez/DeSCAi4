// cSC4AirportDeveloper — the airport developer (29 primary symbols, 0x1000c8034..0x1000c8ed2; class id
// 0xaa2049a1). It is the airport concrete subclass of the recovered cSC4PortDeveloper: the base owns the port
// lots, the growth configurations, the monthly simulation and the stream format, and this class supplies the
// eight port-type virtuals plus the airport department budget and the airport-specific trip computation
// (airports absorb the city's office traffic on the subnetworks their lot touches).
//
// RTTI (vmi typeinfo 0x100a6f930, base_count 2):
//   cSC4AirportDeveloper : cISC4AirportDeveloper (offset 0, public), cSC4PortDeveloper (offset 8, public)
//   cISC4AirportDeveloper : cISC4PortDeveloper (typeinfo 0x100aff560, vtable 0x100aff578: 15 pure slots)
// The object is 0xb8 bytes in the original: the interface subobject (one vptr) at +0x00, then the whole
// cSC4PortDeveloper (0xb0 bytes, three vptrs and its data) at +0x08. The four vtables are:
//   +0x00 cISC4AirportDeveloper  0x100a6f700   29 slots (below)
//   +0x08 cISC4PortDeveloper     0x100a6f7f8   24 slots (cSC4PortDeveloper's own vtable, adjustor thunks)
//   +0x10 cIGZMessageTarget2     0x100a6f8c8    4 slots (QI/AddRef/Release/DoMessage thunks)
//   +0x18 cIGZSerializable       0x100a6f8f8    6 slots (QI/AddRef/Release/Write/Read/GetGZCLSID thunks)
//
// The +0x00 vtable (0x100a6f700), by slot:
//   [ 0] 0x00 QueryInterface     [ 1] 0x08 AddRef                 [ 2] 0x10 Release
//   [ 3] 0x18 Init               [ 4] 0x20 Shutdown               [ 5] 0x28 GetAvgEfficiency
//   [ 6] 0x30 GetSimulatorType  [ 7] 0x38 GetLeastEfficientPort  [ 8] 0x40 GetAllPortLots
//   [ 9] 0x48 GetNumPorts       [10] 0x50 AsISC4PortDeveloper     [11] 0x58 GetNumAirports
//   [12] 0x60 GetAirportUsageValues  [13] 0x68 GetNumAirportTrips [14] 0x70 GetMaintenanceCost
//   [15] 0x78 ~cSC4AirportDeveloper  [16] 0x80 deleting dtor
//   [17] 0x88 FinalRelease      [18] 0x90 Write                    [19] 0x98 Read
//   [20] 0xa0 GetGZCLSID        [21] 0xa8 DoMessage                [22] 0xb0 UpdateBudget
//   [23] 0xb8 GetTripsForPort   [24] 0xc0 OnStageChange            [25] 0xc8 OnNewLotCreated
//   [26] 0xd0 SendAdvisorUpgradeMessage  [27] 0xd8 DoMonthlyTick    [28] 0xe0 GetAllocatedFunding
// Slots 0..9 are the cISC4PortDeveloper view of the interface branch; 10..14 are cISC4AirportDeveloper's own
// five virtuals. The destructor pair comes next because cISC4AirportDeveloper has no virtual destructor, and
// the tail (17..28) is the cSC4PortDeveloper machinery in the derived class's *declaration order*
// (UpdateBudget is declared before the seven growth virtuals in the original, which is why it is at 0xb0 here).
// The C++ declaration below repeats that order so the generated vtable matches; the secondary vtables get the
// usual adjustor thunks (this-8) automatically.
//
// Identity written by the constructor (the constructor itself has no symbol: GZCOM_CreatecSC4AirportDeveloper
// 0x1000c8034 inlines it):
//   port developer id (base +0x20, the ini exemplar instance)          0xca270e9d
//   zone type (base +0x58)                                            11
//   history category (base +0x5c)                                     0x6a3e9032
//   advisor event id (base +0x60)                                     0x6a4119f0
//   building occupant group (base +0x64)                              0x1508
//   simulator type (GetSimulatorType, slot 0x30)                      0xea3955c4
//   class id (GetGZCLSID)                                             0xaa2049a1
//   QueryInterface iid (cISC4AirportDeveloper)                        0xea20548d
//
// Airport department budget: Init gets (or creates) the department 0x6a4119f0 from SL::spBudgetSim
// (GetDepartmentBudget vslot 0xd8 / CreateDepartmentBudget vslot 0xc8, group 0xaa369059), names it
// (0x6a3ff01c, 0xea493564), caps its funding at 120% (the float at 0x100a16ee0), creates line item 1 and
// clears its display flag 1. The airport's share of the line item's current/full expenses is the funding
// ratio that scales every capacity and maintenance figure. The original keeps the department pointer in the
// base's otherwise unused +0x50 slot; the recovered base keeps that field private, so this port stores the
// pointer in its own member (see the deviations below).
//
// DoMonthlyTick (0x1000c8a46): the monthly base pass calls this before doMonthlySimulation. It asks the
// traffic simulator for the occupants of office type 0x3320 and 0x3330 on every subnetwork
// (GetOccupantCountForAllSubnetworks, vslot 0x150), zeroes every port's trips, buckets each port by the
// subnetworks its lot touches (GetSubnetworksForLot, vslot 0x138), then per subnetwork spreads the office
// occupants over the airports on it in proportion to their current-stage capacity scaled by the funding
// ratio. Airports therefore gain trips from office growth across their road access.
//
// Messages: Init also registers the six advisor upgrade messages this developer sends (see SendAdvisor...);
// DoMessage intercepts them and grows the ports, forwarding every other message to the base.
//
// Deviations kept in this port (the first two are forced by the recovered base's interface):
//  * The department budget pointer lives in a member of this class (sizeof 0xc0 instead of the original
//    0xb8) because cSC4PortDeveloper's +0x50 field is private and unreachable from a derived class.
//  * The base's IPortDeveloperHost does not expose the budget simulator or the city's traffic simulator, so
//    IAirportDeveloperHost extends it and is installed through its own global (like SetPortDeveloperHost;
//    the original reaches the same two SL::sp* objects through the base's budget/city pointers). Init reports
//    failure when no airport host is installed.
//  * Null hosts, ports, configurations, lots and line items report a zero/false instead of faulting, as the
//    rest of the recovered port code does.
//  * The unchecked stage-index reads (GetMaintenanceCost) and the unchecked subnetwork-index write in
//    DoMonthlyTick are clamped; a zero scaled capacity (funding 0) leaves the trips at zero instead of
//    dividing by zero (the original's CVTTSS2SI of inf yields INT_MIN and corrupts the trip count).
//  * GetMaintenanceCost really reads the stage's *budget* (+0x08 of cPortGrowthRule), not its cost (+0x18).
#pragma once

#include <cstdint>
#include <list>
#include <vector>

#include "gz/cIGZMessage2.h"
#include "sc4/BudgetSimulator.h"
#include "sc4/PortDeveloper.h"
#include "sc4/TrafficSimulator.h"

namespace sc4 {

class cSC4AirportDeveloper;

constexpr std::uint32_t kGZCLSID_cSC4AirportDeveloper = 0xaa2049a1;   // GZCOM_cSC4AirportDeveloperCLSID
constexpr std::uint32_t kGZIID_cISC4AirportDeveloper = 0xea20548d;     // the airport QueryInterface iid
constexpr std::uint32_t kAirportSimulatorType = 0xea3955c4;           // GetSimulatorType
constexpr std::uint32_t kAirportDeveloperId = 0xca270e9d;              // the ini exemplar instance
constexpr std::uint16_t kAirportZoneType = 11;
constexpr std::uint32_t kAirportHistoryCategory = 0x6a3e9032;
constexpr std::uint32_t kAirportAdvisorEventId = 0x6a4119f0;           // == the department id
constexpr std::uint32_t kAirportBuildingGroup = 0x1508;
constexpr std::uint32_t kAirportDepartmentGroup = 0xaa369059;          // CreateDepartmentBudget group
constexpr std::uint32_t kAirportDepartmentNameGroup = 0x6a3ff01c;      // SetDepartmentName(group, instance)
constexpr std::uint32_t kAirportDepartmentNameInstance = 0xea493564;
constexpr std::uint32_t kAirportDepartmentLineItem = 1;                // CreateLineItem / GetLineItem id
constexpr float kAirportMaxAllowedFundingPercentage = 1.2f;            // float at 0x100a16ee0
constexpr std::uint32_t kAirportOfficeMediumType = 0x3320;             // office developer type, medium
constexpr std::uint32_t kAirportOfficeHighType = 0x3330;               // office developer type, high

// The six advisor messages SendAdvisorUpgradeMessage posts; DoMessage intercepts them and grows the ports.
// One pair per port size class (1, 2, 3), the first for stage 0 and the second for every later stage.
constexpr std::uint32_t kAirportMessageSize1First = 0xea394b74;
constexpr std::uint32_t kAirportMessageSize1Later = 0xcb3be815;
constexpr std::uint32_t kAirportMessageSize2First = 0xeb3be415;
constexpr std::uint32_t kAirportMessageSize2Later = 0x2b3be44a;
constexpr std::uint32_t kAirportMessageSize3First = 0x6b3be6d8;
constexpr std::uint32_t kAirportMessageSize3Later = 0xb3be81a;

// The six ids in the original registration/removal order (Init 0x1000c82d6.., Shutdown 0x1000c83a0..).
inline constexpr std::uint32_t kAirportAdvisorMessages[6] = {
    kAirportMessageSize1First, kAirportMessageSize1Later, kAirportMessageSize2First,
    kAirportMessageSize2Later, kAirportMessageSize3First, kAirportMessageSize3Later,
};

// ---------------------------------------------------------------------------------------------------
// Everything the airport needs beyond IPortDeveloperHost. Both accessors are views of the SL::sp* globals the
// base's host already wraps; the host owns the objects.
// ---------------------------------------------------------------------------------------------------
class IAirportDeveloperHost : public IPortDeveloperHost {
public:
    // SL::spBudgetSim, the object IPortDeveloperHost::Budget() adapts: the airport uses the budget simulator's
    // department map directly (vslot 0xc8 CreateDepartmentBudget, 0xd8 GetDepartmentBudget). Borrowed.
    virtual cSC4BudgetSimulator* BudgetSimulator() = 0;
    // SL::spCity vslot 0x228 (cSC4City::GetTrafficSimulator): the recovered simulator, whose
    // GetOccupantCountForAllSubnetworks (vslot 0x150) and GetSubnetworksForLot (vslot 0x138) feed the airport
    // trips. Borrowed.
    virtual cSC4TrafficSimulator* TrafficSimulator() = 0;
};
// The airport host is installed separately from the base's (the two views wrap the same game objects); every
// base call still goes through PortDeveloperHost().
IAirportDeveloperHost* AirportDeveloperHost();
void SetAirportDeveloperHost(IAirportDeveloperHost* host);

// ---------------------------------------------------------------------------------------------------
// cISC4AirportDeveloper: the airport COM view, the primary base of the concrete class. It extends the port
// interface with the five airport calls (the 15 pure slots of vtable 0x100aff578).
// ---------------------------------------------------------------------------------------------------
class cISC4AirportDeveloper : public cISC4PortDeveloper {
public:
    // The concrete port implementation behind this interface branch (the cSC4PortDeveloper subobject at
    // +0x08), not the cISC4PortDeveloper subobject of this branch at +0x00.
    virtual cISC4PortDeveloper* AsISC4PortDeveloper() = 0;                          // [10] 0x50
    // ePortSize: 0 means "every airport" (the port list size); 1..3 are the size classes counted through
    // cSC4PortDeveloper::GetNumPortsBySize (1 << size).
    virtual int GetNumAirports(int size) = 0;                                       // [11] 0x58
    // The base's usage/capacity for the lot, with the capacity scaled by the department funding ratio and
    // the usage clamped to the scaled capacity.
    virtual bool GetAirportUsageValues(const cSC4Lot* lot, std::uint32_t& usage,
                                       std::uint32_t& capacity) = 0;                // [12] 0x60
    virtual int GetNumAirportTrips() = 0;                                           // [13] 0x68
    virtual std::int64_t GetMaintenanceCost(const cSC4Lot* lot) = 0;                // [14] 0x70

protected:
    ~cISC4AirportDeveloper() = default;
};

// ---------------------------------------------------------------------------------------------------
// The object (original layout 0xb8; see the deviations above for the extra member).
// ---------------------------------------------------------------------------------------------------
class cSC4AirportDeveloper : public cISC4AirportDeveloper, public cSC4PortDeveloper {
public:
    // The original constructor is inlined in GZCOM_CreatecSC4AirportDeveloper (0x1000c8034): base
    // construction, the airport vptrs, the identity and the zeroed department slot.
    cSC4AirportDeveloper();
    ~cSC4AirportDeveloper() override;                                // [15] 0x78 / [16] 0x80

    // cISC4PortDeveloper view (the primary base branch at +0x00)
    bool QueryInterface(std::uint32_t iid, void** out) override;     // @ 0x1000c8430
    std::uint32_t AddRef() override;                                 // @ 0x1000c84c4
    std::uint32_t Release() override;                                // @ 0x1000c84f8
    bool Init() override;                                            // @ 0x1000c81fc
    bool Shutdown() override;                                        // @ 0x1000c8362
    float GetAvgEfficiency() override;                               // @ 0x1000c872a
    std::uint32_t GetSimulatorType() const override;                 // @ 0x1000c8e5c
    bool GetLeastEfficientPort(SC4Percentage& efficiency, PortPoint& location) override;  // @ 0x1000c87f0
    void GetAllPortLots(std::vector<cSC4Lot*>& out) override;        // @ 0x1000c8e68
    int GetNumPorts(int sizeMask, int stageMask) override;           // @ 0x1000c8e76

    // cISC4AirportDeveloper (the primary base's own virtuals)
    cISC4PortDeveloper* AsISC4PortDeveloper() override;              // @ 0x1000c8e84
    int GetNumAirports(int size) override;                           // @ 0x1000c87c2
    bool GetAirportUsageValues(const cSC4Lot* lot, std::uint32_t& usage,
                               std::uint32_t& capacity) override;    // @ 0x1000c88c0
    int GetNumAirportTrips() override;                               // @ 0x1000c895e
    std::int64_t GetMaintenanceCost(const cSC4Lot* lot) override;    // @ 0x1000c89a2

    // The cSC4PortDeveloper virtuals, in the original's declaration order (see the vtable note above).
    void FinalRelease() override;                                    // @ 0x1000c852c   [17] 0x88
    bool Write(cIGZOStream& stream) const override;                  // @ 0x1000c85d0   [18] 0x90
    bool Read(cIGZIStream& stream) override;                         // @ 0x1000c8656   [19] 0x98
    std::uint32_t GetGZCLSID() const override;                       // @ 0x1000c8e8e   [20] 0xa0
    bool DoMessage(cIGZMessage2* message) override;                  // @ 0x1000c8544   [21] 0xa8
    void UpdateBudget() override;                                    // @ 0x1000c8808   [22] 0xb0
    std::uint32_t GetTripsForPort(cPortLot& port) override;          // @ 0x1000c87dc   [23] 0xb8
    void OnStageChange(cPortLot& port) override;                     // @ 0x1000c8742   [24] 0xc0
    void OnNewLotCreated(cPortLot& port) override;                   // @ 0x1000c874e   [25] 0xc8
    void SendAdvisorUpgradeMessage(cPortLot& port) override;         // @ 0x1000c875a   [26] 0xd0
    void DoMonthlyTick() override;                                   // @ 0x1000c8a46   [27] 0xd8
    std::int64_t GetAllocatedFunding() override;                     // @ 0x1000c8880   [28] 0xe0

    // The airport's department budget (owned: one reference held from Init to Shutdown).
    cSC4DepartmentBudget* DepartmentBudget() const { return department_; }

    static constexpr std::uint32_t kOffsetAirportInterface = 0x00;
    static constexpr std::uint32_t kOffsetPortDeveloper = 0x08;
    static constexpr std::uint32_t kOriginalClassSize = 0xb8;

private:
    cSC4LineItem* DepartmentLineItem();
    float FundingRatio();

    // Original base +0x50 (see the deviations above). Borrowed from the budget simulator, one reference held.
    cSC4DepartmentBudget* department_ = nullptr;
};

}  // namespace sc4
