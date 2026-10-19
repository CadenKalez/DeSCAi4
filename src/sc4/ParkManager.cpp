#include "sc4/ParkManager.h"

#include <algorithm>

#include "rz/cRZMessage2Standard.h"
#include "sc4/DBSegment.h"
#include "sc4/SCPropertyHelp.h"
#include "sc4/ServiceLocator.h"

namespace sc4 {

namespace {
IParkEnvironment* g_environment = nullptr;
constexpr std::uint32_t kPropertyLeaveAlone = 0x029244db;        // occupants carrying it are ignored
constexpr std::uint32_t kPropertyFundingThreshold = 0xca480830;
constexpr std::uint32_t kPropertyEscapeCurve = 0x4a6676ad;
constexpr std::uint32_t kOccupantTypeBuildingOccupant = 0x278128a0;
}  // namespace

IParkEnvironment* ParkEnvironment() { return g_environment; }
void SetParkEnvironment(IParkEnvironment* environment) { g_environment = environment; }

cSC4ParkManager::cSC4ParkManager() = default;

cSC4ParkManager::~cSC4ParkManager() = default;

bool cSC4ParkManager::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZSerializable || iid == 0xe4fda3d4) {
        *out = static_cast<cIGZSerializable*>(this);
    } else if (iid == 0x452294aa) {
        *out = static_cast<cIGZMessageTarget2*>(this);
    } else if (iid == 1 || iid == kGZIID_cISC4ParkManager) {
        *out = static_cast<cISC4ParkManager*>(this);
    } else {
        return false;
    }
    AddRef();
    return true;
}

std::uint32_t cSC4ParkManager::Release() {
    if (refCount_ < 2) {
        Shutdown();
        delete this;
        return 0;
    }
    return --refCount_;
}

bool cSC4ParkManager::Init() {
    if (started_) return true;
    started_ = true;
    IParkEnvironment* env = ParkEnvironment();
    if (!tuningRead_) {
        ReadTuningParams();
        tuningRead_ = true;
    }
    if (!budget_) budget_ = env ? env->Budget() : nullptr;
    if (budget_) {
        department_ = budget_->GetDepartmentBudget(kDepartmentParks);
        if (!department_) {
            department_ = budget_->CreateDepartmentBudget(kDepartmentParks, kBudgetGroupParks);
            department_->SetDepartmentName(0x6a3ff01c, 0xca49375e);
            department_->SetMaxAllowedFundingPercentage(1.0f);        // kSC4PercentageOneHundred
        }
        department_->AddRef();
    }
    if (!messages_ && SL::spMS2) {
        messages_ = SL::spMS2;
        messages_->AddRef();
        messages_->AddNotification(this, kMessageOccupantInserted);
        messages_->AddNotification(this, kMessageOccupantRemovedFromManager);
    }
    if (env) env->AddMonthlyAgent(this);
    return true;
}

void cSC4ParkManager::ReleaseOccupantList(std::list<cISC4Occupant*>& list) {
    while (!list.empty()) {
        list.front()->Release();
        list.pop_front();
    }
}

bool cSC4ParkManager::Shutdown() {
    if (!started_) return true;
    started_ = false;
    ReleaseOccupantList(parks_);
    ReleaseOccupantList(zoos_);
    if (IParkEnvironment* env = ParkEnvironment()) env->RemoveAgent(this);
    if (messages_) {
        messages_->RemoveNotification(this, kMessageOccupantInserted);
        messages_->RemoveNotification(this, kMessageOccupantRemovedFromManager);
        messages_->Release();
        messages_ = nullptr;
    }
    if (department_) { department_->Release(); department_ = nullptr; }
    budget_ = nullptr;
    return true;
}

// The funding threshold and the zoo escape curve come from the tuning exemplar; without a curve property the curve
// is the straight line from 50 at 0% to 1 at 100%.
bool cSC4ParkManager::ReadTuningParams() {
    IParkEnvironment* env = ParkEnvironment();
    cISCPropertyHolder* exemplar = env ? env->TuningExemplar() : nullptr;
    if (!exemplar) return false;
    cSCPropertyHelp::GetPropertyValue(exemplar, kPropertyFundingThreshold, threshold_);
    std::vector<float> curve;
    if (exemplar->HasProperty(kPropertyEscapeCurve) && cSCPropertyHelp::GetPropertyValue(exemplar, kPropertyEscapeCurve, curve)) {
        escapeCurve_.SetResponseCurveData(curve.data(), curve.size());
    } else {
        escapeCurve_.SetResponseCurveData(std::vector<SC4Point>{{0.0f, 50.0f}, {100.0f, 1.0f}});
    }
    return true;
}

bool cSC4ParkManager::Write(cIGZOStream& stream) const {
    stream.SetUint16(2);
    for (const auto* list : {&parks_, &zoos_}) {
        stream.SetUint32(static_cast<std::uint32_t>(list->size()));
        for (cISC4Occupant* occupant : *list) SC4WriteGZSerializable(stream, occupant);
    }
    return !stream.GetError();
}

bool cSC4ParkManager::Read(cIGZIStream& stream) {
    std::uint16_t version = 0;
    stream.GetUint16(version);
    if (version != 2) return false;
    for (auto* list : {&parks_, &zoos_}) {
        ReleaseOccupantList(*list);
        std::uint32_t count = 0;
        stream.GetUint32(count);
        if (count < 10000000) {
            for (; count != 0; --count) {
                cISC4Occupant* occupant = nullptr;
                SC4ReadGZSerializable(stream, kGZIID_cISC4Occupant, reinterpret_cast<void**>(&occupant), false);
                list->push_back(occupant);
            }
        }
    }
    return !stream.GetError();
}

bool cSC4ParkManager::DoMessage(cIGZMessage2* message) {
    switch (message->GetType()) {
        case kMessageOccupantInserted:
        case kMessageOccupantRemovedFromManager: {
            auto* occupant = static_cast<cISC4Occupant*>(static_cast<cIGZMessage2Standard*>(message)->GetVoid1());
            if (!occupant->AsPropertyHolder()->HasProperty(kPropertyLeaveAlone)) {
                if (message->GetType() == kMessageOccupantInserted) DoOccupantInserted(occupant);
                else DoOccupantRemoved(occupant);
            }
            break;
        }
        case kMessageNewMonth:
            DoMonthlySimulation();
            break;
        default:
            break;
    }
    return true;
}

void cSC4ParkManager::DoMonthlySimulation() {
    if (parks_.empty()) return;
    float funding = department_ ? department_->GetFundingPercentage() : 0.0f;
    bool changed = false;
    std::uint32_t condition = kLotConditionNormal;
    if (threshold_ < funding || runDown_) {
        if (funding > threshold_ && runDown_) {
            runDown_ = false;
            condition = kLotConditionNormal;
            changed = true;
        }
    } else {
        runDown_ = true;
        condition = kLotConditionRunDown;
        changed = true;
    }
    if (changed) {
        if (IParkEnvironment* env = ParkEnvironment())
            for (cISC4Occupant* park : parks_) env->SetLotCondition(park, condition);
    }
    if (!zoos_.empty()) {
        const float chance = escapeCurve_.GetOutputValue(funding * 100.0f);
        if (random_.RandomDoubleUniform() < static_cast<double>(chance * 0.01f)) BeginZooEscape();
    }
}

void cSC4ParkManager::DoOccupantInserted(cISC4Occupant* occupant) {
    if (occupant->GetType() == kOccupantTypeBuildingOccupant && occupant->IsOccupantGroup(kOccupantGroupPark)) {
        occupant->AddRef();
        parks_.push_back(occupant);
        BudgetItem item;
        if (budget_ && budget_->GetBudgetItemForPurpose(occupant->AsPropertyHolder(), kBudgetPurposeParks, item)) {
            cISC4BuildingOccupant* building = nullptr;
            if (occupant->QueryInterface(kGZIID_cISC4BuildingOccupant, reinterpret_cast<void**>(&building))) {
                cSC4DepartmentBudget* department = budget_->GetDepartmentBudget(static_cast<std::uint32_t>(item.departmentId));
                if (item.lineItemId == 0) {
                    item.lineItemId = static_cast<std::int32_t>(building->GetBuildingType());
                    budget_->ChangeBudgetItemLine(occupant->AsPropertyHolder(), static_cast<int>(item.purpose), static_cast<std::uint32_t>(item.lineItemId));
                }
                if (item.lineItemId != 0 && department) {
                    cSC4LineItem* line = department->GetLineItem(static_cast<std::uint32_t>(item.lineItemId));
                    if (!line) {
                        line = department->CreateLineItemForBuildingType(static_cast<std::uint32_t>(item.lineItemId), false);
                        line->SetDisplayFlag(2, true);
                    }
                    line->AddToFullExpenses(item.cost);
                    line->SetSecondaryInfoField(line->GetSecondaryInfoField() + 1);
                }
                building->Release();
            }
        }
    }
    if (occupant->GetType() == kOccupantTypeBuildingOccupant && occupant->IsOccupantGroup(kOccupantGroupZoo)) {
        occupant->AddRef();
        zoos_.push_back(occupant);
    }
}

void cSC4ParkManager::DoOccupantRemoved(cISC4Occupant* occupant) {
    if (occupant->GetType() == kOccupantTypeBuildingOccupant && occupant->IsOccupantGroup(kOccupantGroupPark)) {
        parks_.remove(occupant);
        occupant->Release();
        BudgetItem item;
        if (budget_ && budget_->GetBudgetItemForPurpose(occupant->AsPropertyHolder(), kBudgetPurposeParks, item)) {
            if (cSC4LineItem* line = budget_->GetLineItemFromBudgetItem(item)) {
                line->AddToFullExpenses(-item.cost);
                line->SetSecondaryInfoField(line->GetSecondaryInfoField() - 1);
            }
        }
    }
    if (occupant->GetType() == kOccupantTypeBuildingOccupant && occupant->IsOccupantGroup(kOccupantGroupZoo)) {
        zoos_.remove(occupant);
        occupant->Release();
    }
}

// Picks a random zoo, tells the message server about it and starts the "zoomanji" automata on it.
void cSC4ParkManager::BeginZooEscape() {
    if (zoos_.empty()) return;
    std::uint32_t index = random_.RandomUint32Uniform(static_cast<std::uint32_t>(zoos_.size()));
    auto it = zoos_.begin();
    std::advance(it, index);
    cISC4Occupant* zoo = *it;
    if (!zoo) return;
    auto* message = new cRZMessage2Standard;
    message->AddRef();
    message->SetType(kMessageZooEscape);
    message->SetVoid1(this);
    message->SetIGZUnknown(zoo);
    if (messages_) messages_->MessagePost(message, false);
    message->Release();
    if (IParkEnvironment* env = ParkEnvironment()) env->StartZooEscape(zoo);
}

}  // namespace sc4
