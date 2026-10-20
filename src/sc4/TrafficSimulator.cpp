#include "sc4/TrafficSimulator.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

namespace sc4 {

namespace {
// 1.5 * 2^23 rounding trick of the original: float -> nearest integer
inline int RoundToInt(float v) { return int(std::lrintf(v)); }

constexpr std::uint32_t kExemplarProperty_curveF78 = 0xca5f7821, kExemplarProperty_curveFB8 = 0x4be09ec9,
                        kExemplarProperty_curveFD8 = 0x4be09eca, kExemplarProperty_fundingCurve = 0x0bd86fd4;
}  // namespace

const std::array<std::uint8_t, 9> cSC4TrafficSimulator::kTravelModeToTransitNetwork = {0, 1, 1, 2, 1, 2, 3, 4, 5};

// ---- CellSet ------------------------------------------------------------------------------------------

bool CellSet::Insert(std::uint16_t cell) {
    if (!index_.emplace(cell, items_.size()).second) return false;
    items_.push_back(cell);
    return true;
}

bool CellSet::Erase(std::uint16_t cell) {
    const auto it = index_.find(cell);
    if (it == index_.end()) return false;
    const std::size_t slot = it->second;
    const std::uint16_t last = items_.back();
    items_[slot] = last;
    index_[last] = slot;
    items_.pop_back();
    index_.erase(cell);
    return true;
}

// ---- construction / tunables ----------------------------------------------------------------------------

cSC4TrafficSimulator::cSC4TrafficSimulator(ITrafficHost* host, cRZRandom* random) : host_(host), random_(random) {
    tunables_.travelTypeEnabled.fill(true);
    tunables_.travelTypeFlag_e95.fill(true);
    travelTypeAffectedByTraffic_.fill(false);
    for (auto& row : tunables_.inverseMaximumSpeed) row.fill(FLT_MAX);
    tunables_.tripScaleForDisplay = 0.0f;
    tunables_.tripScale = 90.0f;
}

bool cSC4TrafficSimulator::ReadSimulatorTunableValues(const cISCPropertyHolder* exemplar) {
    if (!exemplar) return false;
    TrafficTunables& t = tunables_;
    using H = cSCPropertyHelp;
    H::GetPropertyValue(exemplar, 0x491332e6, t.value_bcc);

    for (int type = 0; type < 9; ++type) {
        std::vector<float> speeds;
        auto& perMinute = t.cellsPerMinute[type];
        auto& inverse = t.inverseMaximumSpeed[type];
        int filled = 0;
        if (H::GetPropertyValue(exemplar, 0x491332e7 + type, speeds)) {
            filled = int(std::min<std::size_t>(speeds.size(), 13));
            for (int i = 0; i < filled; ++i) {
                if (speeds[i] <= 0.0f) {
                    perMinute[i] = 0.0f;
                    inverse[i] = FLT_MAX;
                } else {
                    perMinute[i] = ((speeds[i] * 1000.0f) / 60.0f) / host_->CityCellSize();
                    inverse[i] = 1.0f / perMinute[i];
                }
            }
        }
        for (int i = filled; i < 13; ++i) {
            perMinute[i] = 0.0f;
            inverse[i] = FLT_MAX;
        }
    }
    H::GetPropertyValue(exemplar, 0x29136788, t.value_bd4);
    H::GetPropertyValue(exemplar, 0x29136789, t.value_bd0);

    std::vector<float> values;
    H::GetPropertyValue(exemplar, 0x6a84493e, values);
    for (std::size_t i = 0; i < 13; ++i) t.budgetScale[i] = i < values.size() ? values[i] : 0.0f;

    SC4Percentage percentage;
    if (H::GetPropertyValue(exemplar, 0xa92356ae, percentage)) t.value_de0 = percentage.value;
    if (H::GetPropertyValue(exemplar, 0xa92356af, percentage)) t.value_de4 = percentage.value;
    H::GetPropertyValue(exemplar, 0xa92356b0, t.potholePenalty);
    H::GetPropertyValue(exemplar, 0xa92356b6, t.value_ea0);
    H::GetPropertyValue(exemplar, 0xa92356b7, t.value_ea4);
    H::GetPropertyValue(exemplar, 0xa92356b8, t.value_ea8);

    if (H::GetPropertyValue(exemplar, 0xa92356b1, values)) {
        for (std::size_t i = 0; i < 9 && i < values.size(); ++i) t.values_dfc[i] = values[i];
    }
    if (H::GetPropertyValue(exemplar, 0xa92356b3, values)) {
        for (std::size_t i = 0; i < 13; ++i) {
            const float v = i < values.size() ? values[i] : 0.0f;
            t.values_e24[i] = v;
            t.inverse_e58[i] = std::fabs(v) > 1e-10f ? 1.0f / v : 1.0f;
        }
    }
    std::vector<bool> flags;
    const auto readFlags = [&](std::uint32_t id, auto& out) {
        if (!H::GetPropertyValue(exemplar, id, flags)) return;
        for (std::size_t i = 0; i < 9; ++i) out[i] = i < flags.size() ? bool(flags[i]) : true;
    };
    readFlags(0xa92356b4, t.travelTypeEnabled);
    readFlags(0xa92356bf, travelTypeAffectedByTraffic_);
    readFlags(0xa92356b5, t.travelTypeFlag_e95);

    // congestion -> inverse speed curve: y values are stored as speeds and used as their reciprocal
    {
        std::vector<float> curveValues;
        if (H::GetPropertyValue(exemplar, 0xa92356b9, curveValues)) congestionToInverseSpeed_.SetResponseCurveData(curveValues.data(), curveValues.size());
        std::vector<SC4Point> points;
        congestionToInverseSpeed_.GetResponseCurveData(points);
        for (SC4Point& p : points) p.y = 1.0f / p.y;
        congestionToInverseSpeed_.SetResponseCurveData(points);
        if (!points.empty()) t.value_eb0 = 255.0f / (points.back().y - 1.0f);
    }
    H::GetPropertyValue(exemplar, 0xa92356ba, t.tripScale);
    if (H::GetPropertyValue(exemplar, 0xa92356bb, values)) {
        t.count_ec0 = std::uint8_t(values.size());
        t.inverse_ec8.assign(values.size() & 0xff, 0.0f);
        for (std::size_t i = 0; i < t.count_ec0 && i < values.size(); ++i) t.inverse_ec8[i] = 1.0f / values[i];
    }
    const auto readNine = [&](std::uint32_t id, auto& out) {
        if (!H::GetPropertyValue(exemplar, id, values)) return;
        for (std::size_t i = 0; i < 9; ++i) out[i] = i < values.size() ? values[i] : 0.0f;
    };
    readNine(0xa92356bc, t.values_ee0);
    readNine(0xea8c3cdb, t.values_f04);
    readNine(0xcad64136, t.values_f28);
    H::GetPropertyValue(exemplar, 0xa92356bd, t.value_f4c);
    H::GetPropertyValue(exemplar, 0x49a2e8be, t.freightScalingFactor);
    H::GetPropertyValue(exemplar, 0x4a678060, t.value_f60);
    H::GetPropertyValue(exemplar, 0xca76013b, t.tripScaleForDisplay);
    H::GetPropertyValue(exemplar, 0xea7b5f06, t.value_f68);
    if (H::GetPropertyValue(exemplar, 0xa92356be, values)) {
        const std::size_t n = std::min<std::size_t>(values.size(), 3);
        for (std::size_t i = 0; i < n; ++i) t.backgroundTrafficWeights[i] = values[i];
        for (std::size_t i = n; i < 3; ++i) t.backgroundTrafficWeights[i] = 1.0f;
    }

    const auto loadCurve = [&](cSC4ResponseCurve& curve, std::uint32_t id, const std::vector<SC4Point>& fallback) {
        std::vector<float> curveValues;
        if (exemplar->HasProperty(id) && H::GetPropertyValue(exemplar, id, curveValues)) curve.SetResponseCurveData(curveValues.data(), curveValues.size());
        else curve.SetResponseCurveData(fallback);
    };
    loadCurve(curveF78_, kExemplarProperty_curveF78, {{0.0f, 0.0f}, {1.0f, 1.0f}});
    loadCurve(curveFB8_, kExemplarProperty_curveFB8, {{0.25f, 0.0f}, {1.0f, 0.05f}, {2.0f, 0.25f}});
    loadCurve(curveFD8_, kExemplarProperty_curveFD8, {{0.0f, 1.0f}, {1000.0f, 1.0f}});
    t.value_ff8 = 10.0f;
    H::GetPropertyValue(exemplar, 0x4be09ecb, t.value_ff8);
    t.value_ffc = 60.0f;
    H::GetPropertyValue(exemplar, 0x4be09ecc, t.value_ffc);
    loadCurve(fundingCurve_, kExemplarProperty_fundingCurve, {{0.0f, 0.02f}, {100.0f, -0.01f}, {120.0f, -0.04f}});
    t.railAccidentChance = 1.0f;
    H::GetPropertyValue(exemplar, 0x0bd86fd5, t.railAccidentChance);
    H::GetPropertyValue(exemplar, 0x8c3caef5, t.value_f70);
    UpdateTravelStrategies(exemplar);
    return true;
}

void cSC4TrafficSimulator::UpdateTravelStrategies(const cISCPropertyHolder* exemplar) {
    if (exemplar) {
        for (int wealth = 0; wealth < 4; ++wealth) {
            std::vector<std::uint8_t> bytes;
            if (cSCPropertyHelp::GetPropertyValue(exemplar, 0x4953e8a3 + wealth, bytes)) {
                for (int i = 0; i < 3 && i < int(bytes.size()); ++i) travelStrategies_[wealth * 3 + i] = bytes[i];
            }
        }
    }
    std::vector<std::vector<std::int32_t>> ordinances;
    host_->ActiveOrdinanceStrategyAdjustments(ordinances);
    for (const auto& adjust : ordinances) {
        if (adjust.size() < 9) continue;
        for (int group = 0; group < 3; ++group) {
            for (int i = 0; i < 3; ++i) {
                std::uint8_t& slot = travelStrategies_[3 + group * 3 + i];
                int v = int(slot) + adjust[group * 3 + i];
                if (v < 0) v = 0;
                if (v > 99) v = 100;
                slot = std::uint8_t(v);
            }
        }
    }
}

void cSC4TrafficSimulator::GetTravelStrategyPercentages(unsigned wealth, std::vector<SC4Percentage>& out) const {
    out.resize(3);
    for (int i = 0; i < 3; ++i) out[i].value = float(travelStrategies_[wealth * 3 + i]) * 0.01f;
}

// ---- network info grid ----------------------------------------------------------------------------------

void cSC4TrafficSimulator::InitGrid(int cellsX, int cellsZ) {
    lastX_ = cellsX - 1;
    lastZ_ = cellsZ - 1;
    grid_.assign(std::size_t(cellsX), std::vector<std::vector<NetworkInfo>>(std::size_t(cellsZ)));
}

std::vector<NetworkInfo>* cSC4TrafficSimulator::Cell(int x, int z) const {
    if (x < 0 || z < 0 || std::size_t(x) >= grid_.size() || std::size_t(z) >= grid_[std::size_t(x)].size()) return nullptr;
    return &grid_[std::size_t(x)][std::size_t(z)];
}

NetworkInfo* cSC4TrafficSimulator::FindNetworkInfo(int networkType, int x, int z) const {
    std::vector<NetworkInfo>* cell = Cell(x, z);
    if (!cell) return nullptr;
    for (NetworkInfo& info : *cell) {
        if (info.networkType == networkType) return &info;
    }
    return nullptr;
}

NetworkInfo* cSC4TrafficSimulator::GetNetworkInfo(int networkType, int x, int z) { return FindNetworkInfo(networkType, x, z); }
const NetworkInfo* cSC4TrafficSimulator::GetNetworkInfo(int networkType, int x, int z) const { return FindNetworkInfo(networkType, x, z); }

NetworkInfo* cSC4TrafficSimulator::AddNetworkInfo(int networkType, int x, int z) {
    if (NetworkInfo* existing = FindNetworkInfo(networkType, x, z)) return existing;
    std::vector<NetworkInfo>* cell = Cell(x, z);
    if (!cell) return nullptr;
    NetworkInfo info;
    info.networkType = std::uint8_t(networkType);
    cell->push_back(info);
    return &cell->back();
}

void cSC4TrafficSimulator::RemoveNetworkInfo(int networkType, int x, int z) {
    std::vector<NetworkInfo>* cell = Cell(x, z);
    if (!cell) return;
    const auto it = std::find_if(cell->begin(), cell->end(), [&](const NetworkInfo& i) { return i.networkType == networkType; });
    if (it != cell->end()) cell->erase(it);
}

float cSC4TrafficSimulator::GetCapacity(int networkType, int x, int z) const {
    const NetworkInfo* info = GetNetworkInfo(networkType, x, z);
    return info ? 1.0f / info->inverseCapacity : 0.0f;
}

std::uint8_t cSC4TrafficSimulator::GetConnectionCount(int networkType, int x, int z) const {
    const NetworkInfo* info = GetNetworkInfo(networkType, x, z);
    return info ? info->connectionCount : 0;
}

std::uint32_t cSC4TrafficSimulator::EdgeDensityKey(int x, int z, int travelType, bool secondDirection) {
    return std::uint32_t(x << 8) | std::uint32_t(z) | (std::uint32_t(travelType) & 0xff) << 16 | std::uint32_t(secondDirection) << 24;
}

TrafficEdgeDensity& cSC4TrafficSimulator::EdgeDensityEntry(int x, int z, int travelType, bool secondDirection) {
    return edgeDensities_[EdgeDensityKey(x, z, travelType, secondDirection)];
}

const TrafficEdgeDensity& cSC4TrafficSimulator::GetTrafficEdgeDensity(int x, int z, int travelType, bool secondDirection) const {
    const auto it = edgeDensities_.find(EdgeDensityKey(x, z, travelType, secondDirection));
    return it == edgeDensities_.end() ? emptyEdgeDensity_ : it->second;
}

float cSC4TrafficSimulator::GetTravelTimeRatio(int x, int z, int travelType) const {
    if (travelTypeAffectedByTraffic_[std::size_t(travelType)]) {
        std::uint32_t density = 0;
        for (int k = 0; k < 9; ++k) {
            if (!tunables_.travelTypeEnabled[k]) continue;
            // both directions of the edge (the original passes the second flag implicitly for the second call)
            density += GetTrafficEdgeDensity(x & 0xff, z & 0xff, k, false).density;
            density += GetTrafficEdgeDensity(x & 0xff, z & 0xff, k, true).density;
        }
        const NetworkInfo* info = GetNetworkInfo(kTravelModeToTransitNetwork[std::size_t(travelType)], x, z);
        if (info) return congestionToInverseSpeed_.GetOutputValue(float(density) * info->inverseCapacity);
    }
    return 1.0f;
}

float cSC4TrafficSimulator::GetBackgroundTraffic(int x, int z) const {
    static constexpr std::uint32_t kGroup0[] = {0x1010, 0x1020, 0x1030};
    static constexpr std::uint32_t kGroup1[] = {0x3110, 0x3120, 0x3130, 0x3320, 0x3330};
    static constexpr std::uint32_t kGroup2[] = {0x4100, 0x4200, 0x4300, 0x4400};
    int sums[3] = {0, 0, 0};
    for (std::uint32_t id : kGroup0) sums[0] += host_->DemandGridValue(id, x, z);
    for (std::uint32_t id : kGroup1) sums[1] += host_->DemandGridValue(id, x, z);
    for (std::uint32_t id : kGroup2) sums[2] += host_->DemandGridValue(id, x, z);
    float result = 0.0f;
    for (int i = 0; i < 3; ++i) result += float(sums[i]) * tunables_.backgroundTrafficWeights[std::size_t(i)];
    return result;
}

float cSC4TrafficSimulator::TransitSwitchEntryCost(std::uint32_t id) {
    TransitSwitch& entry = transitSwitches_[id];
    const float ratio = entry.traffic / entry.capacity;
    if (5.0f <= ratio) return 1.7014117e+38f;
    return congestionToInverseSpeed_.GetOutputValue(ratio) * entry.cost * transitSwitchCostScale;
}

// ---- roads / rail damage ----------------------------------------------------------------------------------

void cSC4TrafficSimulator::ApplyPotholeChanges(CellSet& healthy, CellSet& damaged, int delta, int networkType) {
    CellSet& source = delta > 0 ? healthy : damaged;
    CellSet& target = delta > 0 ? damaged : healthy;
    std::uint32_t count = delta > 0 ? std::uint32_t(delta) : std::uint32_t(-delta);
    count = std::min<std::uint32_t>(count, std::uint32_t(source.Size()));
    for (; count != 0; --count) {
        const std::uint16_t cell = source.At(random_->RandomUint32Uniform(std::uint32_t(source.Size())));
        const int x = cell >> 8, z = cell & 0xff;
        if (NetworkInfo* info = GetNetworkInfo(networkType, x, z)) {
            info->potholePenalty = delta > 0 ? tunables_.potholePenalty : 0.0f;
            host_->PostPotholeMessage(x, z, delta > 0);
        }
        source.Erase(cell);
        target.Insert(cell);
    }
}

void cSC4TrafficSimulator::ApplyRoadFunding(const SC4Percentage& funding) {
    const float step = fundingCurve_.GetOutputValue(funding.value * 100.0f);
    roadFundingAccum_ = std::min(std::max(roadFundingAccum_ + step, 0.0f), 1.0f);
    const int target = int(roadFundingAccum_ * float(roadPotholeTotal_));
    if (roadPotholeCount_ != target) {
        const int delta = target - roadPotholeCount_;
        ApplyPotholeChanges(healthyRoads_, damagedRoads_, delta, 1);
        roadPotholeCount_ += delta;
        statRoadPotholes_ = short(roadPotholeCount_);
    }
}

void cSC4TrafficSimulator::ApplyMassTransitFunding(const SC4Percentage& funding) {
    const float step = fundingCurve_.GetOutputValue(funding.value * 100.0f);
    railFundingAccum_ = std::min(std::max(railFundingAccum_ + step, 0.0f), 1.0f);
    const int previous = railPotholeCount_;
    const int target = int(railFundingAccum_ * float(railPotholeTotal_));
    if (previous == target) return;
    railPotholeCount_ = target;
    ApplyPotholeChanges(healthyRail_, damagedRail_, target - previous, 2);
}

bool cSC4TrafficSimulator::CheckRailAccident(int x, int z) const {
    if (!damagedRail_.Contains(std::uint16_t((x << 8) | z))) return false;
    return random_->RandomDoubleUniform() < double(tunables_.railAccidentChance);
}

// ---- strikes and budgets ---------------------------------------------------------------------------------

void cSC4TrafficSimulator::UpdateStrikes() {
    const float funding = host_->MassTransitFundingFraction();
    const TrafficTunables& t = tunables_;
    if (strikeState_ == kStrikeNone) {
        if (funding < t.strikeThreatFunding && strikeEligible_ > 0) {
            strikeChance_ = (t.strikeThreatFunding - funding) * t.strikeChanceScale;
            if (random_->RandomDoubleUniform() < double(strikeChance_)) {
                strikeState_ = kStrikeThreatened;
                host_->PostMessage(kTrafficMessageStrikeThreatened);
            }
        }
        return;
    }
    if (strikeState_ == kStrikeOn) {
        if (funding < t.strikeEndFunding && std::uint32_t(strikeDays_) < t.strikeMaxDays && strikeEligible_ != 0) {
            ++strikeDays_;
            return;
        }
        strikeState_ = kStrikeNone;
        strikeActive_ = false;
        host_->PostMessage(kTrafficMessageStrikeEnded);
    } else if (strikeState_ == kStrikeThreatened) {
        if (t.strikeEndFunding <= funding || strikeEligible_ == 0) {
            strikeState_ = kStrikeNone;
            host_->PostMessage(kTrafficMessageStrikeAverted);
        } else {
            strikeState_ = kStrikeOn;
            strikeDays_ = 0;
            strikeActive_ = true;
            host_->PostMessage(kTrafficMessageStrikeBegan);
        }
    }
}

void cSC4TrafficSimulator::UpdateRoadsBudget() {
    const auto& k = tunables_.budgetScale;
    const auto line = [&](std::uint32_t count, float scale) { return RoundToInt(float(count) * scale); };
    host_->SetRoadBudgetExpense(2, line(counters_.c930, k[10]) + line(counters_.c908, k[0]));
    host_->SetRoadBudgetExpense(1, line(counters_.c914, k[3]));
    host_->SetRoadBudgetExpense(3, line(counters_.c938, k[12]) + line(counters_.c910, k[2]));
    host_->SetRoadBudgetExpense(4, line(counters_.c920, k[6]));
}

void cSC4TrafficSimulator::UpdateMassTransitBudget() {
    const auto& k = tunables_.budgetScale;
    const auto line = [&](std::uint32_t count, float scale) { return RoundToInt(float(count) * scale); };
    host_->SetMassTransitBudgetExpense(3, line(counters_.c90c, k[1]));
    host_->SetMassTransitBudgetExpense(4, line(counters_.c924, k[7]));
    host_->SetMassTransitBudgetExpense(5, line(counters_.c928, k[8]));
    host_->SetMassTransitBudgetExpense(6, line(counters_.c92c, k[9]));
}

// ---- trip capacity -----------------------------------------------------------------------------------------

std::uint32_t cSC4TrafficSimulator::GetTripCapacity(const cISCPropertyHolder* holder, std::uint32_t index) {
    cISCProperty* property = holder->GetProperty(kTripCapacityProperty);
    if (!property) return 0;
    cIGZVariant* value = property->GetPropertyValue();
    if (value->GetType() == 0x8004) return value->RefUint16()[index];
    return value->RefUint32()[index];
}

void cSC4TrafficSimulator::SetTripCapacity(cISCPropertyHolder* holder, std::uint32_t index, std::uint32_t valueToSet) {
    cISCProperty* property = holder->GetProperty(kTripCapacityProperty);
    if (!property) {
        auto* created = new cSCProperty(kTripCapacityProperty);
        std::uint16_t zeros[12] = {};
        created->GetPropertyValue()->RefUint16(zeros, 12);
        holder->AddProperty(created, false);   // the holder takes the only reference
        property = created;
    }
    cIGZVariant* value = property->GetPropertyValue();
    if (value->GetType() == 0x8004) {
        std::uint16_t* narrow = const_cast<std::uint16_t*>(value->RefUint16());
        if (valueToSet < 0xffff) {
            narrow[index] = std::uint16_t(valueToSet);
            return;
        }
        std::uint32_t widened[12];
        for (int i = 0; i < 12; ++i) widened[i] = narrow[i];
        value->RefUint32(widened, 12);
    }
    const_cast<std::uint32_t*>(value->RefUint32())[index] = valueToSet;
}

// ---- neighbour geometry ------------------------------------------------------------------------------------

int cSC4TrafficSimulator::GetNeighborConnectionInnerCell(int x, int z, int& innerX, int& innerZ) const {
    const bool xOutside = x < 0 || lastX_ < x;
    if (z < 0) {
        if (xOutside) return -1;
        innerX = x;
        innerZ = 0;
        return 1;
    }
    if (!((lastZ_ < z) != xOutside)) return -1;
    innerX = x;
    innerZ = z;
    if (xOutside) {
        if (x >= 0) { innerX = lastX_; return 2; }
        innerX = 0;
        return 0;
    }
    innerZ = lastZ_;
    return 3;
}

void cSC4TrafficSimulator::FindHighwayReturnTile(int& x, int& z, bool flag) const {
    const bool right = host_->IsRightHandTraffic();
    int step;
    if (right == flag) {
        if (x < 0) { --z; return; }
        if (lastX_ < x) { ++z; return; }
        if (z < 0) step = 1;
        else { step = -1; if (z <= lastZ_) return; }
    } else {
        if (x < 0) { ++z; return; }
        if (lastX_ < x) { --z; return; }
        if (z >= 0) { step = 1; if (z <= lastZ_) return; }
        else step = -1;
    }
    x += step;
}

void cSC4TrafficSimulator::InvalidateIntersectionData(int x0, int z0, int x1, int z1) {
    if (!intersectionDirty_) {
        dirtyX0_ = x0; dirtyZ0_ = z0; dirtyX1_ = x1; dirtyZ1_ = z1;
        intersectionDirty_ = true;
        return;
    }
    dirtyX0_ = std::min(dirtyX0_, x0);
    dirtyZ0_ = std::min(dirtyZ0_, z0);
    dirtyX1_ = std::max(dirtyX1_, x1);
    dirtyZ1_ = std::max(dirtyZ1_, z1);
}

// ---- subnetworks ---------------------------------------------------------------------------------------------

void cSC4TrafficSimulator::ClearSubnetworks() {
    nextSubnetworkId_ = 1;
    for (auto& column : grid_)
        for (auto& cell : column)
            for (NetworkInfo& info : cell) info.subnetwork.fill(0);
}

void cSC4TrafficSimulator::GetSubnetworksForLot(const cISCPropertyHolder* lot, std::vector<std::uint32_t>& out) const {
    out.clear();
    cSCPropertyHelp::GetPropertyValue(lot, kLotSubnetworksProperty, out);
}

namespace {
void AppendNetworkSubnetworks(const NetworkInfo& info, std::vector<std::uint32_t>& out) {
    for (std::uint16_t id : info.subnetwork)
        if (id != 0 && std::find(out.begin(), out.end(), id) == out.end()) out.push_back(id);
}
}  // namespace

// Road cells (network type 1) only; cells outside the grid are skipped. An inverted rectangle yields nothing.
void cSC4TrafficSimulator::GetSubnetworksInRectangle(const TrafficCellRect& rect, std::vector<std::uint32_t>& out) const {
    out.clear();
    for (int x = rect.x0; x <= rect.x1; ++x)
        for (int z = rect.z0; z <= rect.z1; ++z)
            if (const NetworkInfo* info = GetNetworkInfo(1, x, z)) AppendNetworkSubnetworks(*info, out);
}

void cSC4TrafficSimulator::GetSubnetworksInRegion(const TrafficCellRegion& region, std::vector<std::uint32_t>& out) const {
    out.clear();
    for (int x = region.bounds.x0; x <= region.bounds.x1; ++x)
        for (int z = region.bounds.z0; z <= region.bounds.z1; ++z)
            if (region.Contains(x, z))
                if (const NetworkInfo* info = GetNetworkInfo(1, x, z)) AppendNetworkSubnetworks(*info, out);
}

// False when either lot has no (or an empty) subnetwork list.
bool cSC4TrafficSimulator::AreLotsConnected(const cISCPropertyHolder* a, const cISCPropertyHolder* b) const {
    std::vector<std::uint32_t> first, second;
    if (!cSCPropertyHelp::GetPropertyValue(a, kLotSubnetworksProperty, first)) return false;
    if (!cSCPropertyHelp::GetPropertyValue(b, kLotSubnetworksProperty, second)) return false;
    for (std::uint32_t id : first)
        if (std::find(second.begin(), second.end(), id) != second.end()) return true;
    return false;
}

// Largest count of `occupantType` over the lot's subnetworks; ids past the published vector (and id 0's entry
// like any other) are handled as in the original: only the bound check applies.
std::uint32_t cSC4TrafficSimulator::GetConnectedOccupantCount(const cISCPropertyHolder* lot, std::uint32_t occupantType) const {
    std::vector<std::uint32_t> ids;
    if (!cSCPropertyHelp::GetPropertyValue(lot, kLotSubnetworksProperty, ids)) return 0;
    std::uint32_t best = 0;
    for (std::uint32_t id : ids) {
        if (id >= subnetworks_.size()) continue;
        const auto it = subnetworks_[id].occupantsByType.find(occupantType);
        if (it != subnetworks_[id].occupantsByType.end() && it->second > best) best = it->second;
    }
    return best;
}

// Destination types 0..3; anything else gives 0.
std::uint32_t cSC4TrafficSimulator::GetConnectedDestinationCount(const cISCPropertyHolder* lot, int destinationType) const {
    if (destinationType >= 4) return 0;
    std::vector<std::uint32_t> ids;
    if (!cSCPropertyHelp::GetPropertyValue(lot, kLotSubnetworksProperty, ids)) return 0;
    std::uint32_t best = 0;
    for (std::uint32_t id : ids)
        if (id < subnetworks_.size() && subnetworks_[id].destinations[std::size_t(destinationType)] > best)
            best = subnetworks_[id].destinations[std::size_t(destinationType)];
    return best;
}

// One entry per published subnetwork (entry 0 stays 0), the count of `occupantType` where present.
void cSC4TrafficSimulator::GetOccupantCountForAllSubnetworks(std::uint32_t occupantType, std::vector<std::uint32_t>& out) const {
    out.assign(subnetworks_.size(), 0);
    for (std::size_t i = 1; i < subnetworks_.size(); ++i) {
        const auto it = subnetworks_[i].occupantsByType.find(occupantType);
        if (it != subnetworks_[i].occupantsByType.end()) out[i] = it->second;
    }
}

}  // namespace sc4
