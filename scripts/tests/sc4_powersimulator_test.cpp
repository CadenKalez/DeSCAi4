#include <cmath>
#include <cstdio>
#include <map>

#include "sc4/PowerSimulator.h"
#include "sc4/SCProperty.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

using namespace sc4;

namespace {

struct Occ : IPowerOccupant {
    cSCPropertyHolder props;
    std::uint32_t type = kPowerOccupantBuilding;
    PowerRect bounds{};
    bool hasAnchor = false;
    int ax = 0, az = 0;
    std::uint32_t OccupantType() override { return type; }
    cISCPropertyHolder* PropertyHolder() override { return &props; }
    void GetBounds(PowerRect& out) override { out = bounds; }
    bool GetAnchorCell(int& x, int& z) override { x = ax; z = az; return hasAnchor; }
};

struct Pole : IPowerPole {
    bool enabled = false;
    bool IsEnabled() override { return enabled; }
    void SetEnabled(bool e) override { enabled = e; }
};

struct Host : IPowerHost {
    int cx = 64, cz = 64;
    std::vector<IPowerOccupant*> all;
    std::map<std::pair<int, int>, bool> lines;
    std::map<std::pair<int, int>, int> supply;
    float ordinance = 1.0f;
    std::map<IPowerOccupant*, PowerPlantBudgetEntry> budget;
    float departmentFunding = 0.5f;
    int CityCellsX() override { return cx; }
    int CityCellsZ() override { return cz; }
    float CellSizeX() override { return 16.0f; }
    float CellSizeZ() override { return 8.0f; }
    void OccupantsInRect(const PowerRect&, std::vector<IPowerOccupant*>& out) override { out = all; }
    bool IsPowerLineCell(int x, int z) override { return lines.count({x, z}) != 0; }
    void SetSupplyValue(int x, int z, std::uint8_t v) override { supply[{x, z}] = v; }
    float OrdinancePowerFactor() override { return ordinance; }
    PowerPlantBudgetEntry GetPlantBudgetEntry(IPowerOccupant* p) override { auto it = budget.find(p); return it == budget.end() ? PowerPlantBudgetEntry{} : it->second; }
    float DepartmentFundingFraction() override { return departmentFunding; }
    int CityDay() override { return 1; }
};

}  // namespace

int main() {
    Host host;
    cRZRandom random(5);
    cSC4PowerSimulator sim(&host, &random);
    CHECK(kGZCLSID_cSC4PowerSimulator == 0x0990c005);
    sim.ReadSimulatorTunableValues(nullptr);
    CHECK(sim.NeighborhoodRadius() == 2 && sim.value318 == 1.1f && sim.conditionSafe == 0.6f && sim.value3f4 == 1.2f);
    CHECK(sim.strikeEfficiency.value == 0.5f && sim.value3fc == 10 && sim.value400 == 0.9f && sim.ordinanceFactor == 1.0f);
    CHECK(sim.fundingEfficiencyCurve.GetOutputValue(120.0f) == 110.0f && sim.poleFundingCurve.GetOutputValue(0.0f) == -0.02f);
    CHECK(!sim.IsOnStrike() && sim.GetChanceOfStrike() == 0.0f);

    // ---- tunables from an exemplar
    {
        cSCPropertyHolder ex;
        cSCPropertyHelp::SetPropertyValue(&ex, 0xa911e2f1, std::uint32_t(3));
        cSCPropertyHelp::SetPropertyValue(&ex, 0xe911e2fc, 250.0f);
        cSCPropertyHelp::SetPropertyValue(&ex, 0x29121e8b, 80.0f);
        cSCPropertyHelp::SetPropertyValue(&ex, 0x2912202e, std::int32_t(4));
        cSCPropertyHelp::SetPropertyValue(&ex, 0xaa713782, std::vector<float>{0.0f, 1.0f, 100.0f, 2.0f});
        host.ordinance = 0.75f;
        sim.ReadSimulatorTunableValues(&ex);
        CHECK(sim.NeighborhoodRadius() == 3 && sim.value318 == 2.5f && std::fabs(sim.conditionSafe - 0.8f) < 1e-6f && sim.value3fc == 4);
        CHECK(sim.poleFundingCurve.GetOutputValue(0.0f) == 1.0f && sim.ordinanceFactor == 0.75f);
        host.ordinance = 1.0f;
        sim.ReadSimulatorTunableValues(nullptr);
        CHECK(sim.NeighborhoodRadius() == 2 && sim.ordinanceFactor == 1.0f);
    }

    // ---- coordinates and neighbourhoods
    int cx, cz;
    sim.PositionToCell(40.0f, 20.0f, cx, cz);
    CHECK(cx == 2 && cz == 2);
    float px, pz;
    sim.CellToPosition(3, 5, px, pz);
    CHECK(px == 48.0f && pz == 40.0f);
    int sx, sz;
    sim.CellToStandardCityCell(7, 9, sx, sz);
    CHECK(sx == 7 && sz == 9);
    PowerRect r;
    sim.GetNeighborhoodOfPoint(10, 10, r);
    CHECK(r.left == 8 && r.top == 8 && r.right == 12 && r.bottom == 12);
    sim.GetNeighborhoodOfPoint(1, 63, r);
    CHECK(r.left == 0 && r.top == 61 && r.right == 3 && r.bottom == 63);
    sim.GetNeighborhoodOfPoint(2, 62, r);
    CHECK(r.left == 0 && r.right == 4 && r.bottom == 63);
    r = {5, 3, 20, 62};
    sim.ExpandRectToIncludeNeighborhood(r);
    CHECK(r.left == 3 && r.top == 1 && r.right == 22 && r.bottom == 63);

    // ---- bit maps
    sim.InitMarkAndWireBits();
    CHECK(!sim.IsWired(5, 5) && !sim.IsPowered(5, 5));
    Occ building;
    building.bounds = {10, 10, 11, 11};
    Occ pole;
    pole.type = kPowerOccupantPole; pole.hasAnchor = true; pole.ax = 30; pole.az = 30;
    Occ pylon;
    pylon.type = kPowerOccupantPylon; pylon.hasAnchor = true; pylon.ax = 40; pylon.az = 41;
    Occ pylonOff;
    pylonOff.type = kPowerOccupantPylon; pylonOff.hasAnchor = false;
    Occ other;
    other.type = 0x12345678; other.bounds = {50, 50, 50, 50};
    Occ ignored;
    ignored.bounds = {20, 20, 20, 20};
    CHECK(cSC4PowerSimulator::SetsWireBits(&building) && cSC4PowerSimulator::SetsWireBits(&pole) && cSC4PowerSimulator::SetsWireBits(&pylon));
    CHECK(!cSC4PowerSimulator::SetsWireBits(&other));
    host.all = {&building, &pole, &pylon, &pylonOff, &other, &ignored};
    host.lines[{5, 5}] = true;
    PowerRect area{0, 0, 63, 63};
    sim.UpdateWireBits(area, &ignored);
    CHECK(sim.IsWired(10, 10) && sim.IsWired(8, 8) && sim.IsWired(13, 13) && !sim.IsWired(14, 14) && !sim.IsWired(7, 9));   // building 10..11 +-2
    CHECK(sim.IsWired(30, 30) && sim.IsWired(32, 32) && !sim.IsWired(33, 33));       // pole point +-2
    CHECK(sim.IsWired(40, 41) && !sim.IsWired(41, 41) && !sim.IsWired(40, 42));      // pylon: single cell, not expanded
    CHECK(!sim.IsWired(50, 50) && !sim.IsWired(20, 20));                              // non wire types / ignored occupant
    CHECK(sim.IsWired(5, 5) && sim.IsWired(3, 3) && sim.IsWired(7, 7) && !sim.IsWired(8, 5));   // power line cell: radius-2 square
    CHECK((host.supply[{10, 10}] == 1) && (host.supply[{0, 0}] == 0));
    // powered bits show up in the supply value
    Bit1_PixelSet(sim.PoweredBits(), 64, 10, 10);   // slots (size, column, row), see TestBit in PowerSimulator.cpp
    sim.UpdateWireBits({9, 9, 11, 11}, nullptr);
    CHECK(sim.IsPowered(10, 10) && !sim.IsPowered(9, 9));
    sim.ResetWireBits({0, 0, 63, 63});
    CHECK(!sim.IsWired(10, 10));
    // cell changes grow the pending rectangle
    host.lines.clear();
    CHECK(sim.CellChanged(10, 10, 11, 11));
    CHECK(sim.HasPendingRect() && sim.DirtyFlag() && sim.PendingRect().left == 8 && sim.PendingRect().right == 13);
    sim.CellChanged(30, 5, 31, 6);
    CHECK(sim.PendingRect().left == 8 && sim.PendingRect().top == 3 && sim.PendingRect().right == 33 && sim.PendingRect().bottom == 13);
    CHECK(!sim.IsWired(10, 10));      // not a power line cell: no wiring
    host.lines[{40, 40}] = true;
    sim.CellChanged(40, 40, 40, 40);
    CHECK(sim.IsWired(38, 38) && sim.IsWired(42, 42));
    sim.ShutdownMarkAndWireBits();

    // ---- plants
    Occ plantA, plantB;
    cSCPropertyHelp::SetPropertyValue(&plantA.props, kPowerPlantMaxProduced, std::uint32_t(1000));
    cSCPropertyHelp::SetPropertyValue(&plantB.props, kPowerPlantMaxProduced, std::uint32_t(500));
    cSCPropertyHelp::SetPropertyValue(&plantA.props, kPowerPlantPowerType, std::int32_t(2));
    cSCPropertyHelp::SetPropertyValue(&plantB.props, kPowerPlantPowerType, std::int32_t(3));
    CHECK(sim.GetCurrentPowerProduced(nullptr) == 0);
    host.budget[&plantA] = {true, 1.0f, true, 1200};
    host.budget[&plantB] = {true, 0.5f, true, 700};
    // condition curve of a plant: wear -> percent
    cSCPropertyHelp::SetPropertyValue(&plantA.props, kPowerPlantConditionCurve, std::vector<float>{0.0f, 1.0f, 1.0f, 0.5f});
    cSCPropertyHelp::SetPropertyValue(&plantA.props, kPowerPlantWear, 0.0f);
    cSCPropertyHelp::SetPropertyValue(&plantB.props, kPowerPlantConditionCurve, std::vector<float>{0.0f, 1.0f, 1.0f, 0.5f});
    cSCPropertyHelp::SetPropertyValue(&plantB.props, kPowerPlantWear, 1.0f);
    CHECK(sim.GetCurrentPlantConditionPercent(&plantA) == 1.0f && sim.GetCurrentPlantConditionPercent(&plantB) == 0.5f);
    CHECK(sim.GetCurrentPlantConditionPercent(&building) == 0.0f && sim.GetCurrentPlantConditionPercent(nullptr) == 0.0f);
    // efficiency = funded curve (0..110%) * condition; default curve at 100% funding -> 1.0
    CHECK(std::fabs(sim.GetCurrentPlantEfficiencyPercent(&plantA) - 1.0f) < 1e-6f);
    CHECK(std::fabs(sim.GetCurrentPlantEfficiencyPercent(&plantB) - 0.5f * 0.5f) < 1e-6f);     // 50% funding -> 0.5, condition 0.5
    CHECK(sim.GetCurrentPowerProduced(&plantA) == 1000 && sim.GetCurrentPowerProduced(&plantB) == 125);
    Occ untracked;
    cSCPropertyHelp::SetPropertyValue(&untracked.props, kPowerPlantMaxProduced, std::uint32_t(100));
    CHECK(sim.GetCurrentPowerProduced(&untracked) == 0);            // no condition curve -> zero
    SC4Percentage fund;
    CHECK(sim.GetPowerPlantFundingPercentage(&plantB, fund) && fund.value == 0.5f && !sim.GetPowerPlantFundingPercentage(&untracked, fund));
    std::int64_t cost = 0;
    CHECK(sim.GetMonthlyPowerGenerationCost(&plantB, cost) && cost == 700 && !sim.GetMonthlyPowerGenerationCost(&untracked, cost));
    std::uint32_t produced = 0, current = 99;
    CHECK(sim.GetOccupantPowerProduced(&plantB, produced, current) && produced == 125 && current == 0);
    cSCPropertyHelp::SetPropertyValue(&plantB.props, kPowerPlantCurrentProduced, std::int32_t(77));
    CHECK(sim.GetOccupantPowerProduced(&plantB, produced, current) && current == 77);
    CHECK(!sim.GetOccupantPowerProduced(&building, produced, current));
    std::uint32_t maxOut = 0;
    CHECK(cSC4PowerSimulator::GetOccupantMaxPowerProduced(&plantA, maxOut) && maxOut == 1000);
    // plant list
    sim.Plants() = {{&plantA, 0}, {&plantB, 0}};
    {
        std::list<IPowerOccupant*> out;
        sim.GetPowerPlants(out);
        CHECK(out.size() == 2 && out.front() == &plantA);
    }
    cSCPropertyHelp::SetPropertyValue(&plantA.props, kPowerPlantWear, 0.4f);
    cSCPropertyHelp::SetPropertyValue(&plantB.props, kPowerPlantWear, 0.8f);
    IPowerOccupant *lowest = nullptr, *worn = nullptr;
    SC4Percentage lowestFunding;
    int wearPercent = 0;
    sim.FindPowerPlants(&lowest, &lowestFunding, &worn, &wearPercent);
    CHECK(lowest == &plantB && lowestFunding.value == 0.5f);
    CHECK(worn == &plantB && wearPercent == int(0.8f / 99.0f * 100.0f));
    cSCPropertyHelp::SetPropertyValue(&plantA.props, 0xea1cf221, 0.5f);       // plant A wears out at 0.5: ratio 0.8
    cSCPropertyHelp::SetPropertyValue(&plantA.props, 0xea1cf222, 0.5f);
    sim.FindPowerPlants(nullptr, nullptr, &worn, &wearPercent);
    CHECK(worn == &plantA && wearPercent == int(0.8f * 100.0f));
    // visiting plants: state 1 plants record their share of the produced power
    sim.usedCapacity6c = 3;
    sim.Plants() = {{&plantA, 1}, {&plantB, 2}};
    sim.MarkPowerPlantsAsVisited(500, 1000);
    CHECK(sim.Plants().front().state == 2 && sim.Plants().back().state == 2);
    std::int32_t recorded = 0;
    cSCPropertyHelp::GetPropertyValue(&plantA.props, kPowerPlantCurrentProduced, recorded);
    CHECK(recorded == 400);                                                   // 1000 * condition 0.8 * 0.5
    CHECK(sim.MonthlyPower()[0][2] == 400 && sim.MonthlyPower()[0][3] == 0);
    sim.Plants() = {{&plantA, 1}};
    sim.MarkPowerPlantsAsVisited(500, 0);                                      // capacity 0 divides by 1
    cSCPropertyHelp::GetPropertyValue(&plantA.props, kPowerPlantCurrentProduced, recorded);
    CHECK(recorded == 800 * 500);
    sim.usedCapacity6c = 0;
    sim.Plants() = {{&plantA, 1}};
    sim.MarkPowerPlantsAsVisited(500, 1000);
    cSCPropertyHelp::GetPropertyValue(&plantA.props, kPowerPlantCurrentProduced, recorded);
    CHECK(recorded == 0);

    // ---- poles
    std::vector<Pole> poles(10);
    for (auto& p : poles) sim.Poles().push_back(&p);
    sim.SetPoleCount(10);
    auto enabledCount = [&] { int n = 0; for (auto& p : poles) n += p.enabled; return n; };
    sim.poleFundingCurve.SetResponseCurveData(std::vector<SC4Point>{{0.0f, -0.5f}, {100.0f, 0.25f}});   // 0% funding: accumulate 0.5
    sim.ApplyPoleFunding(SC4Percentage{0.0f});
    CHECK(sim.EnabledPoleCount() == 5 && enabledCount() == 5);
    sim.ApplyPoleFunding(SC4Percentage{1.0f});          // accumulator 0.5 - 0.25 -> 0.25 -> 2 poles
    CHECK(sim.EnabledPoleCount() == 2 && enabledCount() == 2);
    sim.ApplyPoleFunding(SC4Percentage{0.0f});
    sim.ApplyPoleFunding(SC4Percentage{0.0f});          // clamps at 1.0 -> all poles
    CHECK(sim.EnabledPoleCount() == 10 && enabledCount() == 10);
    for (int i = 0; i < 6; ++i) sim.ApplyPoleFunding(SC4Percentage{1.0f});   // clamps at 0
    CHECK(sim.EnabledPoleCount() == 0 && enabledCount() == 0);
    CHECK(sim.RecomputeInfrastructureCosts({100, 250}) && sim.InfrastructureCost() == 350);
    CHECK(!sim.RecomputeInfrastructureCosts({350}) && sim.RecomputeInfrastructureCosts({}) && sim.InfrastructureCost() == 0);

    // ---- monthly statistics ring
    {
        cSC4PowerSimulator m(&host, &random);
        m.AnnualPower()[0] = 100;
        m.AnnualPower()[3] = 40;
        m.MonthlyPower()[1][0] = 30;
        m.MonthlyPower()[1][3] = 15;
        m.MonthlyPower()[0][3] = 5;
        m.usedCapacity6c = 4;
        m.capacity70 = 10;
        m.SetPoleCount(0);
        host.departmentFunding = 1.0f;
        m.DoMonthlySimulation();
        CHECK(m.CurrentMonth() == 1);
        CHECK(m.AnnualPower()[0] == 70 && m.AnnualPower()[3] == 25);       // the month leaving the ring is subtracted
        CHECK(m.MonthlyPower()[1][0] == 0 && m.MonthlyPower()[1][3] == 0 && m.MonthlyPower()[0][3] == 5);
        CHECK(m.ratio94 == float((4 - 10) / 4));                            // integer division, as in the original
        CHECK(m.DirtyFlag() && m.SecondDirtyFlag());
        m.usedCapacity6c = 12;
        for (int i = 0; i < 12; ++i) m.DoMonthlySimulation();
        CHECK(m.CurrentMonth() == 1 && m.ratio94 == 0.0f);
        m.AnnualPower()[2] = 9;
        CHECK(m.GetAnnualConsumptionByPowerType(2) == 9);
    }

    {   // statistics getters return the counters at their original offsets
        cSC4PowerSimulator g(&host, &random);
        g.monthlyProduction80 = 1; g.monthlyConsumption84 = 2; g.monthlyImport88 = 3; g.monthlyExport8c = 4;
        g.monthlyDemand90 = 5; g.poweredConsumers9c = 6; g.unpoweredConsumersA0 = 7; g.monthlyRequirementA4 = 8;
        g.plantCount = 9; g.overworkedPlants = 10; g.agedPlants = 11; g.distressedPlants = 12;
        g.plantCountByType[2] = 13; g.plantCountTypeExtra = 14; g.SetPoleCount(15);
        CHECK(g.GetMonthlyProduction() == 1 && g.GetMonthlyConsumption() == 2 && g.GetMonthlyImport() == 3 &&
              g.GetMonthlyExport() == 4 && g.GetMonthlyDemand() == 5 && g.GetPoweredConsumerCount() == 6 &&
              g.GetUnpoweredConsumerCount() == 7 && g.GetMonthlyRequirement() == 8);
        CHECK(g.GetPowerPlantCount() == 9 && g.GetOverworkedPowerPlantCount() == 10 && g.GetAgedPowerPlantCount() == 11 &&
              g.GetDistressedPowerPlantCount() == 12 && g.GetPowerPlantCount(2) == 13 && g.GetPowerPlantCount(9) == 14 &&
              g.GetPowerPlantCount(7) == 0 && g.GetPowerPoleCount() == 15);
    }

    if (failures) return 1;
    std::printf("sc4_powersimulator: all passed\n");
    return 0;
}
