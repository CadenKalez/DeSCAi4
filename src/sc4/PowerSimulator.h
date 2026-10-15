// cSC4PowerSimulator (class id 0x0990c005) — the power grid: which cells are wired and powered, plant output,
// plant ageing inputs, pole funding and the monthly power statistics. The original object is ~0x4b0 bytes.
// Recovered here: cell/position conversion, the neighbourhood rectangles, the wire/power bit maps and their
// refresh (UpdateWireBits), the monthly statistics ring, pole funding, plant output / efficiency / condition,
// plant search, tunables and the query surface. UpdatePowerGrid (the flood fill and neighbour trading),
// UpdatePoweredLots, AgePowerPlant, IsNeighborConnectedToCurrentGrid and the streams are not.
//
// Layout used: +0x28 city, +0x30/+0x38 budget / department, +0x50 power line grid, +0xb0 supply grid, +0xb8
// consumption map, +0x68 month index, +0x6c..+0x9c monthly counters, +0xd0 12 x 10-int monthly power figures,
// +0x2b0 10-int annual totals, +0x2d8 plant list, +0x2f0 pole vector, +0x310 ordinance factor, +0x314
// neighbourhood radius, +0x31c pole count, +0x320 enabled poles, +0x324 pole funding accumulator,
// +0x328 visited / +0x330 wire / +0x340 grid-visited / +0x348 powered bit maps, +0x354.. plant counts,
// +0x3b0..+0x3c0 dirty flags / rect, +0x3f0.. plant tunables and curves.
#pragma once
#include <array>
#include <cstdint>
#include <list>
#include <vector>

#include "gz/Bit1.h"
#include "rz/cRZRandom.h"
#include "sc4/SCPropertyHelp.h"
#include "sc4/cSC4ResponseCurve.h"

namespace sc4 {

constexpr std::uint32_t kGZCLSID_cSC4PowerSimulator = 0x0990c005;
constexpr std::uint32_t kPowerPlantMaxProduced = 0x27812852;      // exemplar property: max output
constexpr std::uint32_t kPowerPlantPowerType = 0x27812853;
constexpr std::uint32_t kPowerPlantCurrentProduced = 0x08d9763d;
constexpr std::uint32_t kPowerPlantWear = 0xc910b4cf;              // 0..1+ age / wear
constexpr std::uint32_t kPowerPlantBudgetProperty = 0xca58e540;
constexpr std::uint32_t kPowerPlantConditionCurve = 0x8a1e07ee;

// occupant types that block / feed the wire map
constexpr std::uint32_t kPowerOccupantBuilding = 0x278128a0, kPowerOccupantPole = 0x2890d4de,
                        kPowerOccupantNetwork = 0x0990b2ea, kPowerOccupantPylon = 0xc772bf98;

struct PowerRect { int left = 0, top = 0, right = 0, bottom = 0; };   // inclusive

class IPowerOccupant {
public:
    virtual ~IPowerOccupant() = default;
    virtual std::uint32_t OccupantType() = 0;                      // vslot 0x38
    virtual cISCPropertyHolder* PropertyHolder() = 0;              // vslot 0x30
    virtual void GetBounds(PowerRect& out) = 0;                    // vslot 0x58
    // The single cell of a pole / network / pylon occupant; false when that interface is missing
    // (a pylon only counts when its flags contain 0x40000).
    virtual bool GetAnchorCell(int& x, int& z) = 0;
};

class IPowerPole {
public:
    virtual ~IPowerPole() = default;
    virtual bool IsEnabled() = 0;                  // vslot 0x58
    virtual void SetEnabled(bool enabled) = 0;     // vslot 0x60
};

// What the budget simulator tracks for a power plant.
struct PowerPlantBudgetEntry {
    bool tracked = false;
    float funding = 0.0f;           // fraction of full funding
    bool hasExplicitFunding = false;
    std::int64_t monthlyCost = 0;
};

class IPowerHost {
public:
    virtual ~IPowerHost() = default;
    virtual int CityCellsX() = 0;                  // vslot 0x2e8
    virtual int CityCellsZ() = 0;                  // vslot 0x2f0
    virtual float CellSizeX() = 0;                 // vslot 0x2d8
    virtual float CellSizeZ() = 0;                 // vslot 0x2e0
    virtual void OccupantsInRect(const PowerRect& rect, std::vector<IPowerOccupant*>& out) = 0;
    virtual bool IsPowerLineCell(int x, int z) = 0;                       // power line grid (+0x50)
    virtual void SetSupplyValue(int x, int z, std::uint8_t value) = 0;    // supply grid (+0xb0)
    virtual float OrdinancePowerFactor() = 0;                             // ordinance 0x0911e117
    virtual PowerPlantBudgetEntry GetPlantBudgetEntry(IPowerOccupant* plant) = 0;
    virtual float DepartmentFundingFraction() = 0;                        // department +0x50
    virtual int CityDay() = 0;
};

struct PowerPlantEntry { IPowerOccupant* occupant = nullptr; int state = 0; };   // list node: +0x10 state, +0x18 occupant

class cSC4PowerSimulator {
public:
    cSC4PowerSimulator(IPowerHost* host, cRZRandom* random);
    ~cSC4PowerSimulator();

    // Loads defaults and then the exemplar {0x6534284a, 0xe7e2c2db, 0xc911e35b}; null = defaults.
    void ReadSimulatorTunableValues(const cISCPropertyHolder* exemplar);

    // ---- coordinates ----
    void PositionToCell(float x, float z, int& cellX, int& cellZ) const;
    void CellToPosition(int cellX, int cellZ, float& x, float& z) const;
    static void CellToStandardCityCell(int x, int z, int& outX, int& outZ) { outX = x; outZ = z; }
    static void StandardCityCellToCell(int x, int z, int& outX, int& outZ) { outX = x; outZ = z; }
    int NeighborhoodRadius() const { return neighborhoodRadius_; }
    void ExpandRectToIncludeNeighborhood(PowerRect& rect) const;
    void GetNeighborhoodOfPoint(int x, int z, PowerRect& out) const;

    // ---- bit maps ----
    void InitMarkAndWireBits();        // allocates the four maps (cells^2 bits each)
    void ShutdownMarkAndWireBits();
    bool IsPowered(int x, int z) const;
    bool IsWired(int x, int z) const;
    static bool SetsWireBits(IPowerOccupant* occupant);
    void ResetWireBits(const PowerRect& rect);
    void UpdateWireBits(const PowerRect& rect, IPowerOccupant* ignore);
    bool CellChanged(unsigned x0, unsigned z0, unsigned x1, unsigned z1);
    bool DirtyFlag() const { return dirty_; }          // +0x3b0
    bool SecondDirtyFlag() const { return dirtySecond_; }   // +0x3b1
    bool HasPendingRect() const { return pending_; }
    const PowerRect& PendingRect() const { return pendingRect_; }
    std::uint32_t* PoweredBits() { return poweredBits_; }
    std::uint32_t* WireBits() { return wireBits_; }

    // ---- plants ----
    std::list<PowerPlantEntry>& Plants() { return plants_; }
    std::uint32_t GetCurrentPowerProduced(IPowerOccupant* plant);
    float GetCurrentPlantConditionPercent(IPowerOccupant* plant);
    float GetCurrentPlantEfficiencyPercent(IPowerOccupant* plant);
    bool GetPowerPlantFundingPercentage(IPowerOccupant* plant, SC4Percentage& out);
    bool GetMonthlyPowerGenerationCost(IPowerOccupant* plant, std::int64_t& cost);
    // Best (least funded, tracked) and most worn plant: plant with the lowest funding below 2.0 and the plant
    // with the highest wear ratio; optional outputs. wearPercent receives wear * 100 of the worn plant.
    void FindPowerPlants(IPowerOccupant** leastFunded, SC4Percentage* funding, IPowerOccupant** mostWorn, int* wearPercent);
    void MarkPowerPlantsAsVisited(std::uint32_t used, std::uint32_t capacity);
    bool GetOccupantPowerProduced(IPowerOccupant* plant, std::uint32_t& produced, std::uint32_t& currentProduced);
    static bool GetOccupantMaxPowerProduced(IPowerOccupant* plant, std::uint32_t& out);
    void GetPowerPlants(std::list<IPowerOccupant*>& out) const;

    // ---- poles and funding ----
    std::vector<IPowerPole*>& Poles() { return poles_; }
    void SetPoleCount(std::uint32_t n) { poleCount_ = n; }     // +0x31c total poles
    void ApplyPoleFunding(const SC4Percentage& funding);
    int EnabledPoleCount() const { return enabledPoles_; }
    bool RecomputeInfrastructureCosts(const std::vector<std::int64_t>& poleCosts);
    std::uint32_t InfrastructureCost() const { return infrastructureCost_; }

    // ---- monthly statistics ----
    void DoMonthlySimulation();
    static constexpr int kMonthlySlots = 12, kPowerTypes = 10;
    std::array<std::array<int, kPowerTypes>, kMonthlySlots>& MonthlyPower() { return monthlyPower_; }   // +0xd0
    std::array<int, kPowerTypes>& AnnualPower() { return annualPower_; }                                // +0x2b0
    int CurrentMonth() const { return month_; }
    int GetAnnualConsumption() const;
    int GetAnnualConsumptionByPowerType(int type) const { return annualPower_[std::size_t(type)]; }
    // counters
    int usedCapacity6c = 0, capacity70 = 0, monthlyProduction80 = 0, monthlyConsumption84 = 0, monthlyImport88 = 0,
        monthlyExport8c = 0, monthlyDemand90 = 0;
    float ratio94 = 0.0f;
    int poweredConsumers9c = 0, unpoweredConsumersA0 = 0, monthlyRequirementA4 = 0;
    int plantCount = 0, overworkedPlants = 0, agedPlants = 0, distressedPlants = 0;
    std::array<int, 6> plantCountByType{};

    // ---- tunables ----
    float ordinanceFactor = 1.0f;           // +0x310
    float value318 = 1.1f;                  // +0x318
    float conditionSafe = 0.6f;             // +0x3f0
    float value3f4 = 1.2f;                  // +0x3f4
    SC4Percentage strikeEfficiency{0.5f};   // +0x3f8
    int value3fc = 10;                      // +0x3fc
    float value400 = 0.9f;                  // +0x400
    cSC4ResponseCurve fundingEfficiencyCurve, curve428, curve448, curve468, poleFundingCurve;   // +0x408.. +0x488
    const SC4Percentage& GetStrikeEfficiencyPercent() const { return strikeEfficiency; }
    static constexpr bool IsOnStrike() { return false; }
    static constexpr float GetChanceOfStrike() { return 0.0f; }

private:
    IPowerHost* host_;
    cRZRandom* random_;
    int neighborhoodRadius_ = 2;
    std::uint32_t* visitedBits_ = nullptr;     // +0x328
    std::uint32_t* wireBits_ = nullptr;        // +0x330
    std::uint32_t* gridVisitedBits_ = nullptr; // +0x340
    std::uint32_t* poweredBits_ = nullptr;     // +0x348
    std::uint32_t bitWords_ = 0;
    bool dirty_ = false, dirtySecond_ = false, pending_ = false;
    PowerRect pendingRect_{};
    std::list<PowerPlantEntry> plants_;
    std::vector<IPowerPole*> poles_;
    std::uint32_t poleCount_ = 0;
    std::uint32_t enabledPoles_ = 0;
    float poleFundingAccum_ = 0.0f;
    std::uint32_t infrastructureCost_ = 0;
    int month_ = 0;
    std::array<std::array<int, kPowerTypes>, kMonthlySlots> monthlyPower_{};
    std::array<int, kPowerTypes> annualPower_{};
};

}  // namespace sc4
