#include <cfloat>
#include <cmath>
#include <cstdio>
#include <map>

#include "sc4/SCProperty.h"
#include "sc4/TrafficSimulator.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

using namespace sc4;

namespace {

struct Host : ITrafficHost {
    std::map<std::uint32_t, int> messages;
    std::vector<std::array<int, 3>> potholes;
    std::map<int, int> road, transit;
    float funding = 1.0f;
    bool rightHand = true;
    std::map<std::uint32_t, short> grids;
    std::vector<std::vector<std::int32_t>> ordinances;
    void PostMessage(std::uint32_t m) override { ++messages[m]; }
    void PostPotholeMessage(int x, int z, bool on) override { potholes.push_back({x, z, on}); }
    void SetRoadBudgetExpense(int line, int amount) override { road[line] = amount; }
    void SetMassTransitBudgetExpense(int line, int amount) override { transit[line] = amount; }
    float MassTransitFundingFraction() override { return funding; }
    float CityCellSize() override { return 16.0f; }
    bool IsRightHandTraffic() override { return rightHand; }
    short DemandGridValue(std::uint32_t id, int, int) override { auto it = grids.find(id); return it == grids.end() ? 0 : it->second; }
    void ActiveOrdinanceStrategyAdjustments(std::vector<std::vector<std::int32_t>>& out) override { out = ordinances; }
};

}  // namespace

int main() {
    Host host;
    cRZRandom random(12345);
    cSC4TrafficSimulator sim(&host, &random);
    CHECK(sim.GetSimulatorType() == 0x88027f0d);
    CHECK(sim.GetGZCLSID() == 0x6990c1aa);

    // ---- cell set
    CellSet set;
    CHECK(set.Insert(5) && set.Insert(9) && set.Insert(7) && !set.Insert(5));
    CHECK(set.Erase(5) && !set.Erase(5) && set.Size() == 2 && set.Contains(9) && set.Contains(7));

    // ---- network info grid
    sim.InitGrid(8, 8);
    CHECK(sim.GetNetworkInfo(1, 2, 3) == nullptr);
    NetworkInfo* a = sim.AddNetworkInfo(1, 2, 3);
    a->inverseCapacity = 0.25f;
    a->connectionCount = 3;
    CHECK(sim.AddNetworkInfo(1, 2, 3) == a);
    CHECK(sim.GetCapacity(1, 2, 3) == 4.0f);
    CHECK(sim.GetConnectionCount(1, 2, 3) == 3);
    CHECK(sim.GetCapacity(2, 2, 3) == 0.0f && sim.GetConnectionCount(2, 2, 3) == 0);
    sim.AddNetworkInfo(2, 2, 3);
    sim.RemoveNetworkInfo(1, 2, 3);
    CHECK(sim.GetNetworkInfo(1, 2, 3) == nullptr && sim.GetNetworkInfo(2, 2, 3) != nullptr);
    CHECK(sim.GetNetworkInfo(1, -1, 0) == nullptr && sim.GetNetworkInfo(1, 99, 0) == nullptr);

    // ---- tunables from an exemplar
    cSCPropertyHolder exemplar;
    cSCPropertyHelp::SetPropertyValue(&exemplar, 0x491332e6, 2.5f);
    cSCPropertyHelp::SetPropertyValue(&exemplar, 0x491332e7, std::vector<float>{60.0f, 0.0f, 30.0f});   // travel type 0
    cSCPropertyHelp::SetPropertyValue(&exemplar, 0x6a84493e, std::vector<float>{1.0f, 2.0f});
    cSCPropertyHelp::SetPropertyValue(&exemplar, 0xa92356b9, std::vector<float>{0.0f, 1.0f, 2.0f, 4.0f});  // congestion curve points (0,1) (2,4)
    cSCPropertyHelp::SetPropertyValue(&exemplar, 0xa92356be, std::vector<float>{2.0f, 3.0f});
    cSCPropertyHelp::SetPropertyValue(&exemplar, 0x4953e8a3, std::vector<std::uint8_t>{10, 20, 70});
    cSCPropertyHelp::SetPropertyValue(&exemplar, 0x4953e8a4, std::vector<std::uint8_t>{30, 30, 40});
    CHECK(!sim.ReadSimulatorTunableValues(nullptr));
    CHECK(sim.ReadSimulatorTunableValues(&exemplar));
    const TrafficTunables& t = sim.Tunables();
    CHECK(t.value_bcc == 2.5f);
    // 60 km/h at 16 m cells: 60*1000/60/16 = 62.5 cells per minute
    CHECK(t.cellsPerMinute[0][0] == 62.5f && t.inverseMaximumSpeed[0][0] == 1.0f / 62.5f);
    CHECK(t.cellsPerMinute[0][1] == 0.0f && t.inverseMaximumSpeed[0][1] == FLT_MAX);
    CHECK(t.inverseMaximumSpeed[0][2] == 1.0f / (30.0f * 1000.0f / 60.0f / 16.0f));
    CHECK(t.inverseMaximumSpeed[0][3] == FLT_MAX && t.inverseMaximumSpeed[4][0] == FLT_MAX);
    CHECK(t.budgetScale[0] == 1.0f && t.budgetScale[1] == 2.0f && t.budgetScale[2] == 0.0f);
    CHECK(t.backgroundTrafficWeights[0] == 2.0f && t.backgroundTrafficWeights[1] == 3.0f && t.backgroundTrafficWeights[2] == 1.0f);
    std::vector<SC4Point> pts;
    sim.CongestionToInverseSpeedCurve().GetResponseCurveData(pts);
    CHECK(pts.size() == 2 && pts[0].y == 1.0f && pts[1].y == 0.25f);   // y values are stored reciprocal
    CHECK(t.value_eb0 == 255.0f / (0.25f - 1.0f));
    std::vector<SC4Percentage> pct;
    sim.GetTravelStrategyPercentages(0, pct);
    CHECK(pct.size() == 3 && std::fabs(pct[0].value - 0.10f) < 1e-6f && std::fabs(pct[2].value - 0.70f) < 1e-6f);
    sim.GetTravelStrategyPercentages(1, pct);
    CHECK(std::fabs(pct[2].value - 0.40f) < 1e-6f);
    // default funding curve (0, 0.02) (100, -0.01) (120, -0.04) since the exemplar has none
    CHECK(std::fabs(sim.FundingCurve().GetOutputValue(100.0f) + 0.01f) < 1e-6f);
    CHECK(t.value_ff8 == 10.0f && t.value_ffc == 60.0f && t.railAccidentChance == 1.0f);

    // ordinances adjust wealth classes 1..3 (clamped to 0..100)
    host.ordinances = {{5, 5, 5, 0, 0, 0, 0, 0, 0}, {-100, 200, 0, 0, 0, 0, 0, 0, 0}};
    sim.UpdateTravelStrategies(&exemplar);
    sim.GetTravelStrategyPercentages(1, pct);
    CHECK(std::fabs(pct[0].value - 0.0f) < 1e-6f);        // 30 + 5 - 100 -> 0
    CHECK(std::fabs(pct[1].value - 1.0f) < 1e-6f);        // 30 + 5 + 200 -> 100
    CHECK(std::fabs(pct[2].value - 0.45f) < 1e-6f);
    host.ordinances.clear();

    // ---- travel time ratio and edge densities
    CHECK(sim.GetTravelTimeRatio(2, 3, 1) == 1.0f);   // travel type not affected by traffic -> 1
    sim.MutableTunables().travelTypeEnabled.fill(false);
    sim.MutableTunables().travelTypeEnabled[0] = true;
    sim.EdgeDensityEntry(2, 3, 0, false).density = 6;
    sim.EdgeDensityEntry(2, 3, 0, true).density = 2;
    CHECK(sim.GetTrafficEdgeDensity(2, 3, 0, false).density == 6 && sim.GetTrafficEdgeDensity(2, 3, 0, true).density == 2);
    CHECK(sim.GetTrafficEdgeDensity(2, 3, 1, false).density == 0);
    CHECK(cSC4TrafficSimulator::EdgeDensityKey(2, 3, 5, true) == (0x203u | 5u << 16 | 1u << 24));

    // ---- transit switch cost
    {
        auto& sw = sim.TransitSwitchEntry(77);
        sw.traffic = 0; sw.capacity = 10; sw.cost = 3;
        sim.transitSwitchCostScale = 2.0f;
        CHECK(sim.TransitSwitchEntryCost(77) == sim.CongestionToInverseSpeedCurve().GetOutputValue(0.0f) * 3.0f * 2.0f);
        sw.traffic = 50;
        CHECK(sim.TransitSwitchEntryCost(77) == 1.7014117e+38f);
        sw.traffic = 20;
        CHECK(sim.TransitSwitchEntryCost(77) == sim.CongestionToInverseSpeedCurve().GetOutputValue(2.0f) * 3.0f * 2.0f);
    }

    // ---- background traffic
    host.grids[0x1010] = 3; host.grids[0x1030] = 4;       // group 0 = 7
    host.grids[0x3110] = 1; host.grids[0x3330] = 2;       // group 1 = 3
    host.grids[0x4400] = 10;                              // group 2 = 10 (weight 1)
    CHECK(sim.GetBackgroundTraffic(4, 4) == 7 * 2.0f + 3 * 3.0f + 10 * 1.0f);

    // ---- road funding, potholes
    sim.AddNetworkInfo(1, 1, 1);
    for (int i = 0; i < 6; ++i) sim.HealthyRoadCells().Insert(std::uint16_t((i << 8) | i));
    sim.SetRoadPotholeCapacity(6);
    host.potholes.clear();
    // curve(0%) = 0.02: accumulator 0.02 -> 0 potholes; push the accumulator with repeated calls
    SC4Percentage zero{0.0f};
    for (int i = 0; i < 25; ++i) sim.ApplyRoadFunding(zero);     // 25 * 0.02 = 0.5 -> 3 of 6
    CHECK(sim.RoadPotholeCount() == 3 && sim.DamagedRoadCells().Size() == 3 && sim.HealthyRoadCells().Size() == 3);
    CHECK(sim.StatRoadPotholes() == 3);
    {
        int damaged = 0;
        for (int i = 0; i < 6; ++i) damaged += sim.IsRoadDamaged(i, i) ? 1 : 0;
        CHECK(damaged == 3);
    }
    SC4Percentage full{1.0f};
    for (int i = 0; i < 100; ++i) sim.ApplyRoadFunding(full);    // curve(100%) = -0.01 -> accumulator back to 0
    CHECK(sim.RoadPotholeCount() == 0 && sim.DamagedRoadCells().Size() == 0 && sim.HealthyRoadCells().Size() == 6);
    // a pothole on a cell that has road network info penalises it and posts a message
    {
        CellSet healthy, damaged;
        healthy.Insert((1 << 8) | 1);
        host.potholes.clear();
        sim.ApplyPotholeChanges(healthy, damaged, 1, 1);
        CHECK(damaged.Contains((1 << 8) | 1) && healthy.Size() == 0);
        CHECK(sim.GetNetworkInfo(1, 1, 1)->potholePenalty == 32.0f);
        CHECK(host.potholes.size() == 1 && host.potholes[0][0] == 1 && host.potholes[0][1] == 1 && host.potholes[0][2] == 1);
        sim.ApplyPotholeChanges(healthy, damaged, -3, 1);
        CHECK(healthy.Contains((1 << 8) | 1) && damaged.Size() == 0 && sim.GetNetworkInfo(1, 1, 1)->potholePenalty == 0.0f);
        CHECK(host.potholes.size() == 2 && host.potholes[1][2] == 0);
    }
    // rail
    sim.HealthyRailCells().Insert(0x0102);
    sim.SetRailPotholeCapacity(1);
    for (int i = 0; i < 60; ++i) sim.ApplyMassTransitFunding(zero);   // 60 * 0.02 clamps to 1.0
    CHECK(sim.RailPotholeCount() == 1 && sim.DamagedRailCells().Contains(0x0102));
    CHECK(sim.CheckRailAccident(1, 2) && !sim.CheckRailAccident(0, 0));   // chance 1.0
    sim.MutableTunables().railAccidentChance = 0.0f;
    CHECK(!sim.CheckRailAccident(1, 2));

    // ---- strikes
    {
        sim.MutableTunables().strikeChanceScale = 100.0f;
        sim.SetStrikeEligible(5);
        host.funding = 0.9f;
        host.messages.clear();
        sim.UpdateStrikes();                                   // funding above the threat level: nothing
        CHECK(sim.GetStrikeState() == cSC4TrafficSimulator::kStrikeNone && host.messages.empty());
        host.funding = 0.5f;
        sim.UpdateStrikes();                                   // threatened
        CHECK(sim.GetStrikeState() == cSC4TrafficSimulator::kStrikeThreatened && host.messages[kTrafficMessageStrikeThreatened] == 1);
        CHECK(std::fabs(sim.StrikeChance() - 20.0f) < 1e-4f);
        sim.UpdateStrikes();                                   // funding still low -> strike begins
        CHECK(sim.GetStrikeState() == cSC4TrafficSimulator::kStrikeOn && sim.StrikeActiveFlag() && host.messages[kTrafficMessageStrikeBegan] == 1);
        for (int i = 0; i < 24; ++i) sim.UpdateStrikes();       // 24 days of strike
        CHECK(sim.GetStrikeState() == cSC4TrafficSimulator::kStrikeOn && sim.StrikeDays() == 24);
        sim.UpdateStrikes();                                   // day cap reached -> ends
        CHECK(sim.GetStrikeState() == cSC4TrafficSimulator::kStrikeNone && !sim.StrikeActiveFlag() && host.messages[kTrafficMessageStrikeEnded] == 1);
        // threat averted when funding recovers
        sim.UpdateStrikes();
        CHECK(sim.GetStrikeState() == cSC4TrafficSimulator::kStrikeThreatened);
        host.funding = 1.0f;
        sim.UpdateStrikes();
        CHECK(sim.GetStrikeState() == cSC4TrafficSimulator::kStrikeNone && host.messages[kTrafficMessageStrikeAverted] == 1);
        // no eligible service: no threat
        sim.SetStrikeEligible(0);
        host.funding = 0.0f;
        sim.UpdateStrikes();
        CHECK(sim.GetStrikeState() == cSC4TrafficSimulator::kStrikeNone);
    }

    // ---- budgets (round to nearest even)
    {
        auto& scale = sim.MutableTunables().budgetScale;
        scale.fill(0.0f);
        scale[0] = 0.5f; scale[10] = 1.0f; scale[3] = 1.0f; scale[12] = 1.0f; scale[2] = 0.5f; scale[6] = 0.5f;
        sim.Counters().c908 = 5;     // 2.5 -> 2
        sim.Counters().c930 = 3;
        sim.Counters().c914 = 7;
        sim.Counters().c938 = 1;
        sim.Counters().c910 = 3;     // 1.5 -> 2
        sim.Counters().c920 = 7;     // 3.5 -> 4
        sim.UpdateRoadsBudget();
        CHECK(host.road[2] == 3 + 2 && host.road[1] == 7 && host.road[3] == 1 + 2 && host.road[4] == 4);
        scale[1] = 0.5f; scale[7] = 2.0f; scale[8] = 0.25f; scale[9] = 1.0f;
        sim.Counters().c90c = 9;     // 4.5 -> 4
        sim.Counters().c924 = 2;
        sim.Counters().c928 = 10;    // 2.5 -> 2
        sim.Counters().c92c = 6;
        sim.UpdateMassTransitBudget();
        CHECK(host.transit[3] == 4 && host.transit[4] == 4 && host.transit[5] == 2 && host.transit[6] == 6);
    }

    // ---- trip capacity widening
    {
        cSCPropertyHolder lot;
        CHECK(cSC4TrafficSimulator::GetMaxTripCapacity(&lot, 2) == 0);
        cSC4TrafficSimulator::SetMaxTripCapacity(&lot, 2, 40);
        CHECK(cSC4TrafficSimulator::GetMaxTripCapacity(&lot, 2) == 40);
        CHECK(lot.GetProperty(kTripCapacityProperty)->GetPropertyValue()->GetType() == 0x8004);
        cSC4TrafficSimulator::SetTripCapacity(&lot, 6, 7);
        CHECK(cSC4TrafficSimulator::GetUsedTripCapacity(&lot, 2) == 7);
        cSC4TrafficSimulator::SetTripCapacity(&lot, 10, 9);
        CHECK(cSC4TrafficSimulator::GetTrafficArrived(&lot, 2) == 9);
        cSC4TrafficSimulator::SetMaxTripCapacity(&lot, 3, 70000);   // widens to 32 bit, keeping the rest
        CHECK(lot.GetProperty(kTripCapacityProperty)->GetPropertyValue()->GetType() == 0x8006);
        CHECK(cSC4TrafficSimulator::GetMaxTripCapacity(&lot, 3) == 70000 && cSC4TrafficSimulator::GetMaxTripCapacity(&lot, 2) == 40);
        CHECK(cSC4TrafficSimulator::GetUsedTripCapacity(&lot, 2) == 7 && cSC4TrafficSimulator::GetTrafficArrived(&lot, 2) == 9);
    }

    // ---- neighbour connection geometry (grid is 8x8: valid indices 0..7)
    {
        int ix = -9, iz = -9;
        CHECK(sim.LastCellX() == 7 && sim.LastCellZ() == 7);
        CHECK(sim.GetNeighborConnectionInnerCell(-1, 4, ix, iz) == 0 && ix == 0 && iz == 4);
        CHECK(sim.GetNeighborConnectionInnerCell(8, 4, ix, iz) == 2 && ix == 7 && iz == 4);
        CHECK(sim.GetNeighborConnectionInnerCell(3, -1, ix, iz) == 1 && ix == 3 && iz == 0);
        CHECK(sim.GetNeighborConnectionInnerCell(3, 8, ix, iz) == 3 && ix == 3 && iz == 7);
        CHECK(sim.GetNeighborConnectionInnerCell(3, 3, ix, iz) == -1);            // interior cell
        CHECK(sim.GetNeighborConnectionInnerCell(-1, -1, ix, iz) == -1);          // corner
        CHECK(sim.GetNeighborConnectionInnerCell(-1, 9, ix, iz) == -1);
        int x = -1, z = 4;
        sim.FindHighwayReturnTile(x, z, true);  CHECK(x == -1 && z == 3);          // right-hand, flag equal: west side steps z-1
        x = 8; z = 4; sim.FindHighwayReturnTile(x, z, true);  CHECK(x == 8 && z == 5);
        x = 3; z = -1; sim.FindHighwayReturnTile(x, z, true); CHECK(x == 4 && z == -1);
        x = 3; z = 8; sim.FindHighwayReturnTile(x, z, true);  CHECK(x == 2 && z == 8);
        x = 3; z = 3; sim.FindHighwayReturnTile(x, z, true);  CHECK(x == 3 && z == 3);   // interior: unchanged
        x = -1; z = 4; sim.FindHighwayReturnTile(x, z, false); CHECK(x == -1 && z == 5);
        x = 8; z = 4; sim.FindHighwayReturnTile(x, z, false);  CHECK(x == 8 && z == 3);
        x = 3; z = -1; sim.FindHighwayReturnTile(x, z, false); CHECK(x == 2 && z == -1);
        x = 3; z = 8; sim.FindHighwayReturnTile(x, z, false);  CHECK(x == 4 && z == 8);
        host.rightHand = false;
        x = -1; z = 4; sim.FindHighwayReturnTile(x, z, true);  CHECK(x == -1 && z == 5);   // left-hand traffic mirrors
    }

    // ---- intersection invalidation
    sim.InvalidateIntersectionData(4, 5, 6, 7);
    sim.InvalidateIntersectionData(2, 6, 9, 3);
    int x0, z0, x1, z1;
    sim.GetIntersectionDirtyRect(x0, z0, x1, z1);
    CHECK(sim.IntersectionDirty() && x0 == 2 && z0 == 5 && x1 == 9 && z1 == 7);

    // ---- subnetworks
    {
        cSC4TrafficSimulator net(&host, &random);
        net.InitGrid(8, 8);
        net.AddNetworkInfo(1, 1, 1)->subnetwork = {1, 0, 2, 0};
        net.AddNetworkInfo(1, 2, 1)->subnetwork = {2, 3, 0, 0};
        net.AddNetworkInfo(2, 3, 1)->subnetwork = {9, 0, 0, 0};   // not a road: ignored
        std::vector<std::uint32_t> ids;
        net.GetSubnetworksInRectangle({0, 0, 3, 3}, ids);
        CHECK(ids == (std::vector<std::uint32_t>{1, 2, 3}));
        net.GetSubnetworksInRectangle({4, 4, 2, 2}, ids);
        CHECK(ids.empty());
        TrafficCellRegion region;
        region.bounds = {1, 1, 2, 1};
        region.mask = {{true}, {false}};
        net.GetSubnetworksInRegion(region, ids);
        CHECK(ids == (std::vector<std::uint32_t>{1, 2}));

        net.Subnetworks().resize(4);
        net.Subnetworks()[2].occupantsByType = {{0x10, 7}, {0x20, 3}};
        net.Subnetworks()[3].occupantsByType = {{0x10, 11}};
        net.Subnetworks()[2].destinations = {5, 6, 7, 8};
        net.Subnetworks()[3].destinations = {9, 1, 1, 1};
        cSCPropertyHolder lotA, lotB, lotC, lotNone;
        cSCPropertyHelp::SetPropertyValue(&lotA, kLotSubnetworksProperty, std::vector<std::uint32_t>{2, 3, 99});
        cSCPropertyHelp::SetPropertyValue(&lotB, kLotSubnetworksProperty, std::vector<std::uint32_t>{3});
        cSCPropertyHelp::SetPropertyValue(&lotC, kLotSubnetworksProperty, std::vector<std::uint32_t>{1});
        CHECK(net.AreLotsConnected(&lotA, &lotB) && !net.AreLotsConnected(&lotA, &lotC) && !net.AreLotsConnected(&lotA, &lotNone));
        CHECK(net.GetConnectedOccupantCount(&lotA, 0x10) == 11 && net.GetConnectedOccupantCount(&lotA, 0x20) == 3);
        CHECK(net.GetConnectedOccupantCount(&lotA, 0x30) == 0 && net.GetConnectedOccupantCount(&lotNone, 0x10) == 0);
        CHECK(net.GetConnectedDestinationCount(&lotA, 0) == 9 && net.GetConnectedDestinationCount(&lotA, 3) == 8);
        CHECK(net.GetConnectedDestinationCount(&lotA, 4) == 0);
        net.GetSubnetworksForLot(&lotA, ids);
        CHECK(ids == (std::vector<std::uint32_t>{2, 3, 99}));
        net.GetOccupantCountForAllSubnetworks(0x10, ids);
        CHECK(ids == (std::vector<std::uint32_t>{0, 0, 7, 11}));
        net.ClearSubnetworks();
        net.GetSubnetworksInRectangle({0, 0, 7, 7}, ids);
        CHECK(ids.empty() && net.NextSubnetworkId() == 1);
    }

    if (failures) return 1;
    std::printf("sc4_trafficsimulator: all passed\n");
    return 0;
}
