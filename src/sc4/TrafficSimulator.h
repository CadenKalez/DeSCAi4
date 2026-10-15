// cSC4TrafficSimulator — the data-driven core of the traffic simulator (class id 0x6990c1aa, simulator type
// 0x88027f0d). The original is a 15,000-line class (path finder, trip assignment, ferries, subnetwork flood
// fill); this file recovers the parts that do not depend on those: tunables, network-info grid, travel-time and
// capacity queries, funding -> road/rail damage, mass-transit strikes, budget feeds and neighbour-connection
// geometry (HasMapExit and the neighbour-connection tables are not recovered). Everything that talks to the city (messages, departments, demand grids, ordinances) goes through
// ITrafficHost.
//
// Original layout (offsets in the 0x1000-byte object): +0x40/+0x44 city size in cells, +0xd8 network-info grid
// (x -> z -> chain of NetworkInfo by network type), +0x2c0 edge-density map, +0x350 traffic stats,
// +0x8a0 transit-switch map, +0x880 random, +0x8d0/+0x8d8 road / mass-transit budget departments,
// +0x908..0x938 budget counters, +0x96c strike state, +0x974.. road damage, +0x9d0.. rail damage,
// +0xbd0..0xf70 tunables (read from exemplar {0x6534284a, 0xe7e2c2db, 0xc9133286}).
#pragma once
#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "rz/cRZRandom.h"
#include "sc4/SCPropertyHelp.h"
#include "sc4/cSC4ResponseCurve.h"

namespace sc4 {

constexpr std::uint32_t kGZCLSID_cSC4TrafficSimulator = 0x6990c1aa;
constexpr std::uint32_t kTrafficSimulatorType = 0x88027f0d;

// messages posted to the city
constexpr std::uint32_t kTrafficMessageStrikeAverted = 0x69247dc3, kTrafficMessageStrikeBegan = 0x69247dc4,
                        kTrafficMessageStrikeEnded = 0x69247dc5, kTrafficMessageStrikeThreatened = 0x69247dc6,
                        kTrafficMessagePotholeChanged = 0x69247dc8;
// exemplar property holding the 12 trip-capacity counters of a lot (16-bit array, widened on overflow)
constexpr std::uint32_t kTripCapacityProperty = 0x8a0b4100;

// Per-cell, per-network-type traffic data (the original chains these per cell).
struct NetworkInfo {
    float inverseCapacity = 0.0f;   // +0: capacity = 1 / this
    std::uint8_t connectionCount = 0;  // +4
    std::uint8_t networkType = 0;   // +6
    std::uint8_t flag7 = 0;         // +7
    float potholePenalty = 0.0f;    // +8: set while the cell has a pothole
    std::uint32_t reserved0c = 0, reserved10 = 0;
};

// Value part of an edge-density map entry; only the density word is read by the recovered code.
struct TrafficEdgeDensity { std::uint16_t density = 0; };

// A set of packed cells ((x << 8) | z) with O(1) insert, erase and uniform random pick. The original uses an
// unordered_set and walks to a random position; the distribution is the same.
class CellSet {
public:
    bool Insert(std::uint16_t cell);
    bool Erase(std::uint16_t cell);
    bool Contains(std::uint16_t cell) const { return index_.count(cell) != 0; }
    std::size_t Size() const { return items_.size(); }
    std::uint16_t At(std::size_t i) const { return items_[i]; }
    void Clear() { items_.clear(); index_.clear(); }
private:
    std::vector<std::uint16_t> items_;
    std::unordered_map<std::uint16_t, std::size_t> index_;
};

// One budget department (road or mass transit) line items by index.
class ITrafficHost {
public:
    virtual ~ITrafficHost() = default;
    virtual void PostMessage(std::uint32_t message) = 0;
    virtual void PostPotholeMessage(int x, int z, bool potholed) = 0;      // kTrafficMessagePotholeChanged
    virtual void SetRoadBudgetExpense(int line, int amount) = 0;           // road department line item
    // Mass transit department; line 3 is created on first use (ids 0x6a3ff01c / 0x6a8b0cfb) when missing.
    virtual void SetMassTransitBudgetExpense(int line, int amount) = 0;
    virtual float MassTransitFundingFraction() = 0;                        // department +0x50
    virtual float CityCellSize() = 0;                                      // metres per cell
    virtual bool IsRightHandTraffic() = 0;                                 // first lot-manager flag, true when absent
    virtual short DemandGridValue(std::uint32_t gridId, int x, int z) = 0; // building-development grids
    // property value (SC4Percentage as 0..1 per entry) of every active ordinance's travel-strategy adjustments
    virtual void ActiveOrdinanceStrategyAdjustments(std::vector<std::vector<std::int32_t>>& out) = 0;
};

struct TrafficTunables {
    float value_bcc = 0.0f;                            // 0x491332e6
    float value_bd0 = 0.03125f;                        // 0x29136789
    float value_bd4 = 0.0f;                            // 0x29136788
    std::array<std::array<float, 13>, 9> cellsPerMinute{};        // +0xbd8: speed per class, per travel type
    std::array<std::array<float, 13>, 9> inverseMaximumSpeed{};   // mfInverseMaximumSpeed
    std::array<float, 13> budgetScale{};               // +0xdac, 0x6a84493e
    float value_de0 = 1.2f, value_de4 = 1.2f;          // 0xa92356ae / af (percentages)
    float potholePenalty = 32.0f;                      // +0xde8, 0xa92356b0
    float strikeEndFunding = 1.0f;                     // +0xdec: a strike lasts while funding stays below this
    float strikeThreatFunding = 0.7f;                  // +0xdf0: below this a strike may be threatened
    std::uint32_t strikeMaxDays = 24;                  // +0xdf4
    float strikeChanceScale = 1.0f;                    // +0xdf8
    std::array<float, 9> values_dfc{};                 // 0xa92356b1
    std::uint8_t value_e20 = 0x3c;
    std::array<float, 13> values_e24{}, inverse_e58{}; // 0xa92356b3 and reciprocals
    std::array<bool, 9> travelTypeEnabled{};           // +0xe8c, 0xa92356b4 (true beyond the property)
    std::array<bool, 9> travelTypeFlag_e95{};          // 0xa92356b5
    std::uint32_t value_ea0 = 365, value_ea4 = 128, value_ea8 = 1;
    float tripScale = 90.0f;                           // +0xeac, 0xa92356ba
    float value_eb0 = 64.0f;                           // 255 / (last congestion value - 1)
    std::uint8_t count_ec0 = 0;
    std::vector<float> inverse_ec8;                    // 0xa92356bb reciprocals
    std::array<float, 9> values_ee0{}, values_f04{}, values_f28{};
    float value_f4c = 1.0f;
    std::array<float, 3> backgroundTrafficWeights{1.0f, 1.0f, 1.0f};  // +0xf50, 0xa92356be
    float freightScalingFactor = 0.0f;                 // +0xf5c
    float value_f60 = 0.75f, value_f64 = 0.5f, value_f68 = 4.0f;
    float railAccidentChance = 1.0f;                   // +0xf6c, 0x0bd86fd5
    float value_f70 = 0.001f;                          // 0x8c3caef5
    float value_ff8 = 10.0f, value_ffc = 60.0f;
    float tripScaleForDisplay = 0.0f;                  // +0xf64 (also tunable 0xca76013b)
};

class cSC4TrafficSimulator {
public:
    cSC4TrafficSimulator(ITrafficHost* host, cRZRandom* random);

    // ---- tunables (0x100078e16, 0x10008242c, 0x100082660 is not recovered) ----
    // Returns false when the simulator exemplar is missing (nothing changes).
    bool ReadSimulatorTunableValues(const cISCPropertyHolder* exemplar);
    void UpdateTravelStrategies(const cISCPropertyHolder* exemplar);
    const TrafficTunables& Tunables() const { return tunables_; }
    TrafficTunables& MutableTunables() { return tunables_; }
    cSC4ResponseCurve& CongestionToInverseSpeedCurve() { return congestionToInverseSpeed_; }
    cSC4ResponseCurve& FundingCurve() { return fundingCurve_; }

    // ---- network info grid ----
    void InitGrid(int cellsX, int cellsZ);                       // sets +0x40/+0x44 and clears the grid
    NetworkInfo* AddNetworkInfo(int networkType, int x, int z);  // valid until the next Add on that cell
    NetworkInfo* GetNetworkInfo(int networkType, int x, int z);
    const NetworkInfo* GetNetworkInfo(int networkType, int x, int z) const;
    void RemoveNetworkInfo(int networkType, int x, int z);
    float GetCapacity(int networkType, int x, int z) const;
    std::uint8_t GetConnectionCount(int networkType, int x, int z) const;
    float GetTravelTimeRatio(int x, int z, int travelType) const;
    float GetBackgroundTraffic(int x, int z) const;

    // ---- edge densities ----
    static std::uint32_t EdgeDensityKey(int x, int z, int travelType, bool secondDirection);
    TrafficEdgeDensity& EdgeDensityEntry(int x, int z, int travelType, bool secondDirection);
    const TrafficEdgeDensity& GetTrafficEdgeDensity(int x, int z, int travelType, bool secondDirection) const;

    // ---- transit switches ----
    struct TransitSwitch { std::uint32_t id = 0; float traffic = 0, cost = 0, capacity = 1; };  // +8 traffic, +0xc cost, +0x10 capacity
    TransitSwitch& TransitSwitchEntry(std::uint32_t id) { return transitSwitches_[id]; }
    float TransitSwitchEntryCost(std::uint32_t id);   // FLT_MAX-like 1.7014117e38 when saturated
    float transitSwitchCostScale = 1.0f;               // +0x8c8

    // ---- roads / rail damage and funding ----
    CellSet& HealthyRoadCells() { return healthyRoads_; }        // +0x980
    CellSet& DamagedRoadCells() { return damagedRoads_; }        // +0x9a8
    CellSet& HealthyRailCells() { return healthyRail_; }         // +0x9e0
    CellSet& DamagedRailCells() { return damagedRail_; }         // +0xa08
    void SetRoadPotholeCapacity(std::uint32_t total) { roadPotholeTotal_ = total; }
    void SetRailPotholeCapacity(std::uint32_t total) { railPotholeTotal_ = total; }
    void ApplyRoadFunding(const SC4Percentage& funding);
    void ApplyMassTransitFunding(const SC4Percentage& funding);
    void ApplyPotholeChanges(CellSet& healthy, CellSet& damaged, int delta, int networkType);
    bool IsRoadDamaged(int x, int z) const { return damagedRoads_.Contains(std::uint16_t((x << 8) | z)); }
    bool CheckRailAccident(int x, int z) const;
    int RoadPotholeCount() const { return roadPotholeCount_; }
    int RailPotholeCount() const { return railPotholeCount_; }
    short StatRoadPotholes() const { return statRoadPotholes_; }

    // ---- strikes and budgets ----
    enum StrikeState { kStrikeThreatened = 0, kStrikeOn = 1, kStrikeNone = 2 };
    void UpdateStrikes();
    StrikeState GetStrikeState() const { return strikeState_; }
    int StrikeDays() const { return strikeDays_; }
    void SetStrikeEligible(std::int64_t count) { strikeEligible_ = count; }
    bool StrikeActiveFlag() const { return strikeActive_; }
    float StrikeChance() const { return strikeChance_; }
    struct BudgetCounters { std::uint32_t c908 = 0, c90c = 0, c910 = 0, c914 = 0, c920 = 0, c924 = 0, c928 = 0, c92c = 0, c930 = 0, c938 = 0; };
    BudgetCounters& Counters() { return counters_; }
    void UpdateRoadsBudget();
    void UpdateMassTransitBudget();

    // ---- trip capacity (lot property 0x8a0b4100: 12 counters, max at [0..], used at [+4], arrived at [+8]) ----
    static std::uint32_t GetTripCapacity(const cISCPropertyHolder* holder, std::uint32_t index);
    static void SetTripCapacity(cISCPropertyHolder* holder, std::uint32_t index, std::uint32_t value);
    static std::uint32_t GetMaxTripCapacity(const cISCPropertyHolder* h, std::uint32_t i) { return GetTripCapacity(h, i); }
    static void SetMaxTripCapacity(cISCPropertyHolder* h, std::uint32_t i, std::uint32_t v) { SetTripCapacity(h, i, v); }
    static std::uint32_t GetUsedTripCapacity(const cISCPropertyHolder* h, std::uint32_t i) { return GetTripCapacity(h, i + 4); }
    static std::uint32_t GetTrafficArrived(const cISCPropertyHolder* h, std::uint32_t i) { return GetTripCapacity(h, i + 8); }

    // ---- neighbour connection geometry (city border cells are outside [0, size]) ----
    // Returns the border side (0 west, 1 north, 2 east, 3 south) and the nearest inner cell, or -1 when (x, z)
    // is not a border neighbour cell.
    int GetNeighborConnectionInnerCell(int x, int z, int& innerX, int& innerZ) const;
    void FindHighwayReturnTile(int& x, int& z, bool flag) const;
    int LastCellX() const { return lastX_; }    // +0x40, highest valid x index
    int LastCellZ() const { return lastZ_; }    // +0x44

    // ---- travel strategy percentages (4 wealth classes x 3 strategies, percent bytes) ----
    void GetTravelStrategyPercentages(unsigned wealth, std::vector<SC4Percentage>& out) const;
    const std::array<std::uint8_t, 12>& TravelStrategyBytes() const { return travelStrategies_; }

    // ---- accessors ----
    static constexpr std::uint32_t GetSimulatorType() { return kTrafficSimulatorType; }
    static constexpr std::uint32_t GetGZCLSID() { return kGZCLSID_cSC4TrafficSimulator; }
    float GetTripScale() const { return tunables_.tripScale; }
    float GetTripScaleForDisplay() const { return tunables_.tripScaleForDisplay; }
    float GetFreightScalingFactor() const { return tunables_.freightScalingFactor; }
    int GetFerryTerminalCount(unsigned ferryType) const { return ferryTerminalCount_[ferryType]; }
    void SetFerryTerminalCount(unsigned ferryType, int n) { ferryTerminalCount_[ferryType] = n; }
    int GetLastMonthlyIncome() const { return lastMonthlyIncome_; }
    void SetLastMonthlyIncome(int v) { lastMonthlyIncome_ = v; }
    // x/z packed into one value when the lot-insertion mode is 1, else 0
    std::uint64_t GetDesiredLotInsertionPoint() const { return insertionMode_ == 1 ? insertionPoint_ : 0; }
    void SetDesiredLotInsertion(int mode, std::uint64_t point) { insertionMode_ = mode; insertionPoint_ = point; }
    void InvalidateIntersectionData(int x0, int z0, int x1, int z1);
    bool IntersectionDirty() const { return intersectionDirty_; }
    void GetIntersectionDirtyRect(int& x0, int& z0, int& x1, int& z1) const { x0 = dirtyX0_; z0 = dirtyZ0_; x1 = dirtyX1_; z1 = dirtyZ1_; }

    static const std::array<std::uint8_t, 9> kTravelModeToTransitNetwork;

private:
    NetworkInfo* FindNetworkInfo(int networkType, int x, int z) const;
    std::vector<NetworkInfo>* Cell(int x, int z) const;

    ITrafficHost* host_;
    cRZRandom* random_;
    TrafficTunables tunables_;
    cSC4ResponseCurve congestionToInverseSpeed_;
    cSC4ResponseCurve curveF78_, fundingCurve_, curveFB8_, curveFD8_;
    std::array<bool, 9> travelTypeAffectedByTraffic_{};
    int lastX_ = -1, lastZ_ = -1;
    mutable std::vector<std::vector<std::vector<NetworkInfo>>> grid_;
    std::unordered_map<std::uint32_t, TrafficEdgeDensity> edgeDensities_;
    std::unordered_map<std::uint32_t, TransitSwitch> transitSwitches_;
    TrafficEdgeDensity emptyEdgeDensity_{};

    CellSet healthyRoads_, damagedRoads_, healthyRail_, damagedRail_;
    std::uint32_t roadPotholeTotal_ = 0, railPotholeTotal_ = 0;
    int roadPotholeCount_ = 0, railPotholeCount_ = 0;
    float roadFundingAccum_ = 0, railFundingAccum_ = 0;
    short statRoadPotholes_ = 0;

    StrikeState strikeState_ = kStrikeNone;
    int strikeDays_ = 0;
    std::int64_t strikeEligible_ = 0;
    bool strikeActive_ = false;
    float strikeChance_ = 0;
    BudgetCounters counters_;

    std::array<std::uint8_t, 12> travelStrategies_{};
    std::array<int, 3> ferryTerminalCount_{};
    int lastMonthlyIncome_ = 0;
    int insertionMode_ = 0;
    std::uint64_t insertionPoint_ = 0;
    bool intersectionDirty_ = false;
    int dirtyX0_ = 0, dirtyZ0_ = 0, dirtyX1_ = 0, dirtyZ1_ = 0;
};

}  // namespace sc4
