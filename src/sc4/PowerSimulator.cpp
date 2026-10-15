#include "sc4/PowerSimulator.h"

#include <algorithm>

namespace sc4 {

namespace {
// Bit1_* take (bits, size, column, row) in the order the power simulator calls them; the declared parameter
// names in gz/Bit1.h are the same slots seen from the helper's own decompile.
inline bool TestBit(const std::uint32_t* bits, int size, int x, int z) { return Bit1_PixelTest(bits, size, x, z); }
inline cRZRect ToRect(const PowerRect& r) { cRZRect out; out.left = r.left; out.top = r.top; out.right = r.right; out.bottom = r.bottom; return out; }
}  // namespace

cSC4PowerSimulator::cSC4PowerSimulator(IPowerHost* host, cRZRandom* random) : host_(host), random_(random) {
    ordinanceFactor = 1.0f;
    value318 = 1.0f;
}

cSC4PowerSimulator::~cSC4PowerSimulator() { ShutdownMarkAndWireBits(); }

void cSC4PowerSimulator::ReadSimulatorTunableValues(const cISCPropertyHolder* exemplar) {
    neighborhoodRadius_ = 2;
    value318 = 1.1f;
    value3f4 = 1.2f;
    fundingEfficiencyCurve.SetResponseCurveData(std::vector<SC4Point>{{0.0f, 0.0f}, {100.0f, 100.0f}, {120.0f, 110.0f}});
    curve448.SetResponseCurveData(std::vector<SC4Point>{{0.0f, 1.0f}, {1.0f, 1.0f}});
    curve468.SetResponseCurveData(std::vector<SC4Point>{{0.0f, 1.0f}, {1.0f, 1.0f}});
    curve428.SetResponseCurveData(std::vector<SC4Point>{{0.0f, 20.0f}, {100.0f, 100.0f}});
    poleFundingCurve.SetResponseCurveData(std::vector<SC4Point>{{0.0f, -0.02f}, {120.0f, 0.01f}});
    strikeEfficiency.value = 0.5f;
    value3fc = 10;
    value400 = 0.9f;
    conditionSafe = 0.6f;
    if (exemplar) {
        using H = cSCPropertyHelp;
        const auto loadCurve = [&](cSC4ResponseCurve& curve, std::uint32_t id) {
            std::vector<float> values;
            if (exemplar->HasProperty(id) && H::GetPropertyValue(exemplar, id, values)) curve.SetResponseCurveData(values.data(), values.size());
        };
        std::uint32_t radius = 0;
        if (H::GetPropertyValue(exemplar, 0xa911e2f1, radius)) neighborhoodRadius_ = int(radius);
        float f = 0;
        if (H::GetPropertyValue(exemplar, 0xe911e2fc, f)) value318 = f / 100.0f;
        if (H::GetPropertyValue(exemplar, 0x29121e8b, f)) conditionSafe = f * 0.01f;
        loadCurve(fundingEfficiencyCurve, 0x6911e2df);
        if (H::GetPropertyValue(exemplar, 0xe911e2d1, f)) value3f4 = f * 0.01f;
        if (H::GetPropertyValue(exemplar, 0x09122027, f)) strikeEfficiency.value = f * 0.01f;
        H::GetPropertyValue(exemplar, 0x2912202e, value3fc);
        if (H::GetPropertyValue(exemplar, 0x49122036, f)) value400 = f * 0.01f;
        loadCurve(curve428, 0x0911e2e6);
        loadCurve(curve448, 0x0911e2e8);
        loadCurve(curve468, 0x0911e2e9);
        loadCurve(poleFundingCurve, 0xaa713782);
    }
    ordinanceFactor = host_->OrdinancePowerFactor();
}

// ---- coordinates -------------------------------------------------------------------------------------------

void cSC4PowerSimulator::PositionToCell(float x, float z, int& cellX, int& cellZ) const {
    cellX = int((1.0f / host_->CellSizeX()) * x);
    cellZ = int((1.0f / host_->CellSizeZ()) * z);
}

void cSC4PowerSimulator::CellToPosition(int cellX, int cellZ, float& x, float& z) const {
    x = host_->CellSizeX() * float(cellX);
    z = host_->CellSizeZ() * float(cellZ);
}

namespace {
// start of a window: r cells before `v`, but not below 0 (unsigned compare as in the original)
inline int WindowStart(int v, std::uint32_t r) { return (std::uint32_t(v) <= r) ? 0 : int(std::uint32_t(v) - r); }
// end of a window: r cells after `v`, clamped to the last cell
inline int WindowEnd(int v, std::uint32_t r, int cells) { return (std::uint32_t(r) + std::uint32_t(v) < std::uint32_t(cells)) ? v + int(r) : cells - 1; }
}  // namespace

void cSC4PowerSimulator::ExpandRectToIncludeNeighborhood(PowerRect& rect) const {
    const std::uint32_t r = std::uint32_t(neighborhoodRadius_);
    rect.left = WindowStart(rect.left, r);
    rect.top = WindowStart(rect.top, r);
    rect.right = WindowEnd(rect.right, r, host_->CityCellsX());
    rect.bottom = WindowEnd(rect.bottom, r, host_->CityCellsZ());
}

void cSC4PowerSimulator::GetNeighborhoodOfPoint(int x, int z, PowerRect& out) const {
    const std::uint32_t r = std::uint32_t(neighborhoodRadius_);
    out.left = WindowStart(x, r);
    out.top = WindowStart(z, r);
    out.right = WindowEnd(x, r, host_->CityCellsX());
    out.bottom = WindowEnd(z, r, host_->CityCellsZ());
}

// ---- bit maps ------------------------------------------------------------------------------------------------

void cSC4PowerSimulator::InitMarkAndWireBits() {
    ShutdownMarkAndWireBits();
    const int cells = host_->CityCellsX() * host_->CityCellsZ();
    bitWords_ = std::uint32_t((cells + (std::uint32_t(cells >> 31) >> 27)) >> 5);
    const int size = host_->CityCellsX();
    std::uint32_t** maps[] = {&visitedBits_, &wireBits_, &poweredBits_, &gridVisitedBits_};
    for (std::uint32_t** map : maps) {
        *map = new std::uint32_t[bitWords_];
        Bit1_Clear(*map, size);
    }
}

void cSC4PowerSimulator::ShutdownMarkAndWireBits() {
    delete[] wireBits_;        wireBits_ = nullptr;
    delete[] visitedBits_;     visitedBits_ = nullptr;
    delete[] poweredBits_;     poweredBits_ = nullptr;
    delete[] gridVisitedBits_; gridVisitedBits_ = nullptr;
}

bool cSC4PowerSimulator::IsPowered(int x, int z) const { return TestBit(poweredBits_, host_->CityCellsX(), x, z); }
bool cSC4PowerSimulator::IsWired(int x, int z) const { return TestBit(wireBits_, host_->CityCellsX(), x, z); }

bool cSC4PowerSimulator::SetsWireBits(IPowerOccupant* occupant) {
    const std::uint32_t type = occupant->OccupantType();
    return type == kPowerOccupantBuilding || type == kPowerOccupantPole || type == kPowerOccupantNetwork || type == kPowerOccupantPylon;
}

void cSC4PowerSimulator::ResetWireBits(const PowerRect& rect) {
    Bit1_RectClear(wireBits_, host_->CityCellsX(), ToRect(rect));
}

void cSC4PowerSimulator::UpdateWireBits(const PowerRect& rect, IPowerOccupant* ignore) {
    const int size = host_->CityCellsX();
    PowerRect search;
    search.left = rect.left - neighborhoodRadius_ * 2;
    search.top = rect.top - neighborhoodRadius_ * 2;
    search.right = rect.right + neighborhoodRadius_ * 2;
    search.bottom = rect.bottom + neighborhoodRadius_ * 2;
    std::vector<IPowerOccupant*> occupants;
    host_->OccupantsInRect(search, occupants);
    for (IPowerOccupant* occupant : occupants) {
        if (occupant == ignore || !SetsWireBits(occupant)) continue;
        const std::uint32_t type = occupant->OccupantType();
        PowerRect footprint;
        if (type == kPowerOccupantPylon) {
            int x, z;
            if (!occupant->GetAnchorCell(x, z)) continue;      // only pylons flagged 0x40000 report a cell
            footprint = {x, z, x, z};                          // not expanded by the neighbourhood
        } else if (type == kPowerOccupantNetwork || type == kPowerOccupantPole) {
            int x, z;
            if (!occupant->GetAnchorCell(x, z)) continue;
            footprint = {x, z, x, z};
            ExpandRectToIncludeNeighborhood(footprint);
        } else {
            occupant->GetBounds(footprint);
            ExpandRectToIncludeNeighborhood(footprint);
        }
        Bit1_RectSet(wireBits_, size, ToRect(footprint));
    }
    for (int x = rect.left; x <= rect.right; ++x) {
        for (int z = rect.top; z <= rect.bottom; ++z) {
            if (!host_->IsPowerLineCell(x, z)) continue;
            PowerRect around;
            GetNeighborhoodOfPoint(x, z, around);
            Bit1_RectSet(wireBits_, size, ToRect(around));
        }
    }
    for (int x = rect.left; x <= rect.right; ++x) {
        for (int z = rect.top; z <= rect.bottom; ++z) {
            const bool wired = TestBit(wireBits_, size, x, z);
            const bool powered = TestBit(poweredBits_, size, x, z);
            host_->SetSupplyValue(x, z, wired ? std::uint8_t(powered + 1) : std::uint8_t(0));
        }
    }
}

bool cSC4PowerSimulator::CellChanged(unsigned x0, unsigned z0, unsigned x1, unsigned z1) {
    PowerRect rect{int(x0), int(z0), int(x1), int(z1)};
    ExpandRectToIncludeNeighborhood(rect);
    if (host_->IsPowerLineCell(int(x0), int(z0))) Bit1_RectSet(wireBits_, host_->CityCellsX(), ToRect(rect));
    // the pending rectangle collects the neighbourhood-expanded rectangles
    if (!pending_) {
        pending_ = true;
        dirty_ = true;
        pendingRect_ = rect;
    } else {
        pendingRect_.left = std::min(pendingRect_.left, rect.left);
        pendingRect_.top = std::min(pendingRect_.top, rect.top);
        pendingRect_.right = std::max(pendingRect_.right, rect.right);
        pendingRect_.bottom = std::max(pendingRect_.bottom, rect.bottom);
    }
    return true;
}

// ---- plants ---------------------------------------------------------------------------------------------------------

std::uint32_t cSC4PowerSimulator::GetCurrentPowerProduced(IPowerOccupant* plant) {
    if (!plant) return 0;
    std::uint32_t maxProduced = 0;
    if (!cSCPropertyHelp::GetPropertyValue(plant->PropertyHolder(), kPowerPlantMaxProduced, maxProduced)) return maxProduced;
    return std::uint32_t(float(maxProduced) * GetCurrentPlantEfficiencyPercent(plant));
}

float cSC4PowerSimulator::GetCurrentPlantConditionPercent(IPowerOccupant* plant) {
    if (!plant) return 0.0f;
    cISCPropertyHolder* holder = plant->PropertyHolder();
    if (!holder->HasProperty(kPowerPlantConditionCurve)) return 0.0f;
    cSC4ResponseCurve curve;
    std::vector<float> values;
    if (cSCPropertyHelp::GetPropertyValue(holder, kPowerPlantConditionCurve, values)) curve.SetResponseCurveData(values.data(), values.size());
    float wear = 0.0f;
    cSCPropertyHelp::GetPropertyValue(holder, kPowerPlantWear, wear);
    return curve.GetOutputValue(wear);
}

float cSC4PowerSimulator::GetCurrentPlantEfficiencyPercent(IPowerOccupant* plant) {
    if (!plant) return 0.0f;
    const PowerPlantBudgetEntry entry = host_->GetPlantBudgetEntry(plant);
    float funded = 0.0f;
    if (entry.tracked) funded = fundingEfficiencyCurve.GetOutputValue(entry.funding * 100.0f) * 0.01f;
    return GetCurrentPlantConditionPercent(plant) * funded;
}

bool cSC4PowerSimulator::GetPowerPlantFundingPercentage(IPowerOccupant* plant, SC4Percentage& out) {
    const PowerPlantBudgetEntry entry = host_->GetPlantBudgetEntry(plant);
    if (!entry.tracked) return false;
    out.value = entry.funding;
    return true;
}

bool cSC4PowerSimulator::GetMonthlyPowerGenerationCost(IPowerOccupant* plant, std::int64_t& cost) {
    const PowerPlantBudgetEntry entry = host_->GetPlantBudgetEntry(plant);
    if (!entry.tracked) return false;
    cost = entry.monthlyCost;
    return true;
}

void cSC4PowerSimulator::FindPowerPlants(IPowerOccupant** leastFunded, SC4Percentage* funding, IPowerOccupant** mostWorn, int* wearPercent) {
    if (plants_.empty()) return;
    float lowestFunding = 2.0f, highestWear = 0.0f;
    IPowerOccupant* lowest = nullptr;
    IPowerOccupant* worn = nullptr;
    for (const PowerPlantEntry& entry : plants_) {
        cISCPropertyHolder* holder = entry.occupant->PropertyHolder();
        const PowerPlantBudgetEntry budget = host_->GetPlantBudgetEntry(entry.occupant);
        if (budget.tracked && budget.hasExplicitFunding && budget.funding < lowestFunding) {
            lowestFunding = budget.funding;
            lowest = entry.occupant;
        }
        float wear = 0.0f, limitA = 99.0f, limitB = 99.0f;
        cSCPropertyHelp::GetPropertyValue(holder, kPowerPlantWear, wear);
        cSCPropertyHelp::GetPropertyValue(holder, 0xea1cf221, limitA);
        cSCPropertyHelp::GetPropertyValue(holder, 0xea1cf222, limitB);
        wear = wear / std::max(limitA, limitB);
        if (highestWear < wear) {
            highestWear = wear;
            worn = entry.occupant;
        }
    }
    if (mostWorn && worn) {
        *mostWorn = worn;
        *wearPercent = int(highestWear * 100.0f);
    }
    if (leastFunded && lowest) {
        *leastFunded = lowest;
        if (funding) funding->value = lowestFunding;
    }
}

void cSC4PowerSimulator::MarkPowerPlantsAsVisited(std::uint32_t used, std::uint32_t capacity) {
    float scale;
    if (usedCapacity6c < 1) scale = 0.0f;
    else scale = float(used) / (capacity == 0 ? 1.0f : float(capacity));
    for (PowerPlantEntry& entry : plants_) {
        if (entry.state != 1) continue;
        cISCPropertyHolder* holder = entry.occupant->PropertyHolder();
        std::int32_t type = 0;
        const std::uint32_t produced = GetCurrentPowerProduced(entry.occupant);
        cSCPropertyHelp::GetPropertyValue(holder, kPowerPlantPowerType, type);
        const int current = int(float(produced) * scale);
        cSCPropertyHelp::SetPropertyValue(holder, kPowerPlantCurrentProduced, std::int32_t(current));
        monthlyPower_[std::size_t(month_)][std::size_t(type)] += current;
        entry.state = 2;
    }
}

bool cSC4PowerSimulator::GetOccupantPowerProduced(IPowerOccupant* plant, std::uint32_t& produced, std::uint32_t& currentProduced) {
    cISCPropertyHolder* holder = plant->PropertyHolder();
    if (!holder->HasProperty(kPowerPlantMaxProduced)) return false;
    produced = GetCurrentPowerProduced(plant);
    currentProduced = 0;
    if (holder->HasProperty(kPowerPlantCurrentProduced)) {
        std::uint32_t v = 0;
        cSCPropertyHelp::GetPropertyValue(holder, kPowerPlantCurrentProduced, v);
        currentProduced = v;
    }
    return true;
}

bool cSC4PowerSimulator::GetOccupantMaxPowerProduced(IPowerOccupant* plant, std::uint32_t& out) {
    return cSCPropertyHelp::GetPropertyValue(plant->PropertyHolder(), kPowerPlantMaxProduced, out);
}

void cSC4PowerSimulator::GetPowerPlants(std::list<IPowerOccupant*>& out) const {
    for (const PowerPlantEntry& entry : plants_) out.push_back(entry.occupant);
}

// ---- poles and funding -----------------------------------------------------------------------------------------------

void cSC4PowerSimulator::ApplyPoleFunding(const SC4Percentage& funding) {
    poleFundingAccum_ -= poleFundingCurve.GetOutputValue(funding.value * 100.0f);
    if (poleFundingAccum_ > 1.0f) poleFundingAccum_ = 1.0f;
    else if (poleFundingAccum_ < 0.0f) poleFundingAccum_ = 0.0f;
    const std::uint32_t target = std::uint32_t(float(poleCount_) * poleFundingAccum_);
    if (enabledPoles_ == target) return;
    std::vector<IPowerPole*> shuffled = poles_;
    for (std::size_t i = shuffled.size(); i > 1; --i) std::swap(shuffled[i - 1], shuffled[random_->RandomUint32Uniform(std::uint32_t(i))]);
    for (IPowerPole* pole : shuffled) {
        if (enabledPoles_ == target) break;
        if (!pole->IsEnabled()) {
            if (enabledPoles_ < target) { pole->SetEnabled(true); ++enabledPoles_; }
        } else if (target < enabledPoles_) {
            pole->SetEnabled(false);
            --enabledPoles_;
        }
    }
}

bool cSC4PowerSimulator::RecomputeInfrastructureCosts(const std::vector<std::int64_t>& poleCosts) {
    std::uint32_t sum = 0;
    for (std::int64_t cost : poleCosts) sum += std::uint32_t(cost);
    const std::uint32_t previous = infrastructureCost_;
    infrastructureCost_ = sum;
    return previous != sum;
}

// ---- monthly statistics --------------------------------------------------------------------------------------------------

void cSC4PowerSimulator::DoMonthlySimulation() {
    ordinanceFactor = host_->OrdinancePowerFactor();
    month_ = month_ > 10 ? 0 : month_ + 1;
    auto& slot = monthlyPower_[std::size_t(month_)];
    annualPower_[0] -= slot[0];
    slot[0] = 0;
    for (int k = 1; k < kPowerTypes; ++k) {
        annualPower_[std::size_t(k)] -= slot[std::size_t(k)];
        slot[std::size_t(k)] = 0;
    }
    if (usedCapacity6c < capacity70 && 0 < usedCapacity6c) ratio94 = float((usedCapacity6c - capacity70) / usedCapacity6c);
    else ratio94 = 0.0f;
    for (int k = 0; k < kPowerTypes; ++k) annualPower_[std::size_t(k)] += slot[std::size_t(k)];   // the slot was just cleared
    dirty_ = true;
    dirtySecond_ = true;
    ApplyPoleFunding(SC4Percentage{host_->DepartmentFundingFraction()});
}

int cSC4PowerSimulator::GetAnnualConsumption() const {
    int sum = 0;
    for (int v : annualPower_) sum += v;
    return sum;
}

}  // namespace sc4
