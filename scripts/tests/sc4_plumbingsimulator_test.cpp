// Tests cSC4PlumbingSimulator: tunables, cell map / pipe / building tracking, production, pipe funding, ageing,
// the producer queries, nearest-water search and message dispatch.
#include <cmath>
#include <cstdio>
#include <map>

#include "gz/Bit1.h"
#include "sc4/PlumbingSimulator.h"
#include "sc4/SCProperty.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

using namespace sc4;

namespace {

struct SimCity : ISimGridCity {
    std::int32_t CitySizeXInCells() const override { return 64; }
    std::int32_t CitySizeZInCells() const override { return 64; }
    float CellSizeXMeters() const override { return 16.0f; }
    float CellSizeZMeters() const override { return 16.0f; }
};

struct Occ : IPlumbingOccupant {
    cSCPropertyHolder props;
    int refs = 1;
    std::uint32_t type = kPlumbingOccupantBuilding;
    float x = 0, z = 0;
    float lo[3] = {0, 0, 0}, hi[3] = {0, 0, 0};
    bool budget = true;
    IPlumbingPipe* pipe = nullptr;
    void AddRef() override { ++refs; }
    void Release() override { --refs; }
    cISCPropertyHolder* PropertyHolder() override { return &props; }
    std::uint32_t OccupantType() override { return type; }
    void GetPosition(float& px, float& pz) override { px = x; pz = z; }
    void GetFootprint(float mn[3], float mx[3]) override { for (int i = 0; i < 3; ++i) { mn[i] = lo[i]; mx[i] = hi[i]; } }
    IPlumbingPipe* AsPipe() override { return pipe; }
    bool HasBudgetInterface() override { return budget; }
};

struct Pipe : IPlumbingPipe {
    cSCPropertyHolder props;
    int refs = 1;
    std::uint32_t flags = 0;
    PlumbingRect bounds;
    std::uint8_t connections = 0;   // bit per direction
    int cx = 0, cz = 0;
    void AddRef() override { ++refs; }
    void Release() override { --refs; }
    cISCPropertyHolder* PropertyHolder() override { return &props; }
    std::uint32_t OccupantType() override { return kPlumbingOccupantNetwork; }
    void GetPosition(float&, float&) override {}
    void GetFootprint(float[3], float[3]) override {}
    IPlumbingPipe* AsPipe() override { return this; }
    bool HasBudgetInterface() override { return false; }
    bool TestFlag(std::uint32_t f) override { return (flags & f) != 0; }
    std::uint32_t GetFlags() override { return flags; }
    void SetFlags(std::uint32_t f) override { flags |= f; }
    void ClearFlags(std::uint32_t f) override { flags &= ~f; }
    void GetCellBounds(PlumbingRect& out) override { out = bounds; }
    bool HasConnection(int direction, int networkType) override { return networkType == 4 && (connections >> direction & 1); }
    void GetCell(int& x, int& z) override { x = cx; z = cz; }
};

struct FakeWaterMap : IPlumbingWaterMap {
    int w = 64, h = 64;
    std::map<std::pair<int, int>, bool> water;
    int Width() override { return w; }
    int Height() override { return h; }
    bool IsWater(int x, int z) override { return water.count({x, z}) != 0; }
};

struct Conn : IPlumbingNeighborConnection {
    int x = 0, z = 0; bool connected = false;
    void GetCell(int& ox, int& oz) override { ox = x; oz = z; }
    void SetConnected(bool c) override { connected = c; }
};

struct Host : IPlumbingHost {
    FakeWaterMap map;
    bool hasMap = true;
    float funding = 1.0f;
    std::map<cISCPropertyHolder*, PlumbingBudgetEntry> budget;
    int pipeCost = -1;
    int budgetApplied = 0, burstCalls = 0, lastBurstX = -1, lastBurstZ = -1;
    PlumbingRect lot{10, 10, 12, 11};
    bool lotExists = true;
    int capacity = 100;
    std::map<std::pair<int, int>, int> conditions;
    std::uint32_t pollution = 0;
    bool lotDisabled = false;
    std::vector<IPlumbingOccupant*> cellBuildings;
    std::vector<IPlumbingNeighborConnection*> conns;
    int CityCellsX() override { return 64; }
    int CityCellsZ() override { return 64; }
    IPlumbingWaterMap* WaterMap() override { return hasMap ? &map : nullptr; }
    void PositionToCell(float x, float z, int& cx, int& cz) override { cx = int(x / 16.0f); cz = int(z / 16.0f); }
    float DepartmentFundingFraction() override { return funding; }
    std::uint32_t PollutionAt(int, int) override { return pollution; }
    std::vector<IPlumbingOccupant*> BuildingsAtCell(int, int) override { for (auto* b : cellBuildings) b->AddRef(); return cellBuildings; }
    PlumbingBudgetEntry GetBudgetEntry(cISCPropertyHolder* h) override { auto it = budget.find(h); return it == budget.end() ? PlumbingBudgetEntry{} : it->second; }
    void SetPipeInfrastructureCost(int c) override { pipeCost = c; }
    void ApplyBuildingBudget(IPlumbingOccupant*, bool) override { ++budgetApplied; }
    bool GetLotRect(int, int, PlumbingRect& out) override { out = lot; return lotExists; }
    std::int32_t LotWaterCapacity(IPlumbingOccupant*) override { return capacity; }
    void SetLotCondition(int x, int z, int state) override { conditions[{x, z}] = state; }
    void BurstPipeAt(int x, int z) override { ++burstCalls; lastBurstX = x; lastBurstZ = z; }
    bool ProducerLotDisabled(IPlumbingOccupant*) override { return lotDisabled; }
    std::vector<IPlumbingNeighborConnection*> NeighborConnections(void*) override { return conns; }
};

struct Msg : IPlumbingMessage {
    std::uint32_t type = 0; long d1 = 0; int d2 = 0; IPlumbingOccupant* occ = nullptr; std::uint32_t idle = 0;
    bool hasChange = false; std::uint32_t changeType = 0;
    std::uint32_t Type() override { return type; }
    long Data1() override { return d1; }
    int Data2() override { return d2; }
    IPlumbingOccupant* Occupant() override { return occ; }
    std::uint32_t IdleParameter() override { return idle; }
    bool GetNetworkChangeType(std::uint32_t& t) override { t = changeType; return hasChange; }
};

struct TestSim : cSC4PlumbingSimulator {
    using cSC4PlumbingSimulator::cSC4PlumbingSimulator;
    std::uint32_t lastIdle = 0; int idleCalls = 0;
    bool DoIdle(std::uint32_t t) override { lastIdle = t; ++idleCalls; return true; }
};

}  // namespace

int main() {
    SimCity city;
    SetSimGridCity(&city);
    Host host;
    cRZRandom random(7);
    TestSim sim(&host, &random);
    CHECK(kGZCLSID_cSC4PlumbingSimulator == 0x0990c075 && sim.GetGZCLSID() == 0x0990c075);
    sim.ReadSimulatorTunableValues(nullptr);
    CHECK(sim.wearByAgeCurve.GetOutputValue(0.5f) == 1.0f && sim.pipeFundingCurve.GetOutputValue(0.0f) == -0.02f);
    CHECK(sim.groundSearchRadius == 2 && sim.surfaceSearchRadius == 1 && sim.farGroundPercent == 20 && sim.pollutionThreshold == 100);
    sim.InitCellMap();
    CHECK(sim.CellFlags().size() == 64 * 64 && sim.Dirty() && sim.GetWateredGrid() != nullptr);
    sim.SetInitialized(true);

    // ---- tunables from an exemplar
    {
        cSCPropertyHolder ex;
        cSCPropertyHelp::SetPropertyValue(&ex, 0x68f5436b, std::uint32_t(5));
        cSCPropertyHelp::SetPropertyValue(&ex, 0x68f5436d, std::uint32_t(40));
        cSCPropertyHelp::SetPropertyValue(&ex, 0xaa161f81, std::uint32_t(50));
        cSCPropertyHelp::SetPropertyValue(&ex, 0x8a84778f, 0.5f);
        cSCPropertyHelp::SetPropertyValue(&ex, 0xea4025a9, 0.9f);
        TestSim t2(&host, &random);
        t2.ReadSimulatorTunableValues(&ex);
        CHECK(t2.groundSearchRadius == 5 && t2.farGroundPercent == 40 && t2.pollutionThreshold == 50 && t2.pipeCostFactor == 0.5f);
        CHECK(t2.productionFundingCurve.GetOutputValue(60.0f) == 60.0f);   // empty property -> identity curve
        CHECK(t2.wearByAgeCurve.GetOutputValue(50.0f) == 1.0f);   // kept flat: only an empty curve is replaced
    }

    // ---- pipes: cell flags, lists, flags counters
    Pipe pipe;
    pipe.bounds = {5, 6, 5, 6};
    pipe.connections = 0b1011;   // directions 0, 1, 3
    sim.DoPipeChanged(&pipe, true);
    CHECK(sim.CellFlags()[6 * 64 + 5] == (0x10 | 1 | 2 | 8));
    CHECK(sim.GetNumPipes() == 1 && pipe.refs == 2 && host.pipeCost == 0);
    sim.DoPipeChanged(&pipe, true);   // already known: not added twice
    CHECK(sim.GetNumPipes() == 1 && pipe.refs == 2);
    sim.DoPipeChanged(&pipe, false);
    CHECK(sim.GetNumPipes() == 0 && pipe.refs == 1 && sim.CellFlags()[6 * 64 + 5] == 0);
    sim.CellFlags()[6 * 64 + 5] = 0xf3;
    pipe.flags = 0;
    sim.DoPipeChanged(&pipe, false);   // flags above 0xe0 survive
    CHECK(sim.CellFlags()[6 * 64 + 5] == 0xe0);

    // ---- buildings: consumption bit and demand; producers
    Occ house;
    house.x = 16.0f * 3 + 1;
    house.z = 16.0f * 4 + 1;
    cSCPropertyHelp::SetPropertyValue(&house.props, kPropBuildingWaterDemand, std::int32_t(25));
    sim.DoBuildingChanged(&house, true);
    CHECK(sim.GetWaterDemand() == 25 && (sim.CellFlags()[4 * 64 + 3] & 0x40) && host.budgetApplied == 1);
    CHECK(sim.GetNumWaterSources() == 0 && !sim.Dirty() == false);   // not a producer: nothing queued
    sim.DoBuildingChanged(&house, false);
    CHECK(sim.GetWaterDemand() == 0 && !(sim.CellFlags()[4 * 64 + 3] & 0x40));

    Occ pump;
    pump.x = 16.0f * 11 + 1;
    pump.z = 16.0f * 10 + 1;
    cSCPropertyHelp::SetPropertyValue(&pump.props, kPropWaterProductionCapacity, std::int32_t(100));
    cSCPropertyHelp::SetPropertyValue(&pump.props, kPropProducerWear, 5.0f);
    sim.DoOccupantInserted(&pump);
    CHECK(sim.GetNumWaterSources() == 1 && pump.refs == 2 && sim.Producers().front().x == 11 && sim.Producers().front().z == 10);
    CHECK(sim.Producers().front().capacity == 100 && (sim.CellFlags()[10 * 64 + 10] & 0x20) && (sim.CellFlags()[11 * 64 + 12] & 0x20));
    float wear = -1; cSCPropertyHelp::GetPropertyValue(&pump.props, kPropProducerWear, wear);
    CHECK(wear == 0.0f);
    // no lot: the reference is taken and leaked, nothing stored
    {
        Occ ghost = pump; ghost.refs = 1;
        host.lotExists = false;
        sim.DoBuildingChanged(&ghost, true);
        CHECK(sim.GetNumWaterSources() == 1 && ghost.refs == 2);
        host.lotExists = true;
    }

    // ---- NearestWater
    CHECK(sim.NearestWater(11, 10, WaterSourceType::None, 3) == 0);
    CHECK(sim.NearestWater(11, 10, WaterSourceType::Ground, 3) == -1);
    host.map.water[{11, 10}] = true;
    CHECK(sim.NearestWater(11, 10, WaterSourceType::Ground, 3) == 0);
    host.map.water.clear(); host.map.water[{12, 10}] = true;
    CHECK(sim.NearestWater(11, 10, WaterSourceType::Ground, 3) == 1);
    host.map.water.clear(); host.map.water[{14, 10}] = true;
    CHECK(sim.NearestWater(11, 10, WaterSourceType::Ground, 2) == -1);   // outside the radius
    const int ring = sim.NearestWater(11, 10, WaterSourceType::Ground, 4);
    CHECK(ring >= 2);
    host.hasMap = false;
    CHECK(sim.NearestWater(11, 10, WaterSourceType::Ground, 3) == -1);
    host.hasMap = true;

    // ---- UpdateWaterProduction
    host.map.water.clear(); host.map.water[{11, 10}] = true;
    host.funding = 1.0f;
    sim.productionFundingCurve.SetResponseCurveData(std::vector<SC4Point>{{0.0f, 0.0f}, {100.0f, 100.0f}});
    sim.UpdateWaterProduction();
    CHECK(sim.Producers().front().production == 100 && sim.Producers().front().percent == 1.0f && !sim.Producers().front().polluted);
    CHECK(sim.GetNumPollutedWaterSources() == 0 && sim.AveragePollution() == 0);
    host.map.water.clear(); host.map.water[{13, 10}] = true;   // two rings away: 20% of capacity
    sim.UpdateWaterProduction();
    CHECK(sim.Producers().front().production == 20);
    host.map.water.clear();
    sim.UpdateWaterProduction();
    CHECK(sim.Producers().front().production == 0);
    host.map.water[{11, 10}] = true;
    host.pollution = 500;
    sim.UpdateWaterProduction();
    CHECK(sim.Producers().front().production == 0 && sim.Producers().front().polluted && sim.GetNumPollutedWaterSources() == 1 && sim.AveragePollution() == 500);
    host.pollution = 0;
    host.funding = 0.5f;
    sim.UpdateWaterProduction();
    CHECK(sim.Producers().front().production == 50);
    host.lotDisabled = true;
    sim.UpdateWaterProduction();
    CHECK(sim.Producers().front().production == 0 && sim.Producers().front().percent == 0.0f);
    host.lotDisabled = false;
    host.funding = 1.0f;
    sim.UpdateWaterProduction();
    // surface water property selects the surface radius
    cSCPropertyHelp::SetPropertyValue(&pump.props, kPropProducerGroundwater, true);
    host.map.water.clear(); host.map.water[{12, 10}] = true;
    sim.UpdateWaterProduction();
    CHECK(sim.Producers().front().production == 100);
    host.map.water.clear(); host.map.water[{13, 10}] = true;
    sim.UpdateWaterProduction();
    CHECK(sim.Producers().front().production == 0);   // surface radius is 1
    // condition curve scales output by the wear
    cSCPropertyHelp::SetPropertyValue(&pump.props, kPropProducerWear, 0.5f);
    cSCPropertyHelp::SetPropertyValue(&pump.props, kPropProducerConditionCurve, std::vector<float>{0, 1, 1, 0});
    host.map.water.clear(); host.map.water[{11, 10}] = true;
    sim.UpdateWaterProduction();
    CHECK(sim.Producers().front().production == 50);
    SC4Percentage cond;
    CHECK(sim.GetWaterProducerConditionPercent(&pump, cond) && std::fabs(cond.value - 0.5f) < 1e-6f);
    Occ plain;
    CHECK(!sim.GetWaterProducerConditionPercent(&plain, cond));

    // ---- producer queries
    CHECK(sim.GetWaterProduction(&pump) == 50 && sim.GetWaterProduction(&plain) == 0);
    std::uint32_t prod = 0, cap = 0, cur = 77; SC4Percentage pct; bool polluted = true;
    CHECK(sim.GetWaterProducerInfo(&pump, &prod, &cap, &pct, &polluted, &cur) && prod == 50 && cap == 100 && pct.value == 0.5f && !polluted && cur == 77);
    cSCPropertyHelp::SetPropertyValue(&pump.props, kPropProducerCurrentProduced, std::int32_t(30));
    sim.GetWaterProducerInfo(&pump, nullptr, nullptr, nullptr, nullptr, &cur);
    CHECK(cur == 30);
    CHECK(!sim.GetWaterProducerInfo(&plain, &prod, nullptr, nullptr, nullptr, nullptr));
    std::list<IPlumbingOccupant*> list;
    const int refsBefore = pump.refs;
    sim.GetWaterProducers(list);
    CHECK(list.size() == 1 && list.front() == &pump && pump.refs == refsBefore + 1);

    // ---- FindWaterBuildings
    cSCPropertyHelp::SetPropertyValue(&pump.props, kPropProducerWearWarn, 80.0f);
    cSCPropertyHelp::SetPropertyValue(&pump.props, kPropProducerWearBroken, 100.0f);
    cSCPropertyHelp::SetPropertyValue(&pump.props, kPropProducerWear, 50.0f);
    host.budget[&pump.props] = {true, 0.25f, true, 1234};
    IPlumbingOccupant* least = nullptr; IPlumbingOccupant* worn = nullptr; SC4Percentage lf; int wp = 0;
    const int refs0 = pump.refs;
    sim.FindWaterBuildings(&least, &lf, &worn, &wp);
    CHECK(least == &pump && worn == &pump && lf.value == 0.25f && wp == 50 && pump.refs == refs0 + 2);
    std::int64_t upkeep = 0;
    CHECK(sim.GetOccupantMonthlyUpkeepCost(&pump, upkeep) && upkeep == 1234);
    CHECK(!sim.GetOccupantMonthlyUpkeepCost(&plain, upkeep));

    // ---- AgeWaterProducer
    sim.wearByAgeCurve.SetResponseCurveData(std::vector<SC4Point>{{0.0f, 1.0f}, {1.0f, 1.0f}});
    sim.wearByUseCurve.SetResponseCurveData(std::vector<SC4Point>{{0.0f, 0.0f}, {1.0f, 2.0f}});
    cSCPropertyHelp::SetPropertyValue(&pump.props, kPropProducerAgeRate, 10.0f);
    cSCPropertyHelp::SetPropertyValue(&pump.props, kPropProducerWear, 0.0f);
    cSCPropertyHelp::SetPropertyValue(&pump.props, kPropProducerCurrentProduced, std::int32_t(25));   // 25 of 50 produced -> use 0.5
    WaterProducer& wp0 = sim.Producers().front();
    wp0.production = 50;
    sim.AgeWaterProducer(wp0);
    cSCPropertyHelp::GetPropertyValue(&pump.props, kPropProducerWear, wear);
    CHECK(std::fabs(wear - 10.0f) < 1e-4f && host.conditions.empty());   // 1 * 10 * (0.5 * 2)
    cSCPropertyHelp::SetPropertyValue(&pump.props, kPropProducerWear, 75.0f);
    sim.AgeWaterProducer(wp0);
    CHECK(host.conditions.size() == 1 && host.conditions.begin()->second == 2);   // 85 >= warn 80
    cSCPropertyHelp::SetPropertyValue(&pump.props, kPropProducerWear, 95.0f);
    sim.AgeWaterProducer(wp0);
    CHECK(host.conditions.begin()->second == 4);   // 105 >= broken 100

    // ---- ApplyPipeFunding
    std::vector<Pipe> pipes(20);
    for (int i = 0; i < 20; ++i) { pipes[std::size_t(i)].bounds = {i, 30, i, 30}; pipes[std::size_t(i)].cx = i; pipes[std::size_t(i)].cz = 30; sim.DoPipeChanged(&pipes[std::size_t(i)], true); }
    CHECK(sim.GetNumPipes() == 20 && host.pipeCost == 0);
    sim.burstVariance = 0.0f;
    sim.pipeFundingAccum = 0.0f;
    sim.pipeFundingCurve.SetResponseCurveData(std::vector<SC4Point>{{0.0f, -0.5f}, {100.0f, -0.5f}});   // each pass adds 0.5
    sim.ApplyPipeFunding(SC4Percentage(0.5f));
    CHECK(sim.pipeFundingAccum == 0.5f && sim.GetNumDistressedPipes() == 10 && sim.GetNumBurstPipes() == 0);
    int flagged = 0; for (auto& p : pipes) flagged += (p.flags & 0x10000) != 0;
    CHECK(flagged == 10);
    sim.pipeFundingCurve.SetResponseCurveData(std::vector<SC4Point>{{0.0f, 0.25f}, {100.0f, 0.25f}});   // each pass removes 0.25
    sim.ApplyPipeFunding(SC4Percentage(0.5f));
    CHECK(sim.pipeFundingAccum == 0.25f && sim.GetNumDistressedPipes() == 5);
    flagged = 0; for (auto& p : pipes) flagged += (p.flags & 0x10000) != 0;
    CHECK(flagged == 5);
    sim.pipeFundingCurve.SetResponseCurveData(std::vector<SC4Point>{{0.0f, 5.0f}, {100.0f, 5.0f}});   // overshoot: clamp to 0
    sim.ApplyPipeFunding(SC4Percentage(0.5f));
    CHECK(sim.pipeFundingAccum == 0.0f && sim.GetNumDistressedPipes() == 0);
    // bursts: only distressed pipes burst
    sim.burstVariance = 1.0f;
    sim.pipeFundingCurve.SetResponseCurveData(std::vector<SC4Point>{{0.0f, -1.0f}, {100.0f, -1.0f}});   // everything distressed
    int totalBursts = 0;
    for (int pass = 0; pass < 20; ++pass) { sim.pipeFundingAccum = 0.0f; sim.ApplyPipeFunding(SC4Percentage(0.5f)); totalBursts = sim.GetNumBurstPipes(); }
    CHECK(sim.GetNumDistressedPipes() == 20 && totalBursts == host.burstCalls && totalBursts > 0);
    int burstFlagged = 0; for (auto& p : pipes) burstFlagged += (p.flags & 0x20000) != 0;
    CHECK(burstFlagged == totalBursts && host.lastBurstZ == 30);
    // removing pipes keeps the counters in step
    for (auto& p : pipes) sim.DoPipeChanged(&p, false);
    CHECK(sim.GetNumPipes() == 0 && sim.GetNumDistressedPipes() == 0 && sim.GetNumBurstPipes() == 0);

    // ---- grid helpers
    {
        cSC4SimGrid<std::uint8_t>* g = nullptr;
        sim.GetSupplyValueMapPtr(g);
        CHECK(g != nullptr && g == sim.GetWateredGrid());
        g->SetTractValue(7, 8, 3);
        CHECK(sim.AreWateredBuildingsInCell(7, 8) && sim.GetSupplyValue(7, 8) == 3 && sim.IsCellWatered(7, 8));
        g->SetTractValue(7, 8, 2);
        CHECK(!sim.AreWateredBuildingsInCell(7, 8) && !sim.IsCellWatered(7, 8));
        g->SetTractValue(3, 4, 1);
        CHECK(sim.IsBuildingWatered(&house));
        g->SetTractValue(3, 4, 0);
        CHECK(!sim.IsBuildingWatered(&house));
        // FillGridAtOccupant: lot rectangle when a lot exists, else the half-open footprint
        host.lot = {20, 21, 21, 22};
        pump.lo[0] = 16.0f * 20; pump.lo[2] = 16.0f * 21; pump.hi[0] = 16.0f * 25; pump.hi[2] = 16.0f * 25;
        sim.FillGridAtOccupant(*g, &pump, 4);
        CHECK(g->GetTractValue(20, 21) == 4 && g->GetTractValue(21, 22) == 4 && g->GetTractValue(22, 21) == 0 && g->GetTractValue(20, 23) == 0);
        host.lotExists = false;
        pump.lo[0] = 16.0f * 30; pump.lo[2] = 16.0f * 30; pump.hi[0] = 16.0f * 32; pump.hi[2] = 16.0f * 31;
        sim.FillGridAtOccupant(*g, &pump, 8);
        CHECK(g->GetTractValue(30, 30) == 8 && g->GetTractValue(31, 30) == 8 && g->GetTractValue(32, 30) == 0 && g->GetTractValue(30, 31) == 0);
        host.lotExists = true;
    }

    // ---- consumption
    Occ b1, b2;
    cSCPropertyHelp::SetPropertyValue(&b1.props, kPropBuildingWaterDemand, std::uint32_t(10));
    host.cellBuildings = {&b1, &b2};   // b2 has no demand property
    CHECK(sim.BuildingConsumption(3, 4) == 10 && b1.refs == 1 && b2.refs == 1);

    // ---- neighbour connections
    Conn c1, c2;
    c1.x = -5; c1.z = 70;   // clamped to (0, 63)
    int cx = 0, cz = 0;
    sim.GetConnectionCell(&c1, cx, cz);
    CHECK(cx == 0 && cz == 63);
    c2.x = 9; c2.z = 9;
    host.conns = {&c1, &c2};
    CHECK(!sim.IsNeighborConnectedToCurrentNetwork(nullptr, true) && !c2.connected);
    Bit1_PixelSet(sim.Bitmap().data(), sim.BitmapSize(), 9, 9);
    CHECK(sim.IsNeighborConnectedToCurrentNetwork(nullptr, false) && !c2.connected);
    CHECK(sim.IsNeighborConnectedToCurrentNetwork(nullptr, true) && c2.connected && !c1.connected);

    // ---- messages
    Msg m;
    m.type = kMsgSimIdle1;
    CHECK(sim.DoMessage(m) && sim.idleCalls == 1 && sim.lastIdle == 5);
    m.type = kMsgSimIdle2; m.idle = 321;
    sim.DoMessage(m);
    CHECK(sim.lastIdle == 321 && sim.idleCalls == 2);
    m.type = kMsgPlumbingFlag;
    sim.DoMessage(m);
    CHECK(sim.flag20);
    m.type = kMsgCellCleared; m.d1 = 4; m.d2 = 5;
    sim.CellFlags()[5 * 64 + 4] = 0xff;
    sim.DoMessage(m);
    CHECK(sim.CellFlags()[5 * 64 + 4] == 0xe0);
    m.type = kMsgNetworkChanged; m.d1 = 0; m.hasChange = true; m.changeType = 0x4773ffe3;
    sim.DoMessage(m);
    CHECK(sim.Countdown() == 9);
    m.type = kMsgFundingChanged;
    CHECK(!sim.DirtySecond());
    sim.DoMessage(m);
    CHECK(sim.DirtySecond());
    // occupant messages skip marked occupants
    Occ marked;
    cSCPropertyHelp::SetPropertyValue(&marked.props, 0x29244db, std::uint32_t(1));
    cSCPropertyHelp::SetPropertyValue(&marked.props, kPropBuildingWaterDemand, std::int32_t(7));
    marked.x = 16.0f * 3 + 1; marked.z = 16.0f * 4 + 1;
    m.type = kMsgOccupantInserted; m.occ = &marked;
    const std::int64_t demandBefore = sim.GetWaterDemand();
    sim.DoMessage(m);
    CHECK(sim.GetWaterDemand() == demandBefore);
    m.occ = &house;
    sim.DoMessage(m);
    CHECK(sim.GetWaterDemand() == demandBefore + 25);
    m.type = kMsgOccupantRemoved;
    sim.DoMessage(m);
    CHECK(sim.GetWaterDemand() == demandBefore);
    // pipes only react without the preview flag
    Occ pipeOcc;
    pipeOcc.type = kPlumbingOccupantNetwork;
    Pipe network; network.bounds = {40, 40, 40, 40};
    pipeOcc.pipe = &network;
    sim.DoOccupantInserted(&pipeOcc);
    CHECK(sim.GetNumPipes() == 1);
    sim.DoOccupantRemoved(&pipeOcc);
    CHECK(sim.GetNumPipes() == 0);
    network.flags = 0x80000000;
    sim.DoOccupantInserted(&pipeOcc);
    CHECK(sim.GetNumPipes() == 0);

    // ---- stats getters and lifecycle
    sim.capacity_ = 1; sim.consumed_ = 2; sim.imported_ = 3; sim.exported_ = 4;
    sim.totalBuildings_ = 10; sim.wateredBuildings_ = 7;
    CHECK(sim.GetWaterProductionCapacity() == 1 && sim.GetWaterConsumed() == 2 && sim.GetWaterImported() == 3 && sim.GetWaterExported() == 4);
    CHECK(sim.GetNumWateredBuildings() == 7 && sim.GetNumUnwateredBuildings() == 3);
    {   // the three views (the references the queries take are left: the simulator lives on the stack): cISC4PlumbingSimulator, the message target and the serializable; an unknown id answers nothing
        void* view = nullptr;
        CHECK(sim.QueryInterface(0x47782ccc, &view) && view == static_cast<cISC4PlumbingSimulator*>(&sim));
        CHECK(sim.QueryInterface(0x452294aa, &view) && view == static_cast<cIGZMessageTarget2*>(&sim));
        CHECK(sim.QueryInterface(0xe4fda3d4, &view) && view == static_cast<cIGZSerializable*>(&sim));
        view = nullptr;
        CHECK(!sim.QueryInterface(0x1234, &view) && !view);
    }
    const int pumpRefs = pump.refs;
    sim.Shutdown();
    CHECK(pump.refs == pumpRefs - 1 && sim.GetNumWaterSources() == 0 && sim.GetWateredGrid() == nullptr);

    std::printf(failures ? "FAILED %d\n" : "sc4_plumbingsimulator OK\n", failures);
    return failures ? 1 : 0;
}
