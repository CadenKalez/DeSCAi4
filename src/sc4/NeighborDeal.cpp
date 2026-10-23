#include "sc4/NeighborDeal.h"

#include "sc4/DBSegment.h"

namespace sc4 {

static INeighborDealHost* g_host = nullptr;
INeighborDealHost* NeighborDealHost() { return g_host; }
void SetNeighborDealHost(INeighborDealHost* host) { g_host = host; }

bool cSC4NeighborDeal::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZSerializable) {
        *out = static_cast<cIGZSerializable*>(this);
    } else if (iid == 1 || iid == kGZIID_cISC4NeighborDeal) {
        *out = static_cast<cISC4NeighborDeal*>(this);
    } else {
        return false;
    }
    AddRef();
    return true;
}

std::uint32_t cSC4NeighborDeal::Release() {
    if (refCount_ > 1) return --refCount_;
    delete this;
    return 0;
}

void* cSC4NeighborDeal::GetCityOnOtherSideOfConnection() const {
    void* city = nullptr;
    return g_host->CityEntryAtCell(otherX_, otherZ_, city) ? city : nullptr;
}

void cSC4NeighborDeal::SetAmount(int amount) {
    if (!g_host->CityHasBudget()) return;
    if (CheckFlagsOr(kDealFlagFrozen)) return;
    amount_ = amount;
    if (!CheckFlagsOr(kDealFlagSilent)) g_host->ReportDealStatus(false, this);
    UpdateCost();
}

void cSC4NeighborDeal::UpdateCost() {
    if (CheckFlagsOr(kDealFlagFrozen)) return;
    std::int64_t cost = static_cast<std::int64_t>(g_host->DealCost(static_cast<double>(amount_), type_, GetCityOnOtherSideOfConnection()));
    cost_ = cost != 0 ? cost : 1;
    if (!CheckFlagsOr(kDealFlagSilent)) g_host->ReportDealStatus(true, this);
}

// flags set while the state bits (0x38) are locked by 0x100 keep their old state bits
std::uint32_t cSC4NeighborDeal::SetFlags(std::uint32_t flags, bool on) {
    const std::uint32_t old = flags_;
    if (on) {
        std::uint32_t add = flags;
        if ((old & kDealFlagLockedState) != 0 && (flags & kDealStateMask) != 0) add = flags & 0xffffffc7u;
        flags_ = add | old;
    } else {
        flags_ = ~flags & old;
    }
    if (!CheckFlagsOr(kDealFlagSilent) && flags_ != old) g_host->ReportDealStatus(true, this);
    return flags_;
}

// @ 0x100075b46. Spending deals that the city cannot pay for become unaffordable. An otherwise affordable
// deal only gets the "affordable" flag when its connection disappeared; while a suitable connection exists no
// state flag is touched (that is what keeps a healthy deal in the active branch of the neighbour simulator).
void cSC4NeighborDeal::UpdateState() {
    const std::uint32_t offset = type_ + 0xb88c0020u;
    const bool spends = offset < 5 && ((0x15u >> offset) & 1) != 0;
    const bool affordableByFunds = !spends || static_cast<double>(cost_) <= g_host->CurrentRegionalCityBudget();
    if (affordableByFunds) {
        void* other = GetCityOnOtherSideOfConnection();
        if (other && g_host->HasSuitableConnection(type_, other)) return;
        SetFlags(kDealFlagAffordable, true);
    } else {
        SetFlags(kDealFlagUnaffordable, true);
    }
}

void cSC4NeighborDeal::Cancel() {
    SetFlags(kDealFlagCancelled, true);
    SetFlags(kDealFlagActive, false);
    SetAmount(0);
}

bool cSC4NeighborDeal::Activate() {
    if (CheckFlagsOr(kDealFlagProposed) && !CheckFlagsOr(kDealStateMask)) {
        SetFlags(kDealFlagActive, true);
        SetFlags(kDealFlagProposed, false);
        SetFlags(kDealFlagCancelled, false);
        return true;
    }
    return false;
}

void cSC4NeighborDeal::Copy(const cISC4NeighborDeal& other) {
    amount_ = other.GetAmount();
    amountUsed_ = other.GetAmountUsed();
    cost_ = other.GetCost();
    type_ = other.GetType();
    flags_ = other.GetFlags();
    incAmount_ = other.GetIncAmount();
    lastAmount_ = other.GetLastAmount();
    amountUsed_ = other.GetAmountUsed();
    cGZDate date;
    other.GetStartDate(date);
    start_.SetJulianDay(date.DayNumber());
    other.GetExpirationDate(date);
    expiration_.SetJulianDay(date.DayNumber());
    other.GetCurrentDate(date);
    current_.SetJulianDay(date.DayNumber());
    other.GetSuspensionDate(date);
    suspension_.SetJulianDay(date.DayNumber());
    other.GetRegionCellOnOtherSideOfConnection(otherX_, otherZ_);
    other.GetRegionCellOnCitySideOfConnection(cityX_, cityZ_);
}

// The copy normally describes the deal from this city's side; when the city-side cell does not belong to the
// current city the copy is seen from the neighbour: cells swap and buy / sell types exchange.
bool cSC4NeighborDeal::Clone(cISC4NeighborDeal** out, bool silent) const {
    auto* copy = new cSC4NeighborDeal();
    copy->AddRef();
    *out = copy;
    copy->AddRef();
    copy->Copy(*this);
    copy->SetFlags(kDealFlagSilent, silent);
    void* current = g_host->CurrentCity();
    void* citySide = nullptr;
    void* otherSide = nullptr;
    const bool haveCitySide = g_host->CityEntryAtCell(cityX_, cityZ_, citySide);
    const bool haveOtherSide = g_host->CityEntryAtCell(otherX_, otherZ_, otherSide);
    if (!haveCitySide || !haveOtherSide) {
        copy->SetFlags(kDealFlagCancelled, true);
    } else if (current != citySide) {
        copy->SetRegionCellOnCitySideOfConnection(otherX_, otherZ_);
        copy->SetRegionCellOnOtherSideOfConnection(cityX_, cityZ_);
        switch (copy->GetType()) {
            case 0x4773ffe0: copy->SetType(0x4773ffe1); break;
            case 0x4773ffe1: copy->SetType(0x4773ffe0); break;
            case 0x4773ffe2: copy->SetType(0x4773ffe3); break;
            case 0x4773ffe3: copy->SetType(0x4773ffe2); break;
            case 0x4773ffe4: copy->SetType(0x4773ffe5); break;
            case 0x4773ffe5: copy->SetType(0x4773ffe4); break;
            default: break;
        }
    }
    copy->Release();
    return true;
}

bool cSC4NeighborDeal::Write(cIGZOStream& stream) const {
    stream.SetUint16(5);
    stream.SetUint32(type_);
    stream.SetSint32(otherX_);
    stream.SetSint32(otherZ_);
    stream.SetSint32(amount_);
    stream.SetSint32(amountUsed_);
    stream.SetSint64(cost_);
    stream.SetSint32(static_cast<std::int32_t>(flags_));
    stream.SetSint32(cityX_);
    stream.SetSint32(cityZ_);
    stream.SetSint32(incAmount_);
    stream.SetSint32(lastAmount_);
    stream.SetSint32(amountUsed_);
    stream.SetUint32(current_.JulianDay());
    stream.SetUint32(start_.JulianDay());
    stream.SetUint32(expiration_.JulianDay());
    cSCPropertyHolder::Write(stream);
    stream.SetUint32(suspension_.JulianDay());
    return !stream.GetError();
}

bool cSC4NeighborDeal::Read(cIGZIStream& stream) {
    std::uint16_t version = 0;
    stream.GetUint16(version);
    stream.GetUint32(type_);
    stream.GetSint32(otherX_);
    stream.GetSint32(otherZ_);
    stream.GetSint32(amount_);
    stream.GetSint32(amountUsed_);
    stream.GetSint64(cost_);
    std::int32_t flags = 0;
    stream.GetSint32(flags);
    flags_ = static_cast<std::uint32_t>(flags);
    stream.GetSint32(cityX_);
    stream.GetSint32(cityZ_);
    stream.GetSint32(incAmount_);
    stream.GetSint32(lastAmount_);
    stream.GetSint32(amountUsed_);
    std::uint32_t julian = 0;
    stream.GetUint32(julian);
    current_.SetJulianDay(julian);
    stream.GetUint32(julian);
    start_.SetJulianDay(julian);
    stream.GetUint32(julian);
    expiration_.SetJulianDay(julian);
    cSCPropertyHolder::Read(stream);
    if (version > 4) {
        stream.GetUint32(julian);
        suspension_.SetJulianDay(julian);
    }
    return !stream.GetError();
}

}  // namespace sc4
