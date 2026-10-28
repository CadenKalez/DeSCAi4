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
#include "gz/cIGZMessage2.h"
#include "gz/cIGZSerializable.h"
#include "rz/cRZRandom.h"
#include "sc4/SCPropertyHelp.h"
#include "sc4/cSC4ResponseCurve.h"

class cIGZIStream;
class cIGZOStream;

namespace sc4 {

constexpr std::uint32_t kGZCLSID_cSC4PowerSimulator = 0x0990c005;
constexpr std::uint32_t kGZIID_cISC4PowerSimulator = 0xe9e7bff5;
constexpr std::uint32_t kPowerGridIid = 0x223e2b67;        // int16 sim grid (the power line grid)
constexpr std::uint32_t kSupplyGridIid = 0xc9bd939a;       // uint8 sim grid (the supply grid)
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

// The primary view the city holds for this simulator (cISC4PowerSimulator, iid 0xe9e7bff5). The port only
// answers its identity through it; the query surface lives on the concrete class.
class cISC4PowerSimulator : public cIGZUnknown {};

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
    // ---- stream seams: nested serializables the simulator reaches through the host (kept out of the sim's
    // own translation unit so it never depends on the DB-segment layer). Wired to SC4Write/ReadGZSerializable.
    virtual void WritePowerGrid(cIGZOStream&) {}                          // power line grid, iid 0x223e2b67
    virtual bool ReadPowerGrid(cIGZIStream&) { return false; }
    virtual void WriteSupplyGrid(cIGZOStream&) {}                         // supply grid, iid 0xc9bd939a
    virtual bool ReadSupplyGrid(cIGZIStream&) { return false; }
    virtual void WritePlant(cIGZOStream&, IPowerOccupant*) {}             // SC4WriteGZSerializable(stream, occupant)
    virtual bool ReadPlant(cIGZIStream&, IPowerOccupant**) { return false; }
    virtual void WritePole(cIGZOStream&, IPowerPole*) {}                  // SC4WriteGZSerializable(stream, pole)
    virtual bool ReadPole(cIGZIStream&, IPowerPole**) { return false; }
};

struct PowerPlantEntry { IPowerOccupant* occupant = nullptr; int state = 0; };   // list node: +0x10 state, +0x18 occupant

class cSC4PowerSimulator : public cISC4PowerSimulator,
                           public cIGZMessageTarget2,
                           public cIGZSerializable {
public:
    cSC4PowerSimulator(IPowerHost* host, cRZRandom* random);
    virtual ~cSC4PowerSimulator();

    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return ++refCount_; }
    std::uint32_t Release() override;
    std::uint32_t GetGZCLSID() const override { return kGZCLSID_cSC4PowerSimulator; }
    bool Write(cIGZOStream& stream) const override;
    bool Read(cIGZIStream& stream) override;
    bool DoMessage(cIGZMessage2* message) override;

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
    int plantCountTypeExtra = 0;   // 10th plant-count slot (+0x37c)

    // cISC4PowerSimulator statistics getters (each returns one counter of the object)
    std::uint32_t GetMonthlyRequirement() const { return std::uint32_t(monthlyRequirementA4); }       // @ 0x10021291a (+0xa4)
    std::uint32_t GetMonthlyProduction() const { return std::uint32_t(monthlyProduction80); }         // @ 0x100212926 (+0x80)
    std::uint32_t GetMonthlyImport() const { return std::uint32_t(monthlyImport88); }                 // @ 0x100212932 (+0x88)
    std::uint32_t GetMonthlyExport() const { return std::uint32_t(monthlyExport8c); }                 // @ 0x10021293e (+0x8c)
    std::uint32_t GetMonthlyDemand() const { return std::uint32_t(monthlyDemand90); }                 // @ 0x10021294a (+0x90)
    std::uint32_t GetMonthlyConsumption() const { return std::uint32_t(monthlyConsumption84); }       // @ 0x100212956 (+0x84)
    std::uint32_t GetUnpoweredConsumerCount() const { return std::uint32_t(unpoweredConsumersA0); }   // @ 0x1002129b4 (+0xa0)
    std::uint32_t GetPoweredConsumerCount() const { return std::uint32_t(poweredConsumers9c); }       // @ 0x1002129c0 (+0x9c)
    std::uint32_t GetPowerPlantCount() const { return std::uint32_t(plantCount); }                    // @ 0x100214e26 (+0x354)
    // @ 0x100214e32: the original indexes ten counters at +0x358; slots 6..8 are not modelled (they read 0 here)
    std::uint32_t GetPowerPlantCount(std::uint32_t type) const {
        if (type < plantCountByType.size()) return std::uint32_t(plantCountByType[type]);
        return type == 9 ? std::uint32_t(plantCountTypeExtra) : 0;
    }
    std::uint32_t GetPowerPoleCount() const { return poleCount_; }                                    // @ 0x100214e42 (+0x31c)
    std::uint32_t GetOverworkedPowerPlantCount() const { return std::uint32_t(overworkedPlants); }    // @ 0x100214e4e (+0x380)
    std::uint32_t GetAgedPowerPlantCount() const { return std::uint32_t(agedPlants); }                // @ 0x100214e5a (+0x384)
    std::uint32_t GetDistressedPowerPlantCount() const { return std::uint32_t(distressedPlants); }    // @ 0x100214e66 (+0x388)

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
    std::uint32_t refCount_ = 0;                 // +0x1c (COM identity)
    int neighborhoodRadius_ = 2;
    std::uint32_t* visitedBits_ = nullptr;     // +0x328
    std::uint32_t* wireBits_ = nullptr;        // +0x330
    std::uint32_t* gridVisitedBits_ = nullptr; // +0x340
    std::uint32_t* poweredBits_ = nullptr;     // +0x348
    std::uint32_t bitWords_ = 0;               // +0x338
    std::vector<std::vector<std::int16_t>> consumptionMap_;   // +0xb8 (the monthly per-cell consumption map)
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
    // Stream-only fields (part of the binary record; the passes that maintain them are not recovered).
    std::int32_t monthlyValue98_ = 0;           // +0x98 monthly counter (unrecovered producer)
    std::int64_t monthlyCostA8_ = 0;            // +0xa8 monthly power-generation cost total
    std::uint8_t flag350_ = 0;                  // +0x350
    std::uint8_t flag3a8_ = 0;                  // +0x3a8 (cleared by Read after loading)
    std::int32_t value3ac_ = 0;                 // +0x3ac
    std::uint32_t value3c4_ = 0;                // +0x3c4
};

}  // namespace sc4
