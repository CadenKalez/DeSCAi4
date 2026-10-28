// cSC4ParkManager — tracks the city's parks and zoos (20 + thunks, 0x1000c8f7e..0x1000c9f56). Class id / interface
// 0xaa3fe6bc, serializable view 0xe4fda3d4, message target 0x452294aa. Building occupants of the park group (0x1006) are
// registered with the park budget department (0x8a4260a6, group 0x6a357b96): each park adds its cost and a count to
// the line item of its building type. Every month the department's funding decides whether the parks are shown in poor
// condition (funding <= the tuning threshold, default 25%) and zoos may let their animals out (chance read from a
// response curve of the funding percentage: 50% at 0 funding down to 1% at 100%).
// Record version 2: the park list then the zoo list (u32 count, then each occupant).
// Layout: +0x18 started, +0x20 tuning read, +0x21 parks run down, +0x28 parks (count +0x38), +0x40 zoos (count +0x50),
// +0x80 funding threshold, +0x88 escape curve, +0xa8 random generator.
#pragma once
#include <cstdint>
#include <list>

#include "gz/cIGZMessage2.h"
#include "gz/cIGZSerializable.h"
#include "rz/cRZRandomSuper.h"
#include "sc4/BudgetSimulator.h"
#include "sc4/BuildingOccupant.h"
#include "sc4/Occupant.h"
#include "sc4/cSC4ResponseCurve.h"

namespace sc4 {

constexpr std::uint32_t kGZCLSID_cSC4ParkManager = 0xaa3fe6bc;
constexpr std::uint32_t kGZIID_cISC4ParkManager = 0xaa3fe6bc;
constexpr std::uint32_t kDepartmentParks = 0x8a4260a6;
constexpr std::uint32_t kBudgetGroupParks = 0x6a357b96;
constexpr std::uint32_t kBudgetPurposeParks = 0xca639989;
constexpr std::uint32_t kOccupantGroupPark = 0x1006;
constexpr std::uint32_t kOccupantGroupZoo = 0x1702;
constexpr std::uint32_t kMessageNewMonth = 0x66956816;
constexpr std::uint32_t kMessageZooEscape = 0xaa667ce2;               // void 1 = the park manager, unknown = the zoo
constexpr std::uint32_t kLotConditionNormal = 0, kLotConditionRunDown = 2;

class IParkEnvironment {
public:
    virtual ~IParkEnvironment() = default;
    virtual cSC4BudgetSimulator* Budget() = 0;                            // SL::spBudgetSim
    virtual cISCPropertyHolder* TuningExemplar() = 0;                     // exemplar 0x6534284a / 0x67cd5fa1 / 0x8a4805bc
    virtual void SetLotCondition(cISC4Occupant* occupant, std::uint32_t condition) = 0;   // lot under the occupant
    virtual void StartZooEscape(cISC4Occupant* zoo) = 0;                  // automata controllers of occupant group "zoomanji"
    // city position -> cell, then the lot manager's lot at that cell (spCity vslot 0x2f8, spLotManager vslot 0x28); the lot is opaque here
    virtual void* LotWhereOccupantResides(cISC4Occupant*) { return nullptr; }
    virtual void AddMonthlyAgent(cIGZMessageTarget2* target) = 0;         // simulator AddAgent(target, 6, "cSC4ParkManager", 2)
    virtual void RemoveAgent(cIGZMessageTarget2* target) = 0;
};
IParkEnvironment* ParkEnvironment();
void SetParkEnvironment(IParkEnvironment* environment);

class cISC4ParkManager : public cIGZUnknown {};

class cSC4ParkManager : public cISC4ParkManager, public cIGZMessageTarget2, public cIGZSerializable {
public:
    cSC4ParkManager();
    virtual ~cSC4ParkManager();

    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return ++refCount_; }
    std::uint32_t Release() override;                   // deletes (after Shutdown) when the count is below 2

    bool Init();
    bool Shutdown();
    bool Write(cIGZOStream& stream) const override;
    bool Read(cIGZIStream& stream) override;
    std::uint32_t GetGZCLSID() const override { return kGZCLSID_cSC4ParkManager; }
    bool DoMessage(cIGZMessage2* message) override;

    bool ReadTuningParams();
    void DoMonthlySimulation();
    void DoOccupantInserted(cISC4Occupant* occupant);
    void DoOccupantRemoved(cISC4Occupant* occupant);
    void BeginZooEscape();
    void* GetLotWhereOccupantResides(cISC4Occupant* occupant) {             // @ 0x1000c9c92
        IParkEnvironment* env = ParkEnvironment();
        return env ? env->LotWhereOccupantResides(occupant) : nullptr;
    }

    const std::list<cISC4Occupant*>& Parks() const { return parks_; }
    const std::list<cISC4Occupant*>& Zoos() const { return zoos_; }
    bool ParksRunDown() const { return runDown_; }
    float FundingThreshold() const { return threshold_; }
    cSC4ResponseCurve& EscapeCurve() { return escapeCurve_; }
    cRZRandomSuper& Random() { return random_; }

private:
    static void ReleaseOccupantList(std::list<cISC4Occupant*>& list);

    bool started_ = false;                  // +0x18
    std::uint32_t refCount_ = 0;            // +0x1c
    bool tuningRead_ = false;               // +0x20
    bool runDown_ = false;                  // +0x21
    std::list<cISC4Occupant*> parks_;       // +0x28
    std::list<cISC4Occupant*> zoos_;        // +0x40
    cSC4BudgetSimulator* budget_ = nullptr; // +0x60
    cIGZMessageServer2* messages_ = nullptr;   // +0x68
    cSC4DepartmentBudget* department_ = nullptr;   // +0x70
    float threshold_ = 0.25f;               // +0x80
    cSC4ResponseCurve escapeCurve_;         // +0x88
    cRZRandomSuper random_{0xffffffffu};    // +0xa8
};

}  // namespace sc4
