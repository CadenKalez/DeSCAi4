// cSC4CivicBuildingSimulator — drives the ongoing cost and the service level of the city's civic buildings
// (schools, hospitals, libraries, museums, rewards). Class id 0x0a413481, interfaces 0x8a41322f (primary),
// 0x452294aa (cIGZMessageTarget2) and 0xe4fda3d4 (cIGZSerializable); 30 symbols, 0x1000e8dec..0x1000ea7a4.
//
// Two lists and two tables drive it:
//   * the landmark list (+0x90, size +0xa0): every building whose civic budget item (purpose 0xaa59670c) exists.
//     GetLandmarkCount (vslot 0x40) is its size; the monthly pass reads the item's department and sets the lot
//     condition of every landmark from the department's funding percentage (>= 50% clears a warning, < 50% raises
//     one);
//   * the zoo list (+0x78, size +0x88): buildings in occupant group 0x1917. Once a month there is a 1% chance that
//     a random one of them gets the "ape_escape" automata controller group (the animals get out);
//   * the excluded-building set (+0x28): ids taken from property 0xea2e078b of a building whose occupant enters or
//     leaves the occupant manager; IsBuildingExcluded (vslot 0x28) answers for them;
//   * the conditional building status map (+0x50): per building-exemplar instance (the resource key's instance word)
//     the advice state byte, a 16-bit building count and the reason text. The reward advice reads and writes the
//     records through vslots 0x30 / 0x38; this simulator keeps the count in step with occupant insert/remove.
//
// Maintenance cost accumulation: DoLotActivated writes the building's budget item cost into the line item of its
// building type in the item's department (department 0xa0(GetLineItem) / 0x90(CreateLineItemForBuildingType) then
// line item 0x68 AddToFullExpenses, 0x1000ea128); the second civic item (purpose 0x4a5a495e) instead adds the cost
// to the line item's income (line item 0x80) and DoBuildingRemoved subtracts it again (0x78 SetIncome), removing the
// line item once its income drops below 1. That asymmetry is the original's, kept.
//
// Original layout (0xa8 bytes): +0x00 primary vptr, +0x08 message-target vptr, +0x10 serializable vptr,
// +0x18 refcount, +0x1c initialised, +0x20 civic department (cISC4DepartmentBudget, serialized as 0x223e2b67),
// +0x28 unordered_set<uint32> excluded buildings (bucket list/ count / first node / size / max load factor at
// +0x28/+0x30/+0x38/+0x40/+0x48), +0x50 unordered_map<uint32, ConditionalBuildingStatus> (+0x50 buckets, +0x60
// first node, +0x68 size, +0x70 max load factor), +0x78 zoo list (sentinel +0x78/+0x80, size +0x88), +0x90 landmark
// list (sentinel +0x90/+0x98, size +0xa0).
//
// Quirks kept (addresses from the binary):
//   * ConditionalBuildingStatus is 0x30 bytes: a 32-bit state word whose low byte is the advice state and whose high
//     word is the building count (UpdateConditionalBuildingStatus 0x1000ea728 copies only that word and the text,
//     never the 0x04..0x0f padding), then a cRZString at +0x08 (vptr +0x08, std::string +0x10, refcount +0x20).
//   * Write (0x1000e9712) stores the state as a byte then a 16-bit word at +2, leaving byte +1 unserialized; the
//     read side therefore always restores 0 there.
//   * The building count is a 16-bit field the insert/remove path adds +1 / 0xffff to (0x1000e9dd8), so it wraps at
//     0xffff/0 just like the original; no record is created when the count changes, only an existing one is updated.
//   * DoLotActivated's second item dereferences the freshly created line item before its null test
//     (0x1000ea22b); the port tests first.
//   * Init dereferences the department it just created without a null test (0x1000e902a); the port guards it.
//   * A list is walked backwards (node->prev) wherever the original searches it (0x1000ea285, 0x1000ea35a), and the
//     monthly ape pick counts from the end of the zoo list (0x1000ea353).
//   * The monthly pass seeds a local cRZRandom with 0xffffffff, i.e. from the clock, and compares the uniform double
//     against (double)0.01f (0x1000ea32b, constant 0x100a182c8).
//   * Read clears the occupant lists without releasing the references the list holds (the std::list clear at
//     0x1000cfb4d); the port releases them so the reference counts stay honest.
//
// The neighbours (budget simulator, building development simulator, lot manager, automata controller manager, the
// game-data registry and the message payloads) are reached through ICivicHost, one method per original call with its
// vtable slot. cISC4Occupant / cISC4BuildingOccupant and the budget classes are used directly (all recovered).
#pragma once

#include <cstdint>
#include <list>
#include <unordered_map>
#include <unordered_set>

#include "gz/CommandParameterSet.h"
#include "gz/cIGZMessage2.h"
#include "gz/cIGZSerializable.h"
#include "rz/cRZString.h"
#include "sc4/BuildingOccupant.h"
#include "sc4/BudgetSimulator.h"
#include "sc4/GameDataRegistry.h"
#include "sc4/Occupant.h"

namespace sc4 {

constexpr std::uint32_t kGZCLSID_cSC4CivicBuildingSimulator = 0x0a413481;
constexpr std::uint32_t kGZIID_cISC4CivicBuildingSimulator = 0x8a41322f;

// The three departments Init creates or looks up (0x1000e8fe3..0x1000e90f9). The first one is the one this
// simulator keeps a reference to (and serializes); the other two only get a name and a 100% funding cap.
constexpr std::uint32_t kCivicDepartment = 0x8a416368;          // and its budget group
constexpr std::uint32_t kCivicDepartmentGroup = 0x6a357b96;
constexpr std::uint32_t kCivicDepartmentName = 0xca491dd2;      // in name table 0x6a3ff01c
constexpr std::uint32_t kCivicDepartment2 = 0xea59717a;
constexpr std::uint32_t kCivicDepartment2Group = 0xea597195;
constexpr std::uint32_t kCivicDepartment2Name = 0x4a59726d;
constexpr std::uint32_t kCivicDepartment3 = 0x2a5a723f;
constexpr std::uint32_t kCivicDepartment3Group = 0x0a5a72d1;
constexpr std::uint32_t kCivicDepartment3Name = 0x8a5a7330;

// Budget item purposes: the landmark item (department funding drives the lot condition) and the second civic item
// whose cost is booked as line-item income.
constexpr std::uint32_t kCivicBudgetPurposeLandmark = 0xaa59670c;
constexpr std::uint32_t kCivicBudgetPurposeIncome = 0x4a5a495e;

// Exemplar properties of a civic building. Their exact game meaning is inferred from the place-lot query tool, which
// treats either of them as "this is a landmark/reward building" (cSC4ViewInputControlPlaceLot 0x1003bd1e3).
constexpr std::uint32_t kCivicPropertyConditionalBuilding = 0xea3209f8;   // bool: count it in the status map
constexpr std::uint32_t kCivicPropertyExclusionId = 0xea2e078b;           // uint: id IsBuildingExcluded answers for

constexpr std::uint32_t kCivicOccupantGroupZoo = 0x1917;                  // the group that can let the ape out
constexpr char kCivicAutomataGroupApeEscape[] = "ape_escape";
constexpr char kCivicSimulatorName[] = "cSC4CivicBuildingSimulator";

// Messages (0x1000e910d..0x1000e913a subscribe, 0x1000e9698..0x1000e96c5 unsubscribe).
constexpr std::uint32_t kCivicMessageLotStateChanged = 0xe8bda2e7;        // payload: the lot (payload slot 0x100)
constexpr std::uint32_t kCivicMessageOccupantInserted = 0x99ef1142;       // payload slot 0x50: the occupant
constexpr std::uint32_t kCivicMessageOccupantRemoved = 0x99ef1143;
constexpr std::uint32_t kCivicMessageMonth = 0x66956816;

constexpr std::uint32_t kCivicLotConditionNormal = 0, kCivicLotConditionWarning = 2;   // lot vslots 0x118 / 0x120
constexpr std::uint32_t kCivicSaveVersion = 6;
constexpr int kCivicAgentInterval = 6;                     // simulator 0x98(target, 6, name, 2)
constexpr std::uint32_t kCivicAgentFlags = 2;
constexpr std::uint32_t kCivicListCountLimit = 9999999u;   // 0x98967f guard of the list reader (0x1000cfb65)

// cISC4Lot (the parts the simulator reads): the building on the lot and its condition.
class ICivicLot {
public:
    virtual ~ICivicLot() = default;
    virtual cISC4BuildingOccupant* GetBuilding() = 0;              // lot vslot 0x180 (borrowed)
    virtual std::uint32_t GetCondition() const = 0;                // lot vslot 0x118
    virtual void SetCondition(std::uint32_t condition) = 0;        // lot vslot 0x120
};

// What the original reaches through the SL::sp* globals and the message payloads.
class ICivicHost {
public:
    virtual ~ICivicHost() = default;
    virtual cSC4BudgetSimulator* BudgetSimulator() = 0;                     // SL::spBudgetSim
    virtual cISC4GameDataRegistry* GameDataRegistry() = 0;                  // RZGetFramework 0x28 service 0xebe0e860
    // simulator vslot 0x98(target, interval, name, flags); false when there is no simulator object.
    virtual bool AddSimulatorAgent(cIGZMessageTarget2* target, int interval, const char* name,
                                   std::uint32_t flags) = 0;
    virtual void RemoveSimulatorAgent(cIGZMessageTarget2* target) = 0;      // city 0x190 -> simulator 0xa8(target)
    // building development simulator vslot 0x68(buildingType, cGZPersistResourceKey&)
    virtual bool GetBuildingKeyFromType(std::uint32_t buildingType, cGZPersistResourceKey& key) = 0;
    // message slot 0x100 (GetIGZUnknown) + QueryInterface(0xe818fced). Borrowed; null when the message has no lot.
    virtual ICivicLot* MessageLot(cIGZMessage2* message) = 0;
    virtual ICivicLot* LotForOccupant(cISC4Occupant* occupant) = 0;         // lot manager vslot 0x70 (borrowed)
    // automata controller manager vslot 0x48(group, occupant, null position); false when the manager is absent.
    virtual bool AddControllersFromOccupantGroup(const cIGZString& group, cISC4Occupant* occupant) = 0;
};
ICivicHost* CivicBuildingHost();
void SetCivicBuildingHost(ICivicHost* host);

// cISC4CivicBuildingSimulator: the interface of the primary COM view (9 slots, vtable 0x100a716d0).
class cISC4CivicBuildingSimulator : public cIGZUnknown {
public:
    // cISC4CivicBuildingSimulator::ConditionalBuildingStatus, 0x30 bytes. `state` is the 32-bit word the reward
    // advice treats as its advice state: low byte = state (bit 2 "fired", low two bits the trigger result), high
    // word = the number of live buildings of that type. The original serializes only the low byte and the high word.
    struct ConditionalBuildingStatus {
        std::uint32_t state = 0;      // +0x00 low byte state, +0x02 u16 building count
        std::uint32_t reserved = 0;   // +0x04 (not copied by UpdateConditionalBuildingStatus)
        cRZString text;               // +0x08 (cRZString: vptr +0x08, std::string +0x10, refcount +0x20)

        std::uint16_t BuildingCount() const { return static_cast<std::uint16_t>(state >> 16); }
        void AddToBuildingCount(int delta) {
            const auto count = static_cast<std::uint16_t>(BuildingCount() + delta);
            state = (state & 0x0000ffffu) | (static_cast<std::uint32_t>(count) << 16);
        }
    };

    virtual bool Init() = 0;                                                                    // [3]  0x18
    virtual bool Shutdown() = 0;                                                                // [4]  0x20
    virtual bool IsBuildingExcluded(std::uint32_t buildingId) = 0;                              // [5]  0x28
    virtual ConditionalBuildingStatus* GetConditionalBuildingStatus(std::uint32_t id) = 0;      // [6]  0x30
    virtual void UpdateConditionalBuildingStatus(std::uint32_t id, const ConditionalBuildingStatus* status) = 0;   // [7] 0x38
    virtual std::uint32_t GetLandmarkCount() const = 0;                                         // [8]  0x40
    virtual ~cISC4CivicBuildingSimulator() = default;                                           // [9],[10]
};

class cSC4CivicBuildingSimulator final : public cISC4CivicBuildingSimulator,
                                         public cIGZMessageTarget2,
                                         public cIGZSerializable {
public:
    cSC4CivicBuildingSimulator() = default;                              // @ 0x1000e8dec/dtor family
    ~cSC4CivicBuildingSimulator() override;                              // @ 0x1000e8dec (deleting 0x1000e8e9a)

    // cIGZUnknown (one shared count at +0x18)
    bool QueryInterface(std::uint32_t iid, void** out) override;         // @ 0x1000e8ec8 (thunks 0x1000e8f0c/f1a)
    std::uint32_t AddRef() override;                                     // @ 0x1000e8f28 (thunks 0x1000e8f36/f44)
    std::uint32_t Release() override;                                    // @ 0x1000e8f52 (thunks 0x1000e8f70/f90)

    // cISC4CivicBuildingSimulator
    bool Init() override;                                                // @ 0x1000e8fb0
    bool Shutdown() override;                                            // @ 0x1000e9564
    bool IsBuildingExcluded(std::uint32_t buildingId) override;          // @ 0x1000ea75c
    ConditionalBuildingStatus* GetConditionalBuildingStatus(std::uint32_t id) override;   // @ 0x1000ea6fc
    void UpdateConditionalBuildingStatus(std::uint32_t id, const ConditionalBuildingStatus* status) override;  // @ 0x1000ea728
    std::uint32_t GetLandmarkCount() const override;                     // @ 0x1000ea782

    // cIGZSerializable
    bool Write(cIGZOStream& stream) const override;                      // @ 0x1000e9712 (thunk 0x1000e97e8)
    bool Read(cIGZIStream& stream) override;                             // @ 0x1000e97f6 (thunk 0x1000e9c6c)
    std::uint32_t GetGZCLSID() const override;                           // @ 0x1000ea78e (thunk 0x1000ea79a)

    // cIGZMessageTarget2
    bool DoMessage(cIGZMessage2* message) override;                      // @ 0x1000e9c7a (thunk 0x1000ea6e6)

    // game data registry command "get_landmark_count" (registered by Init @ 0x1000e93ed)
    static bool sGetLandmarkCount(void* handle, cIGZCommandParameterSet* in, cIGZCommandParameterSet* out,
                                  cISC4GameDataRegistry* registry);      // @ 0x1000e94cc

    // helpers (private in the original, exposed for the port's tests and the host wiring)
    void DoLotActivated(ICivicLot* lot);                                 // @ 0x1000ea076
    void DoMonthlySimulation();                                          // @ 0x1000ea2f0
    void DoBuildingRemoved(cISC4BuildingOccupant* building);              // @ 0x1000ea536

    const std::list<cISC4Occupant*>& Zoos() const { return zoos_; }              // +0x78
    const std::list<cISC4Occupant*>& Landmarks() const { return landmarks_; }    // +0x90
    const std::unordered_set<std::uint32_t>& ExcludedBuildings() const { return excludedBuildings_; }   // +0x28
    const std::unordered_map<std::uint32_t, ConditionalBuildingStatus>& ConditionalStatuses() const { return statuses_; }
    cSC4DepartmentBudget* Department() const { return department_; }             // +0x20
    bool IsInitialised() const { return initialised_; }                          // +0x1c
    std::uint32_t RefCount() const { return refCount_; }                         // +0x18

private:
    cSC4BudgetSimulator* Budget() const;
    static void ReleaseOccupantList(std::list<cISC4Occupant*>& list);

    std::uint32_t refCount_ = 0;                                    // +0x18
    bool initialised_ = false;                                      // +0x1c
    cSC4DepartmentBudget* department_ = nullptr;                    // +0x20 (counted)
    std::unordered_set<std::uint32_t> excludedBuildings_;           // +0x28
    std::unordered_map<std::uint32_t, ConditionalBuildingStatus> statuses_;   // +0x50
    std::list<cISC4Occupant*> zoos_;                                // +0x78 (size +0x88)
    std::list<cISC4Occupant*> landmarks_;                           // +0x90 (size +0xa0)
};

}  // namespace sc4
