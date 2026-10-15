#include "sc4/PollutionSimulator.h"

#include <algorithm>
#include <cmath>

namespace sc4 {

namespace {
// Messages posted at the end of a cycle when the matching grid changed.
constexpr std::uint32_t kMessageRadiationChanged = 0xda6dd35d;
constexpr std::uint32_t kMessageAirChanged = 0xfa6dd0bb;
constexpr std::uint32_t kMessageWaterChanged = 0x5a6dd1aa;
constexpr std::uint32_t kMessageGarbageChanged = 0xda6dd2cb;
constexpr std::uint32_t kMessageSanitationEfficiency = 0xea418d8f;
// history records
constexpr std::uint32_t kHistoryAir = 0x0a1100e8;
constexpr std::uint32_t kHistoryWater = 0xea110130;
constexpr std::uint32_t kHistoryGarbage = 0xea110134;
constexpr std::uint32_t kHistoryRadiation = 0xea110137;
constexpr std::uint32_t kHistoryTotal = 0xea110138;

inline std::int32_t RoundToEven(float v) { return static_cast<std::int32_t>(std::nearbyint(v)); }
}  // namespace

cSC4PollutionSimulator::cSC4PollutionSimulator(IPollutionHost* host) : host_(host) {}

void cSC4PollutionSimulator::ReadSimulatorTunableValues(const cISCPropertyHolder* exemplar) {
    tunables_ = PollutionTunables();
    // default response curves: the prop request / effect curves start as the single point (0, 0)
    const std::vector<SC4Point> zero{{0.0f, 0.0f}};
    propRequestCurve_.SetResponseCurveData(zero);
    garbageTransportCapacity_.SetResponseCurveData(zero);
    const std::vector<SC4Point> identity{{0.0f, 1.0f}, {1.0f, 1.0f}};
    ageCurve_.SetResponseCurveData(identity);
    efficiencyCurve_.SetResponseCurveData(identity);
    if (!exemplar) return;
    PollutionTunables& t = tunables_;
    cSCPropertyHelp::GetPropertyValue(exemplar, kPollutionPropTrafficAirFactor, t.trafficAirFactor);
    cSCPropertyHelp::GetPropertyValue(exemplar, kPollutionPropMaxGarbageForUI, t.maxGarbageForUI);
    cSCPropertyHelp::GetPropertyValue(exemplar, kPollutionPropMaxAirForUI, t.maxAirForUI);
    cSCPropertyHelp::GetPropertyValue(exemplar, kPollutionPropMaxWaterForUI, t.maxWaterForUI);
    cSCPropertyHelp::GetPropertyValue(exemplar, kPollutionPropMaxRadiationForUI, t.maxRadiationForUI);
    cSCPropertyHelp::GetPropertyValue(exemplar, kPollutionPropMaxAirDelta, t.maxAirDelta);
    cSCPropertyHelp::GetPropertyValue(exemplar, kPollutionPropMaxWaterDelta, t.maxWaterDelta);
    cSCPropertyHelp::GetPropertyValue(exemplar, kPollutionPropGarbageThreshold, t.garbageThreshold);
    cSCPropertyHelp::GetPropertyValue(exemplar, kPollutionPropAirThreshold, t.airThreshold);
    cSCPropertyHelp::GetPropertyValue(exemplar, kPollutionPropWaterThreshold, t.waterThreshold);
    cSCPropertyHelp::GetPropertyValue(exemplar, kPollutionPropRadiationThreshold, t.radiationThreshold);
    cSCPropertyHelp::GetPropertyValue(exemplar, kPollutionPropAbductionAmount, t.abductionAmount);
    cSCPropertyHelp::GetPropertyValue(exemplar, kPollutionPropGarbageScaling, t.garbageScalingFactor);
    cSCPropertyHelp::GetPropertyValue(exemplar, kPollutionPropFloat2e0, t.value2e0);
    cSCPropertyHelp::GetPropertyValue(exemplar, kPollutionPropMaxGarbageOnValve, t.maxGarbageOnValve);
    cSCPropertyHelp::GetPropertyValue(exemplar, kPollutionPropLandfillTileCapacity, t.landfillTileCapacity);
    auto loadCurve = [&](cSC4ResponseCurve& curve, std::uint32_t id) {
        std::vector<float> values;
        if (exemplar->HasProperty(id) && cSCPropertyHelp::GetPropertyValue(exemplar, id, values))
            curve.SetResponseCurveData(values.data(), values.size());
    };
    loadCurve(propRequestCurve_, 0x69a437f6);
    loadCurve(garbageTransportCapacity_, 0x8c19c3e9);
    loadCurve(ageCurve_, 0x0911e2e8);
    loadCurve(efficiencyCurve_, 0x0911e2e9);
    loadCurve(fundingEfficiencyCurve_, kPollutionPropFundingEfficiencyCurve);
}

void cSC4PollutionSimulator::CreateGrids() {
    for (int i = 0; i < 4; ++i) {
        if (!valueGrids_[i]) {
            valueGrids_[i] = std::make_unique<cSC4SimGrid<std::int16_t>>();
            valueGrids_[i]->SetTractSize(2);
            valueGrids_[i]->SetInstanceID(kPollutionGridInstanceValueBase + static_cast<std::uint32_t>(i));
        }
        if (!sourceGrids_[i]) {
            sourceGrids_[i] = std::make_unique<cSC4SimGrid<std::int16_t>>();
            sourceGrids_[i]->SetTractSize(i == 2 ? 1 : 2);
            sourceGrids_[i]->SetInstanceID(kPollutionGridInstanceSourceBase + static_cast<std::uint32_t>(i));
        }
    }
}

cSC4SimGrid<std::int16_t>* cSC4PollutionSimulator::GetPollutionGrid(PollutionType type) const {
    return type == kPollutionRadiation ? valueGrids_[3].get() : sourceGrids_[type].get();
}

std::int16_t cSC4PollutionSimulator::GetPollutionValue(PollutionType type, int x, int z) const {
    const std::int16_t v = valueGrids_[type]->GetCellValue(x, z);
    return v < 0 ? 0 : v;
}

bool cSC4PollutionSimulator::IsRadioactive(unsigned x, unsigned z) const {
    return valueGrids_[3]->GetCellValue(static_cast<int>(x), static_cast<int>(z)) != 0;
}

void cSC4PollutionSimulator::GetAirValue(unsigned x, unsigned z, int& out) const {
    out = sourceGrids_[0]->GetCellValue(static_cast<int>(x), static_cast<int>(z));
}

void cSC4PollutionSimulator::GetWaterValue(unsigned x, unsigned z, int& out) const {
    out = sourceGrids_[1]->GetCellValue(static_cast<int>(x), static_cast<int>(z));
}

// The garbage grid keeps the "active landfill" flag in bit 15; the value is read through the sign-extended short
// with that bit cleared, which leaves negative results for flagged cells (original quirk).
void cSC4PollutionSimulator::GetGarbageValue(unsigned x, unsigned z, int& out) const {
    const int v = sourceGrids_[2]->GetCellValue(static_cast<int>(x), static_cast<int>(z));
    out = v & static_cast<int>(0xffff7fff);
}

bool cSC4PollutionSimulator::IsAirPolluted(unsigned x, unsigned z) const {
    int value = 0;
    GetAirValue(x, z, value);
    return tunables_.airThreshold <= static_cast<std::uint32_t>(value);
}

bool cSC4PollutionSimulator::IsWaterPolluted(unsigned x, unsigned z) const {
    int value = 0;
    GetWaterValue(x, z, value);
    return tunables_.waterThreshold <= static_cast<std::uint32_t>(value);
}

bool cSC4PollutionSimulator::IsGarbageAccumulated(unsigned x, unsigned z) const {
    int value = 0;
    GetGarbageValue(x, z, value);
    return tunables_.garbageThreshold <= static_cast<std::uint32_t>(value);
}

void cSC4PollutionSimulator::SetIsRadioactive(unsigned x, unsigned z, bool on) {
    auto& grid = *valueGrids_[3];
    const int shift = grid.GetTractShift();
    grid.SetTractValue(static_cast<int>(x >> shift), static_cast<int>(z >> shift), on ? 1 : 0);
    dirtyRadiation = true;
}

void cSC4PollutionSimulator::SetAirValue(unsigned x, unsigned z, int value) {
    auto& grid = *sourceGrids_[0];
    const int shift = grid.GetTractShift();
    grid.SetTractValue(static_cast<int>(x >> shift), static_cast<int>(z >> shift), static_cast<std::int16_t>(value));
    dirtyAir = true;
}

void cSC4PollutionSimulator::SetWaterValue(unsigned x, unsigned z, int value) {
    auto& grid = *sourceGrids_[1];
    const int shift = grid.GetTractShift();
    grid.SetTractValue(static_cast<int>(x >> shift), static_cast<int>(z >> shift), static_cast<std::int16_t>(value));
    dirtyWater = true;
}

void cSC4PollutionSimulator::SetGarbageValue(unsigned x, unsigned z, int value) {
    auto& grid = *sourceGrids_[2];
    const int shift = grid.GetTractShift();
    const std::uint16_t flag = static_cast<std::uint16_t>(grid.GetCellValue(static_cast<int>(x), static_cast<int>(z))) & 0x8000;
    grid.SetTractValue(static_cast<int>(x >> shift), static_cast<int>(z >> shift),
                       static_cast<std::int16_t>(flag | static_cast<std::uint16_t>(value)));
    dirtyGarbage = true;
}

bool cSC4PollutionSimulator::IsActiveLandfill(unsigned x, unsigned z) const {
    return (static_cast<std::uint16_t>(sourceGrids_[2]->GetCellValue(static_cast<int>(x), static_cast<int>(z))) &
            0x8000) != 0;
}

// Turning a landfill on needs the cell to be zoned landfill (type 0xe); turning it off always works. The write
// goes straight to the tract (x, z) without shifting the cell coordinates, as in the original.
void cSC4PollutionSimulator::SetIsActiveLandfill(unsigned x, unsigned z, bool on, std::uint32_t zoneTypeAtCell) {
    if (zoneTypeAtCell != 0xe && on) return;
    auto& grid = *sourceGrids_[2];
    const bool active = IsActiveLandfill(x, z);
    std::int16_t value;
    if (!active && on) {
        value = static_cast<std::int16_t>(grid.GetCellValue(static_cast<int>(x), static_cast<int>(z)) | 0x8000);
        ++activeLandfillCount_;
    } else {
        if (!active || on) return;
        const std::int16_t current = grid.GetCellValue(static_cast<int>(x), static_cast<int>(z));
        if (activeLandfillCount_ != 0) --activeLandfillCount_;
        value = static_cast<std::int16_t>(current & 0x7fff);
    }
    grid.SetTractValue(static_cast<int>(x), static_cast<int>(z), value);
    dirtyGarbage = true;
}

std::int32_t cSC4PollutionSimulator::GetBuildingGarbageCapacity(cISCPropertyHolder* occupant) const {
    std::int32_t value = 0;
    if (!cSCPropertyHelp::GetPropertyValue(occupant, kPollutionOccGarbageCapacity, value)) return 0;
    return value;
}

void cSC4PollutionSimulator::AddToGarbageSupply(int amount) {
    if (amount < 0 && garbageSupply_ + amount < 0) amount = -garbageSupply_;
    garbageSupply_ = std::max(0, garbageSupply_ + amount);
}

int cSC4PollutionSimulator::GetGarbageSupply() {
    if (garbageSupply_ < 0) garbageSupply_ = 0;
    return garbageSupply_;
}

// Average garbage value over the rectangle through the prop-request curve, rounded to the nearest int.
int cSC4PollutionSimulator::GetRequestedShare(int x0, int z0, int x1, int z1) const {
    std::uint32_t total = 0;
    for (int x = x0; x <= x1; ++x) {
        for (int z = z0; z <= z1; ++z) {
            int value = 0;
            GetGarbageValue(static_cast<unsigned>(x), static_cast<unsigned>(z), value);
            total += static_cast<std::uint32_t>(value);
        }
    }
    const std::uint32_t area = static_cast<std::uint32_t>(((z1 + 1) - z0) * ((x1 + 1) - x0));
    const float average = static_cast<float>(static_cast<std::uint64_t>(total) / area) / 32767.0f;
    return RoundToEven(propRequestCurve_.GetOutputValue(average));
}

bool cSC4PollutionSimulator::AddOrRemoveOccupant(std::list<IPollutionOccupant*>& list, IPollutionOccupant* occupant,
                                                 bool add) {
    if (add) {
        list.push_back(occupant);
        return true;
    }
    auto it = std::find(list.begin(), list.end(), occupant);
    if (it == list.end()) return false;
    list.erase(it);
    return true;
}

// ------------------------------------------------------------------------------------- occupant helpers

void cSC4PollutionSimulator::GetRecyclingCenterMaxCapacity(IPollutionOccupant* occupant, float& percentage) const {
    percentage = 0.0f;
    SC4Percentage p;
    if (cSCPropertyHelp::GetPropertyValue(occupant->PropertyHolder(), kPollutionOccRecyclingMax, p)) percentage = p.value;
}

void cSC4PollutionSimulator::GetRecyclingCenterCurrentCapacity(IPollutionOccupant* occupant, float& percentage) const {
    std::uint32_t value = 0;
    if (GetCurrentCapacity(kPollutionOccRecyclingMax, occupant, value)) percentage = static_cast<float>(value) * 0.01f;
    else percentage = 0.0f;
}

void cSC4PollutionSimulator::GetMaxCapacity(std::uint32_t property, IPollutionOccupant* occupant,
                                            std::uint32_t& out) const {
    out = 0;
    cSCPropertyHelp::GetPropertyValue(occupant->PropertyHolder(), property, out);
}

// capacity * efficiency (zero when the occupant has no power), then scaled by the age curve of the occupant
bool cSC4PollutionSimulator::GetCurrentCapacity(std::uint32_t property, IPollutionOccupant* occupant,
                                                std::uint32_t& out) const {
    cISCPropertyHolder* holder = occupant->PropertyHolder();
    if (!cSCPropertyHelp::GetPropertyValue(holder, property, out)) {
        out = 0;
        return false;
    }
    if (!host_->OccupantPowered(occupant)) out = 0;
    out = static_cast<std::uint32_t>(static_cast<std::int64_t>(static_cast<float>(out) * efficiency_));
    if (holder->HasProperty(kPollutionOccAgeCurve)) {
        cSC4ResponseCurve curve;
        std::vector<float> values;
        if (cSCPropertyHelp::GetPropertyValue(holder, kPollutionOccAgeCurve, values))
            curve.SetResponseCurveData(values.data(), values.size());
        float age = 0.0f;
        cSCPropertyHelp::GetPropertyValue(holder, kPollutionOccAge, age);
        out = static_cast<std::uint32_t>(RoundToEven(curve.GetOutputValue(age) * static_cast<float>(out)));
    }
    return true;
}

void cSC4PollutionSimulator::GetIncineratorCurrentCapacity(IPollutionOccupant* occupant, std::uint32_t& out) const {
    out = 0;
    GetCurrentCapacity(kPollutionOccIncinerator, occupant, out);
}

void cSC4PollutionSimulator::GetWasteToEnergyCurrentCapacity(IPollutionOccupant* occupant, std::uint32_t& out) const {
    out = 0;
    GetCurrentCapacity(kPollutionOccWasteToEnergy, occupant, out);
}

void cSC4PollutionSimulator::GetWaterTreatmentMaxCapacity(IPollutionOccupant* occupant, std::uint32_t& out) const {
    out = 0;
    GetMaxCapacity(kPollutionOccWaterTreatment, occupant, out);
}

void cSC4PollutionSimulator::GetWaterTreatmentCurrentCapacity(IPollutionOccupant* occupant, std::uint32_t& out) const {
    out = 0;
    GetCurrentCapacity(kPollutionOccWaterTreatment, occupant, out);
}

void cSC4PollutionSimulator::GetWaterTreatmentUsedCapacity(IPollutionOccupant* occupant, std::uint32_t& out) const {
    out = 0;
    GetMaxCapacity(kPollutionOccWaterTreatmentUsed, occupant, out);
}

// 100 % scaled by the age curve; fails (leaving 100 %) when the occupant has no age curve
bool cSC4PollutionSimulator::GetCurrentOccupantCondition(IPollutionOccupant* occupant, float& percentage) const {
    percentage = 1.0f;                                                  // kSC4PercentageOneHundred
    cISCPropertyHolder* holder = occupant->PropertyHolder();
    if (!holder->HasProperty(kPollutionOccAgeCurve)) return false;
    cSC4ResponseCurve curve;
    std::vector<float> values;
    if (cSCPropertyHelp::GetPropertyValue(holder, kPollutionOccAgeCurve, values))
        curve.SetResponseCurveData(values.data(), values.size());
    float age = 0.0f;
    cSCPropertyHelp::GetPropertyValue(holder, kPollutionOccAge, age);
    percentage = curve.GetOutputValue(age) * percentage;
    return true;
}

bool cSC4PollutionSimulator::GetCurrentOccupantEfficiency(IPollutionOccupant* occupant, float& percentage) const {
    if (host_->PowerAvailable()) {
        percentage = efficiency_;
        float condition = 0.0f;
        if (GetCurrentOccupantCondition(occupant, condition)) {
            percentage = condition * percentage;
            return true;
        }
    }
    percentage = 0.0f;
    return false;
}

bool cSC4PollutionSimulator::GetUnusedCapacity(std::uint32_t maxProperty, std::uint32_t usedProperty,
                                               IPollutionOccupant* occupant, std::uint32_t& out) const {
    std::uint32_t maximum = 0;
    if (!GetCurrentCapacity(maxProperty, occupant, maximum)) return false;
    std::uint32_t used = 0;
    if (!cSCPropertyHelp::GetPropertyValue(occupant->PropertyHolder(), usedProperty, used)) return false;
    out = maximum < used ? 0 : maximum - used;
    return true;
}

void cSC4PollutionSimulator::GetIncineratorUsedCapacity(IPollutionOccupant* occupant, std::uint32_t& out) const {
    out = 0;
    cSCPropertyHelp::GetPropertyValue(occupant->PropertyHolder(), kPollutionOccIncineratorUsed, out);
}

void cSC4PollutionSimulator::GetWasteToEnergyUsedCapacity(IPollutionOccupant* occupant, std::uint32_t& out) const {
    out = 0;
    cSCPropertyHelp::GetPropertyValue(occupant->PropertyHolder(), kPollutionOccWasteToEnergyUsed, out);
}

bool cSC4PollutionSimulator::CalcOccupantMaintenanceCost(IPollutionOccupant* occupant, std::int64_t& out) const {
    cISCPropertyHolder* holder = occupant->PropertyHolder();
    out = 0;
    float age = 0.0f;
    if (!cSCPropertyHelp::GetPropertyValue(holder, kPollutionOccMaintenanceBase, out)) return false;
    if (!cSCPropertyHelp::GetPropertyValue(holder, kPollutionOccAge, age)) return false;
    if (!holder->HasProperty(kPollutionOccMaintenanceCurve)) return false;
    cSC4ResponseCurve curve;
    std::vector<float> values;
    if (cSCPropertyHelp::GetPropertyValue(holder, kPollutionOccMaintenanceCurve, values))
        curve.SetResponseCurveData(values.data(), values.size());
    out = static_cast<std::int64_t>(curve.GetOutputValue(age) * static_cast<float>(out));
    return true;
}

// Copies each occupant's "capacity used" counter into the saved property and zeroes the counter.
void cSC4PollutionSimulator::ResetOccupantListCapacityUsedProperty(std::list<IPollutionOccupant*>& list,
                                                                    std::uint32_t currentProperty,
                                                                    std::uint32_t savedProperty) const {
    for (IPollutionOccupant* occupant : list) {
        cISCPropertyHolder* holder = occupant->PropertyHolder();
        std::uint32_t current = 0;
        if (!holder->HasProperty(currentProperty)) continue;
        cSCPropertyHelp::GetPropertyValue(holder, currentProperty, current);
        if (holder->HasProperty(savedProperty)) cSCPropertyHelp::SetPropertyValue(holder, savedProperty, current);
        cSCPropertyHelp::SetPropertyValue(holder, currentProperty, std::uint32_t{0});
    }
}

void cSC4PollutionSimulator::ComputeWasteToEnergyListCurrentCapacity() {
    for (IPollutionOccupant* occupant : wasteToEnergy_) {
        cISCPropertyHolder* holder = occupant->PropertyHolder();
        std::uint32_t rate = 0, capacity = 0, used = 0;
        if (cSCPropertyHelp::GetPropertyValue(holder, kPollutionOccWasteToEnergyRate, rate) &&
            cSCPropertyHelp::GetPropertyValue(holder, kPollutionOccWasteToEnergy, capacity) &&
            cSCPropertyHelp::GetPropertyValue(holder, kPollutionOccWasteToEnergyUsed, used) && capacity != 0) {
            cSCPropertyHelp::SetPropertyValue(
                holder, kPollutionOccWasteToEnergyProduced,
                static_cast<std::uint32_t>(static_cast<std::uint64_t>(used) * rate / capacity));
        }
    }
}

// Recycling centres sorted by efficiency (best first); the best centres worth `slots` units of capacity decide
// the city-wide effect: sum(factor * efficiency) / max(slots, sum(factor)).
void cSC4PollutionSimulator::CalculateRecyclingCenterEffect() {
    const int slots = static_cast<int>(host_->RecyclingCapacitySlots());
    std::list<IPollutionOccupant*> sorted;
    if (recyclingCenterCountSnapshot == 0) {
        recyclingEffect_ = 0.0f;
        return;
    }
    for (IPollutionOccupant* occupant : recyclingCenters_) {
        float efficiency = 0.0f;
        if (!GetCurrentOccupantEfficiency(occupant, efficiency)) continue;
        auto position = sorted.begin();
        for (; position != sorted.end(); ++position) {
            float other = 0.0f;
            if (GetCurrentOccupantEfficiency(*position, other) && other <= efficiency) break;
        }
        sorted.insert(position, occupant);
    }
    recyclingCenters_ = sorted;
    float factorSum = 0.0f, weighted = 0.0f;
    if (slots > 0 && !recyclingCenters_.empty()) {
        const float limit = static_cast<float>(slots);
        for (IPollutionOccupant* occupant : recyclingCenters_) {
            float factor = 0.0f;
            if (cSCPropertyHelp::GetPropertyValue(occupant->PropertyHolder(), kPollutionOccRecyclingFactor, factor)) {
                factorSum += factor;
                float efficiency = 0.0f;
                if (GetCurrentOccupantEfficiency(occupant, efficiency)) weighted += factor * efficiency;
            }
            if (!(factorSum < limit)) break;
        }
        if (0.0f < factorSum) {
            recyclingEffect_ = weighted / std::max(limit, factorSum);
            return;
        }
    }
    recyclingEffect_ = 0.0f;
}

// Disposal priority: free recycling allowance, then incinerators, then waste-to-energy, then the landfill
// transport (limited by the landfill free space and the transport curve), then alien abduction.
float cSC4PollutionSimulator::eatGarbage(float amount, std::uint32_t& wasteToEnergyBudget,
                                         std::uint32_t& incineratorBudget) {
    if (amount <= 0.0f) return 0.0f;
    float eaten = 0.0f;
    const int recyclable = recyclingAllowance_ - static_cast<int>(recycledThisMonth_);
    if (recyclable > 0) {
        const float room = static_cast<float>(recyclable);
        const float taken = room < amount ? room : amount;
        recycledThisMonth_ = static_cast<std::int64_t>(static_cast<float>(recycledThisMonth_) + taken);
        eaten += taken;
        if (amount <= room) return eaten;
        amount -= room;
    }
    auto burn = [&](std::uint32_t& budget, std::int64_t& counter) {
        if (!(0.0f < amount) || budget == 0) return;
        float taken;
        if (static_cast<float>(budget) <= amount) {
            // original quirk: a budget no larger than the request is emptied without counting any garbage eaten
            taken = 0.0f;
            budget = 0;
        } else {
            budget -= static_cast<std::uint32_t>(RoundToEven(amount));
            taken = amount;
            amount = 0.0f;
        }
        counter = static_cast<std::int64_t>(static_cast<float>(counter) + taken);
        eaten += taken;
    };
    burn(incineratorBudget, incineratedThisMonth_);
    burn(wasteToEnergyBudget, wasteToEnergyThisMonth_);
    if (0.0f < amount) {
        int low = 0, high = 0;
        host_->LandfillRange(low, high);
        const int free = high - low;
        const float transport = garbageTransportCapacity_.GetOutputValue(static_cast<float>(host_->LandfillTransportLoad()));
        int room = RoundToEven(transport * efficiency_) - garbageSupply_;
        if (free < room) room = free;
        if (0 < room) {
            float moved;
            if (amount <= static_cast<float>(room)) {
                AddToGarbageSupply(RoundToEven(amount));
                moved = amount;
                amount = 0.0f;
            } else {
                moved = static_cast<float>(room);
                AddToGarbageSupply(room);
                amount -= moved;
            }
            eaten += moved;
        }
    }
    if (0.0f < amount) {
        const int allowance = static_cast<int>(tunables_.abductionAmount) - static_cast<int>(abductedThisMonth_);
        if (0 < allowance) {
            const float room = static_cast<float>(allowance);
            const float taken = amount <= room ? amount : room;
            abductedThisMonth_ = static_cast<std::uint32_t>(static_cast<std::int64_t>(static_cast<float>(abductedThisMonth_) + taken));
            eaten += taken;
        }
    }
    return eaten;
}

// Averages the period's sums into the published values, records the history, tells the UI which grids changed
// and refreshes the sanitation efficiency from the department funding.
void cSC4PollutionSimulator::EndOfCycle() {
    CalculateRecyclingCenterEffect();
    garbageUncollected_ = totalGarbageUncollected_ != 0;
    std::uint32_t radiationPercent = 0;
    std::uint64_t avgAir = 0, avgWater = 0;
    std::uint16_t avgGarbage = 0;
    if (cellCount != 0) {
        avgAir = airSum / cellCount;
        avgWater = waterSum / cellCount;
        avgGarbage = static_cast<std::uint16_t>(totalGarbageUncollected_ / cellCount);
        radiationPercent = (radiationCount * 100u) / cellCount;
    }
    averageAir_ = static_cast<std::int32_t>(avgAir);
    averageWater_ = static_cast<std::int32_t>(avgWater);
    averageGarbage_ = static_cast<std::int16_t>(avgGarbage);
    periodValue224[0] = periodValue21c[0];
    periodValue224[1] = periodValue21c[1];
    host_->AddHistory(kHistoryAir, static_cast<std::int64_t>(avgAir));
    host_->AddHistory(kHistoryWater, averageWater_);
    host_->AddHistory(kHistoryGarbage, static_cast<std::uint16_t>(averageGarbage_));
    host_->AddHistory(kHistoryRadiation, radiationCount);
    host_->AddHistory(kHistoryTotal, static_cast<std::int64_t>(radiationPercent) + averageAir_);
    cellCount = 0;
    radiationCount = 0;
    periodValue21c[0] = periodValue21c[1] = 0;
    airSum = waterSum = 0;
    if (dirtyRadiation) host_->PostMessage(kMessageRadiationChanged, 0);
    if (dirtyAir) host_->PostMessage(kMessageAirChanged, 0);
    if (dirtyWater) host_->PostMessage(kMessageWaterChanged, 0);
    if (dirtyGarbage) host_->PostMessage(kMessageGarbageChanged, 0);
    dirtyRadiation = dirtyAir = dirtyWater = dirtyGarbage = false;
    efficiency_ = fundingEfficiencyCurve_.GetOutputValue(host_->SanitationFunding() * 100.0f) * 0.01f;
    host_->PostMessage(kMessageSanitationEfficiency, 8);
}

}  // namespace sc4
