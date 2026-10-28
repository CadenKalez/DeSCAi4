#include "sc4/Budget.h"

#include <algorithm>
#include <cmath>

#include "gz/StreamOps.h"
#include "sc4/DBSegment.h"

namespace sc4 {

namespace {
IBudgetHost* g_host = nullptr;
std::int64_t NonNegative(std::int64_t v) { return v < 0 ? 0 : v; }
// cvtss2si after "+ 0.5" in the original: floor(x + 0.5) of a float expression.
std::int32_t RoundHalfUp(float x) { return static_cast<std::int32_t>(std::floor(x + 0.5f)); }
}  // namespace

IBudgetHost* BudgetHost() { return g_host; }
void SetBudgetHost(IBudgetHost* host) { g_host = host; }

// ------------------------------------------------------------------------------------------------ line item

cSC4LineItem::cSC4LineItem(cSC4DepartmentBudget* department, std::uint32_t id, bool isLocalFunding)
    : id_(id), isLocalFunding_(isLocalFunding), department_(department) {
    if (department_) department_->AddRef();
}

cSC4LineItem::~cSC4LineItem() {
    if (department_) department_->Release();
}

bool cSC4LineItem::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == 1 || iid == kGZCLSID_cSC4LineItem) {
        *out = static_cast<cISC4LineItem*>(this);
    } else if (iid == 0xe4fda3d4) {
        *out = static_cast<cIGZSerializable*>(this);
    } else {
        return false;
    }
    AddRef();
    return true;
}

std::uint32_t cSC4LineItem::Release() {
    if (refCount_ > 1) return --refCount_;
    delete this;
    return 0;
}

void cSC4LineItem::DropOwnerReference() {
    if (!ownerReferenceHeld) return;
    ownerReferenceHeld = false;
    cSC4DepartmentBudget* department = department_;
    department_ = nullptr;
    if (department) department->Release();
}

void cSC4LineItem::SetFullExpenses(std::int64_t value) {
    fullExpenses_ = value;
    if (!isLocalFunding_)
        currentExpenses_ = static_cast<std::int64_t>(department_->GetFundingPercentage() * static_cast<float>(value));
    department_->SendBudgetChangeMessage(kBudgetMessageNeededFundingChanged, 0);
}

void cSC4LineItem::SetIncome(std::int64_t value) {
    income_ = value < 0 ? 0 : value;
    department_->SendBudgetChangeMessage(kBudgetMessageNeededFundingChanged, 0);
}

void cSC4LineItem::AddToIncome(std::int64_t value) {
    income_ += value;
    department_->SendBudgetChangeMessage(kBudgetMessageNeededFundingChanged, 0);
}

// group 0: the instance is a building type and the name is that building's; otherwise a localized string.
void cSC4LineItem::SetName(std::uint32_t group, std::uint32_t instance) {
    nameGroup_ = group;
    nameInstance_ = instance;
    if (!g_host) return;
    if (group == 0) name_ = cRZString(g_host->BuildingName(instance).c_str());
    else name_ = cRZString(g_host->LoadLocalizedString(group, instance).c_str());
}

void cSC4LineItem::GetName(cRZString& out) {
    if (nameNeedsReload_) {
        SetName(nameGroup_, nameInstance_);
        nameNeedsReload_ = false;
    }
    out = name_;
}

bool cSC4LineItem::Write(cIGZOStream& stream) const {
    stream.SetUint16(4);
    stream.SetGZStr(name_);
    stream.SetSint64(secondaryInfo_);
    stream.SetSint64(fullExpenses_);
    stream.SetSint64(currentExpenses_);
    stream.SetSint64(income_);
    stream.SetUint32(id_);
    stream.SetUint32(displayFlags_);
    stream << isLocalFunding_;
    stream.SetUint16(static_cast<std::uint16_t>(type_));
    SC4WriteGZSerializable(stream, static_cast<cIGZSerializable*>(department_));
    stream.SetUint32(nameInstance_);
    stream.SetUint32(nameGroup_);
    return !stream.GetError();
}

bool cSC4LineItem::Read(cIGZIStream& stream) {
    std::uint16_t version = 0;
    stream.GetUint16(version);
    if (version != 4) return false;
    stream.GetGZStr(name_);
    stream.GetSint64(secondaryInfo_);
    stream.GetSint64(fullExpenses_);
    stream.GetSint64(currentExpenses_);
    stream.GetSint64(income_);
    stream.GetUint32(id_);
    stream.GetUint32(displayFlags_);
    stream >> isLocalFunding_;
    std::uint16_t type = 0;
    stream.GetUint16(type);
    type_ = type;
    if (department_) {
        cSC4DepartmentBudget* old = department_;
        department_ = nullptr;
        old->Release();
    }
    void* department = nullptr;
    SC4ReadGZSerializable(stream, kGZCLSID_cSC4DepartmentBudget, &department, false);
    // QueryInterface(classId) returns the cISC4DepartmentBudget sub-object (offset +8); a void-chain cast would keep
    // that +8 address and read the wrong vtable, so downcast through the interface type, which adjusts the offset.
    department_ = static_cast<cSC4DepartmentBudget*>(static_cast<cISC4DepartmentBudget*>(department));
    stream.GetUint32(nameInstance_);
    stream.GetUint32(nameGroup_);
    nameNeedsReload_ = true;
    return !stream.GetError();
}

// ------------------------------------------------------------------------------------- department budget

cSC4DepartmentBudget::~cSC4DepartmentBudget() = default;

bool cSC4DepartmentBudget::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == 1 || iid == kGZIID_cISC4DepartmentBudget || iid == kGZCLSID_cSC4DepartmentBudget) {
        *out = static_cast<cISC4DepartmentBudget*>(this);
    } else if (iid == 0xe4fda3d4) {
        *out = static_cast<cIGZSerializable*>(this);
    } else {
        return false;
    }
    AddRef();
    return true;
}

std::uint32_t cSC4DepartmentBudget::Release() {
    if (refCount_ > 1) return --refCount_;
    delete this;
    return 0;
}

bool cSC4DepartmentBudget::Init() {
    if (!initialised_) initialised_ = true;
    return true;
}

bool cSC4DepartmentBudget::Shutdown() {
    if (initialised_) {
        initialised_ = false;
        for (auto& entry : lineItems_) {
            entry.second->DropOwnerReference();
            entry.second->Release();
        }
        lineItems_.clear();
        for (auto& funded : locallyFunded_) if (funded.first) funded.first->Release();
        locallyFunded_.clear();
    }
    return true;
}

void cSC4DepartmentBudget::SetDepartmentName(std::uint32_t group, std::uint32_t instance) {
    nameGroup_ = group;
    nameInstance_ = instance;
    if (g_host) name_ = cRZString(g_host->LoadLocalizedString(group, instance).c_str());
}

void cSC4DepartmentBudget::SendBudgetChangeMessage(std::uint32_t type, std::uint32_t extra) {
    if (!g_host) return;
    if (type == kBudgetMessageNeededFundingChanged) g_host->NeededFundingChanged(this);
    else if (type == kBudgetMessageFundingPercentageChanged) g_host->FundingPercentageChanged(this);
    g_host->PostBudgetMessage(type, this, departmentId_, extra);
}

cSC4LineItem* cSC4DepartmentBudget::CreateLineItem(std::uint32_t id, bool isLocalFunding) {
    auto* item = new cSC4LineItem(this, id, isLocalFunding);
    lineItems_[id] = item;                          // an existing entry is overwritten without release
    return item;
}

cSC4LineItem* cSC4DepartmentBudget::CreateLineItemForBuildingType(std::uint32_t buildingType, bool isLocalFunding) {
    cSC4LineItem* item = CreateLineItem(buildingType, isLocalFunding);
    item->SetName(0, buildingType);
    return item;
}

void cSC4DepartmentBudget::RemoveLineItem(std::uint32_t id) {
    auto it = lineItems_.find(id);
    if (it == lineItems_.end()) return;
    cSC4LineItem* item = it->second;
    if (item) {
        item->DropOwnerReference();
        item->Release();
    }
    lineItems_.erase(it);
}

cSC4LineItem* cSC4DepartmentBudget::GetLineItem(std::uint32_t id) {
    auto it = lineItems_.find(id);
    return it == lineItems_.end() ? nullptr : it->second;
}

void cSC4DepartmentBudget::GetAllLineItems(std::vector<cSC4LineItem*>& out) {
    out.clear();
    out.reserve(lineItems_.size());
    for (auto& entry : lineItems_) {
        entry.second->AddRef();
        out.push_back(entry.second);
    }
}

std::int64_t cSC4DepartmentBudget::GetIdealMonthlyFunding() const {
    std::int64_t total = 0;
    for (auto& entry : lineItems_) total += entry.second->GetFullExpenses();
    return total;
}

std::int64_t cSC4DepartmentBudget::GetTotalExpenses() const {
    std::int64_t total = 0;
    for (auto& entry : lineItems_) total += entry.second->GetCurrentExpenses();
    return total;
}

std::int64_t cSC4DepartmentBudget::GetTotalIncome() const {
    std::int64_t total = 0;
    for (auto& entry : lineItems_) total += entry.second->GetIncome();
    return total;
}

void cSC4DepartmentBudget::SetFundingPercentage(float percentage, std::uint32_t messageData, bool recalculate) {
    float clamped = percentage >= 0.0f ? percentage : 0.0f;
    if (clamped > maxAllowedFundingPercentage_) clamped = maxAllowedFundingPercentage_;
    if (fixedFunding_) return;
    if (!(fundingPercentage_ != clamped) && !std::isnan(fundingPercentage_) && !std::isnan(clamped)) return;
    fundingPercentage_ = clamped;
    for (auto& entry : lineItems_) {
        cSC4LineItem* item = entry.second;
        if (!item->IsLocalFundingItem() && item->GetType() != 1) {
            std::int64_t current = static_cast<std::int64_t>(static_cast<float>(item->GetFullExpenses()) * clamped + 0.5f);
            item->SetCurrentExpenses(current < 0 ? 0 : current);
        }
    }
    if (recalculate) DoDepartmentFundingChanged();
    SendBudgetChangeMessage(kBudgetMessageFundingPercentageChanged, messageData);
}

// Spreads the department's funding over its locally funded objects: every object keeps its share
// (cost * local funding) and the difference to the department total is handed out proportionally to the cost,
// rounding away from zero, until each object hits its limit (0 or cost * max funding) or the difference is gone.
void cSC4DepartmentBudget::DoDepartmentFundingChanged() {
    if (locallyFunded_.empty() || !g_host) return;
    const float funding = fundingPercentage_;
    const float maximum = maxAllowedFundingPercentage_;
    std::vector<ObjectFunding> objects;
    std::int32_t sumCost = 0, sumAmount = 0;
    for (const auto& funded : locallyFunded_) {
        BudgetItem item;
        if (!g_host->GetBudgetItemForPurpose(funded.first, funded.second, item)) continue;
        ObjectFunding of;
        of.object = funded.first;
        of.item = item;
        of.cost = static_cast<std::int32_t>(item.cost);
        of.localFunding = item.localFunding;
        of.amount = RoundHalfUp(item.localFunding * static_cast<float>(of.cost));
        of.maximum = RoundHalfUp(static_cast<float>(of.cost) * maximum);
        sumAmount += of.amount;
        sumCost += of.cost;
        objects.push_back(of);
    }
    std::int32_t remaining = RoundHalfUp(funding * static_cast<float>(sumCost)) - sumAmount;
    std::int32_t open = static_cast<std::int32_t>(objects.size());
    if (remaining != 0 && open != 0) {
        float ratio = static_cast<float>(remaining) / static_cast<float>(sumCost);
        do {
            for (ObjectFunding& of : objects) {
                if (of.done) continue;
                const float share = static_cast<float>(of.cost) * ratio;
                std::int32_t give;
                if (remaining < 0) {
                    give = static_cast<std::int32_t>(std::floor(share));
                    if (give < remaining) give = remaining;
                    if (of.amount + give < 0) {
                        give = -of.amount;
                        of.done = true;
                        --open;
                        sumCost -= of.cost;
                    }
                } else {
                    give = static_cast<std::int32_t>(std::ceil(share));
                    if (remaining < give) give = remaining;
                    if (of.maximum < of.amount + give) {
                        give = of.maximum - of.amount;
                        of.done = true;
                        --open;
                        sumCost -= of.cost;
                    }
                }
                of.amount += give;
                remaining -= give;
                if (remaining == 0) goto distributed;
            }
            if (remaining == 0 || open == 0) break;
            ratio = static_cast<float>(remaining) / static_cast<float>(sumCost);
        } while (true);
    }
distributed:
    for (auto& entry : lineItems_)
        if (entry.second->IsLocalFundingItem()) entry.second->SetCurrentExpenses(0);
    for (const ObjectFunding& of : objects) {
        g_host->ChangeBudgetItemLocalFunding(of.object, of.item.purpose,
                                             static_cast<float>(of.amount) / static_cast<float>(of.cost));
        cSC4LineItem* line = GetLineItem(static_cast<std::uint32_t>(of.item.lineItemId));   // (original: map[] -> null deref)
        if (line) line->SetCurrentExpenses(NonNegative(line->GetCurrentExpenses() + of.amount));
    }
}

void cSC4DepartmentBudget::AddLocallyFundedObject(cISCPropertyHolder* object, std::uint32_t purpose) {
    BudgetItem item;
    if (g_host) g_host->GetBudgetItemForPurpose(object, purpose, item);
    object->AddRef();
    locallyFunded_.insert({object, purpose});
    cSC4LineItem* line = GetLineItem(static_cast<std::uint32_t>(item.lineItemId));
    if (line) {
        line->AddToFullExpenses(item.cost);
        const std::int64_t current = line->GetCurrentExpenses() +
                                     static_cast<std::int64_t>(fundingPercentage_ * static_cast<float>(item.cost));
        line->SetCurrentExpenses(NonNegative(current));
    }
    if (g_host) {
        g_host->ChangeBudgetItemLocalFunding(object, item.purpose, fundingPercentage_);
        g_host->FundingPercentageChanged(this);
    }
}

void cSC4DepartmentBudget::RemoveLocallyFundedObject(cISCPropertyHolder* object, std::uint32_t purpose) {
    auto it = locallyFunded_.find({object, purpose});
    if (it == locallyFunded_.end()) return;
    SetLocalFullFunding(object, 0, purpose);
    object->Release();
    locallyFunded_.erase(it);
    if (locallyFunded_.empty() && GetIdealMonthlyFunding() == 0) SetFundingPercentage(1.0f, 0, true);
}

bool cSC4DepartmentBudget::IsLocallyFundedObjectInDepartment(cISCPropertyHolder* object, std::uint32_t purpose) const {
    return locallyFunded_.count({object, purpose}) != 0;
}

void cSC4DepartmentBudget::RecalculateLineItemCurrentExpenses(std::uint32_t lineItemId) {
    cSC4LineItem* line = GetLineItem(lineItemId);
    if (!line || !line->IsLocalFundingItem() || line->GetType() != 0 || !g_host) return;
    std::int64_t sum = 0;
    for (const auto& funded : locallyFunded_) {
        std::vector<BudgetItem> items;
        g_host->GetBudgetItemInfo(funded.first, items);
        for (const BudgetItem& item : items) {
            if (static_cast<std::uint32_t>(item.lineItemId) == lineItemId &&
                static_cast<std::uint32_t>(item.departmentId) == departmentId_)
                sum = static_cast<std::int64_t>(static_cast<float>(sum) +
                                                item.localFunding * 100.0f * static_cast<float>(item.cost));
        }
    }
    line->SetCurrentExpenses(NonNegative(sum / 100));
}

void cSC4DepartmentBudget::RecalculateAllLineItemCurrentExpenses() {
    std::map<std::uint32_t, std::int64_t> totals;
    std::uint32_t counted = 0;
    for (auto& entry : lineItems_) {
        cSC4LineItem* item = entry.second;
        if (!item->IsLocalFundingItem()) ++counted;
        else if (item->GetType() == 0) totals[entry.first] = 0;
    }
    if (g_host) {
        for (const auto& funded : locallyFunded_) {
            std::vector<BudgetItem> items;
            g_host->GetBudgetItemInfo(funded.first, items);
            for (const BudgetItem& item : items) {
                if (static_cast<std::uint32_t>(item.departmentId) != departmentId_) continue;
                auto it = totals.find(static_cast<std::uint32_t>(item.lineItemId));
                if (it == totals.end()) continue;
                // the Mac build forms localFunding * 100 first (0x100245113); the Windows build multiplies
                // cost * localFunding first (OpenSC4 budget.md 7.2), which can round to another float
                it->second = static_cast<std::int64_t>(static_cast<float>(it->second) +
                                                       static_cast<float>(item.cost) * (item.localFunding * 100.0f));
                ++counted;
            }
        }
    }
    for (auto& total : totals)
        if (cSC4LineItem* line = GetLineItem(total.first)) line->SetCurrentExpenses(NonNegative(total.second / 100));
    if (lineItems_.empty()) return;
    std::int64_t currentSum = 0, idealSum = 0;
    for (auto& entry : lineItems_) {
        currentSum += entry.second->GetCurrentExpenses();
        idealSum += entry.second->GetFullExpenses();
    }
    if (idealSum > 0) {
        float percent = (static_cast<float>(currentSum) * 100.0f) / static_cast<float>(idealSum);
        if (std::fabs(percent - fundingPercentage_ * 100.0f) > 0.5f &&
            static_cast<float>(counted) + 2.0f <
                std::fabs(static_cast<float>(currentSum) +
                          ((fundingPercentage_ * 100.0f) / -100.0f) * static_cast<float>(idealSum))) {
            float clamped = percent >= 0.0f ? percent : 0.0f;
            const float limit = maxAllowedFundingPercentage_ * 100.0f;
            if (clamped > limit) clamped = limit;
            SetFundingPercentage(clamped * 0.01f, 0, true);
        }
    }
}

void cSC4DepartmentBudget::SetLocalFundingPercent(cISCPropertyHolder* object, float percent, std::uint32_t purpose) {
    if (!(percent >= 0.0f && percent <= maxAllowedFundingPercentage_)) return;
    if (!locallyFunded_.count({object, purpose}) || !g_host) return;
    BudgetItem item;
    if (!g_host->GetBudgetItemForPurpose(object, purpose, item)) return;
    const std::int32_t newAmount = static_cast<std::int32_t>(std::floor(percent * 100.0f * static_cast<float>(item.cost) + 0.5f));
    const std::int32_t oldAmount = static_cast<std::int32_t>(std::floor(item.localFunding * 100.0f * static_cast<float>(item.cost) + 0.5f));
    const std::int64_t delta = static_cast<std::int64_t>(newAmount) - oldAmount;
    float resulting = fundingPercentage_;
    if (delta != 0) {
        const std::int64_t ideal = GetIdealMonthlyFunding();
        if (ideal == 0) {
            resulting = percent;
        } else {
            const std::int32_t current = static_cast<std::int32_t>(
                std::floor(fundingPercentage_ * 100.0f * static_cast<float>(ideal) + 0.5f));
            resulting = (static_cast<float>(delta + current) / static_cast<float>(ideal)) * 0.01f;
        }
        g_host->NotifyLocalFundingChanged(object);
    }
    g_host->ChangeBudgetItemLocalFunding(object, item.purpose, percent);
    RecalculateLineItemCurrentExpenses(static_cast<std::uint32_t>(item.lineItemId));
    if (resulting == fundingPercentage_) return;
    SetFundingPercentage(resulting, 0, false);
}

float cSC4DepartmentBudget::GetLocalFundingPercent(cISCPropertyHolder* object, std::uint32_t purpose) const {
    if (!locallyFunded_.count({object, purpose}) || !g_host) return 0.0f;
    BudgetItem item;
    return g_host->GetBudgetItemForPurpose(object, purpose, item) ? item.localFunding : 0.0f;
}

void cSC4DepartmentBudget::SetLocalFullFunding(cISCPropertyHolder* object, std::int64_t cost, std::uint32_t purpose) {
    if (!locallyFunded_.count({object, purpose}) || !g_host) return;
    BudgetItem item;
    if (!g_host->GetBudgetItemForPurpose(object, purpose, item)) return;
    const std::int64_t delta = cost - item.cost;
    g_host->ChangeBudgetItemCost(object, item.purpose, cost);
    const std::int32_t amountDelta = static_cast<std::int32_t>(
        std::floor(static_cast<float>(delta) * (item.localFunding * 100.0f) + 0.5f));   // percent first, as the original
    const std::int64_t ideal = GetIdealMonthlyFunding();
    float percentage;
    if (ideal + delta == 0) {
        percentage = 1.0f;
    } else {
        const std::int32_t current = static_cast<std::int32_t>(
            std::floor(static_cast<float>(ideal) * (fundingPercentage_ * 100.0f) + 0.5f));
        percentage = (static_cast<float>(static_cast<std::int64_t>(current) + amountDelta) /
                      static_cast<float>(ideal + delta)) * 0.01f;
    }
    // the original reads lineItems_[id] (operator[]) and calls through it unchecked; a missing line would crash there
    cSC4LineItem* line = GetLineItem(static_cast<std::uint32_t>(item.lineItemId));
    if (line) {
        line->AddToFullExpenses(delta);
        line->SetCurrentExpenses(NonNegative(static_cast<std::int64_t>(amountDelta) / 100 + line->GetCurrentExpenses()));
    }
    SendBudgetChangeMessage(kBudgetMessageNeededFundingChanged, 0);
    SetFundingPercentage(percentage, 0, false);
}

std::int64_t cSC4DepartmentBudget::GetLocalFullFunding(cISCPropertyHolder* object, std::uint32_t purpose) const {
    if (!locallyFunded_.count({object, purpose}) || !g_host) return 0;
    BudgetItem item;
    return g_host->GetBudgetItemForPurpose(object, purpose, item) ? item.cost : 0;
}

void cSC4DepartmentBudget::GetLocallyFundedItemsByPurpose(std::uint32_t purpose, std::vector<cISCPropertyHolder*>& out) {
    for (const auto& funded : locallyFunded_) {
        if (funded.second != purpose) continue;
        funded.first->AddRef();
        out.push_back(funded.first);
    }
}

bool cSC4DepartmentBudget::HasLocallyFundedItemsByPurpose(std::uint32_t purpose) const {
    for (const auto& funded : locallyFunded_)
        if (funded.second == purpose) return true;
    return false;
}

bool cSC4DepartmentBudget::Write(cIGZOStream& stream) const {
    stream.SetUint16(10);
    stream << initialised_;
    stream.SetUint32(departmentId_);
    stream.SetGZStr(name_);
    stream.SetUint32(budgetGroup_);
    stream.SetFloat32(fundingPercentage_);
    stream.SetFloat32(maxAllowedFundingPercentage_);
    stream.SetSint64(totalSpending_);
    stream << fixedFunding_;
    stream.SetUint32(static_cast<std::uint32_t>(lineItems_.size()));
    for (const auto& entry : lineItems_) SC4WriteGZSerializable(stream, static_cast<cIGZSerializable*>(entry.second));
    stream.SetUint32(static_cast<std::uint32_t>(locallyFunded_.size()));
    for (const auto& funded : locallyFunded_) {
        SC4WriteGZSerializable(stream, funded.first);
        stream.SetUint32(funded.second);
    }
    stream.SetUint32(nameInstance_);
    stream.SetUint32(nameGroup_);
    return !stream.GetError();
}

bool cSC4DepartmentBudget::Read(cIGZIStream& stream) {
    std::uint16_t version = 0;
    stream.GetUint16(version);
    if (version >= 0xb) return false;
    stream >> initialised_;
    stream.GetUint32(departmentId_);
    stream.GetGZStr(name_);
    stream.GetUint32(budgetGroup_);
    float value = 0.0f;
    stream.GetFloat32(value);
    fundingPercentage_ = version < 10 ? value * 0.01f : value;      // older saves stored whole percents
    stream.GetFloat32(value);
    maxAllowedFundingPercentage_ = version < 10 ? value * 0.01f : value;
    stream.GetSint64(totalSpending_);
    stream >> fixedFunding_;
    std::uint32_t count = 0;
    stream.GetUint32(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        void* item = nullptr;
        SC4ReadGZSerializable(stream, kGZCLSID_cSC4LineItem, &item, false);
        // QueryInterface(classId) returns the cISC4LineItem sub-object (offset +8); downcast through the interface type
        // so the compiler adjusts +8 back to the cSC4LineItem at +0. The old void-chain cast kept the +8 address.
        auto* lineItem = static_cast<cSC4LineItem*>(static_cast<cISC4LineItem*>(item));
        if (lineItem) lineItems_[lineItem->GetID()] = lineItem;
    }
    stream.GetUint32(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        void* holder = nullptr;
        SC4ReadGZSerializable(stream, kGZIID_cISCPropertyHolder, &holder, false);
        std::uint32_t purpose = 0;
        stream.GetUint32(purpose);
        // a holder whose class the host cannot create reads back null: keep the stream in step but do not track it
        if (holder) locallyFunded_.insert({static_cast<cISCPropertyHolder*>(holder), purpose});
    }
    stream.GetUint32(nameInstance_);
    stream.GetUint32(nameGroup_);
    if (nameInstance_ != 0 && nameGroup_ != 0 && g_host)
        name_ = cRZString(g_host->LoadLocalizedString(nameGroup_, nameInstance_).c_str());
    return !stream.GetError();
}

}  // namespace sc4
