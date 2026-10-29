// cSTEVolcanoLava flow core: start rect, production, flow to the lowest neighbour, rim loss, deposit / evaporation and
// the terrain refresh cadence, against a fake terrain.
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "sc4/VolcanoLava.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace sc4;

namespace {
struct Host final : ILavaHost {
    int stride = 8, maxX = 6, maxZ = 6;
    std::uint8_t neighbours = 0xff;
    std::int32_t offsets[8] = {1, 9, 8, 7, -1, -9, -8, -7};       // E, SE, S, SW, W, NW, N, NE on a stride-8 grid
    bool water = false;
    int raised = 0, decals = 0, fires = 0, padded = 0, prepared = 0, changed = 0, periodic = 0, refreshed = 0, burns = 0;
    float raisedAmount = 0, fireDepth = 0, fireAmount = 0;
    std::uint32_t raisedIndex = 0;
    LavaRect lastChanged;
    cSCPropertyHolder tuning;
    std::uint8_t GetNeighbors(std::int32_t, std::int32_t) override { return neighbours; }
    std::int32_t NeighborOffset(int d) override { return offsets[d]; }
    std::int32_t TerrainStride() override { return stride; }
    std::int32_t TerrainMaxX() override { return maxX; }
    std::int32_t TerrainMaxZ() override { return maxZ; }
    bool IsWaterVertex(std::int32_t) override { return water; }
    void RaiseVertex(std::uint32_t i, float a) override { ++raised; raisedIndex = i; raisedAmount = a; }
    void PadRectangle(LavaRect& r, int n) override {
        ++padded;                                                          // the real terrain clamps the padded rectangle to its grid
        r.left = std::max(0, r.left - n); r.top = std::max(0, r.top - n); r.right = std::min(7, r.right + n); r.bottom = std::min(7, r.bottom + n);
    }
    void CreateVertRectAroundVertex(LavaRect& r, int x, int z, int radius) override { r = {x - radius, z - radius, x + radius, z + radius}; }
    void TerrainRegionChanged(const LavaRect& r) override { ++changed; lastChanged = r; }
    void TerrainPeriodicRefresh() override { ++periodic; }
    void TerrainRefresh() override { ++refreshed; }
    cISCPropertyHolder* LoadTuningExemplar() override { return &tuning; }
    void AddDecal(int, int) override { ++decals; }
    void SetThingsOnFire(int, float depth, float amount) override { if (fires++ == 0) { fireDepth = depth; fireAmount = amount; } }   // first call only
    void PrepareForDisplay() override { ++prepared; }
    void CreateBurnAroundRegion(int, int) override { ++burns; }
    bool RegisterWithRenderer() override { return true; }
    bool StartFlowAudio() override { return true; }
};
}  // namespace

int main() {
    Host host;
    cSTEVolcanoLava lava(&host);
    lava.SetGrid(8, 8, 16.0f);
    CHECK(lava.GetLavaHeightByCityCell(1, 1) == 0.0f && lava.GetLavaHeightByCityCell(-1, 0) == 0.0f && lava.GetLavaHeightByCityCell(0, 9) == 0.0f);

    // start: the cell is the floor of the position / 16, the radius is in cells, the flags latch once
    lava.StartLavaFlow(55.0f, 71.0f, 20.0f, true);                       // cell (3, 4), radius 1
    CHECK(lava.StartRect().left == 2 && lava.StartRect().top == 3 && lava.StartRect().right == 4 && lava.StartRect().bottom == 5);
    CHECK(lava.Bounds().left == 2 && lava.Bounds().bottom == 5 && host.burns == 1);
    CHECK(lava.Registered() && lava.AudioStarted());
    lava.StartLavaFlow(48.0f, 64.0f, 0.0f, false);                       // exactly on a cell boundary: still cell (3, 4)
    CHECK(lava.StartRect().left == 3 && lava.StartRect().top == 4);

    // nothing happens while the flow is off
    lava.SetActive(false);
    lava.DoLavaFlow(1000);
    CHECK(host.padded == 0 && lava.GetLavaHeightByCityCell(3, 4) == 0.0f);

    // production only: no solidification, a flat base, no neighbour lower than the cell it came from
    lava.SetActive(true);
    lava.SetLavaProductionRate(0.5f);
    lava.SetSolidificationRate(0.0f);
    lava.StartLavaFlow(55.0f, 71.0f, 0.0f, false);                       // single cell (3, 4)
    host.neighbours = 0;                                                   // nothing to flow to
    lava.DoLavaFlow(10);
    CHECK(lava.GetLavaHeightByCityCell(3, 4) == 5.0f);
    CHECK(host.padded == 1 && host.prepared == 1 && host.changed == 0);   // no deposit: no terrain change
    CHECK(lava.Bounds().left == 2 && lava.Bounds().top == 3 && lava.Bounds().right == 4 && lava.Bounds().bottom == 5);   // padded by one

    // flow: a lower neighbour takes lava at maxFlowRate * movable * dt, capped by what the cell holds
    auto& t = cSTEVolcanoLava::TuningValues();
    t.maxFlowRate = 0.01f;
    t.heightDiffForMaxFlowRate = 4.0f;
    host.neighbours = 0x01;                                                // only direction 0 (east, index + 1)
    lava.SetLavaProductionRate(0.0f);
    lava.BaseHeight()[4 * 8 + 3] = 10.0f;                                   // the cell stands 10 above its neighbour
    host.fires = 0;
    lava.DoLavaFlow(2);
    // drop = ((10 + 5) - (0 + 0)) * 0.5 = 7.5 >= 4, so rate = 0.01; movable = min(5, 7.5) = 5; wanted = 0.01 * 5 * 2 = 0.1
    // (the scan then reaches the neighbour, which passes a little on in turn: two calls)
    CHECK(host.fires == 2 && std::fabs(host.fireAmount - 0.1f) < 1e-7f && host.fireDepth == 0.0f);
    CHECK(std::fabs(lava.GetLavaHeightByCityCell(4, 4) - 0.0999875f) < 1e-7f);
    CHECK(std::fabs(lava.GetLavaHeightByCityCell(3, 4) - 4.9f) < 1e-6f);

    // below the max-flow height difference the rate scales: drop 1.5 -> rate = 0.01 * 1.5 / 4
    t.heightDiffForMaxFlowRate = 40.0f;
    {
        cSTEVolcanoLava s(&host);
        s.SetGrid(8, 8, 16.0f);
        s.SetActive(true);
        s.SetLavaProductionRate(0.0f);
        s.SetSolidificationRate(0.0f);
        s.StartLavaFlow(55.0f, 71.0f, 0.0f, false);
        s.LavaDepth()[4 * 8 + 3] = 3.0f;
        host.fires = 0;
        s.DoLavaFlow(100);
        // drop = 3 * 0.5 = 1.5; rate = 0.01 * 1.5 / 40 = 0.000375; movable 1.5; wanted = 0.000375 * 1.5 * 100 = 0.05625
        CHECK(host.fires >= 1 && std::fabs(host.fireAmount - 0.05625f) < 1e-6f);
    }

    // the rim of the terrain (column 0 / last column / first row / last row) only loses lava
    {
        cSTEVolcanoLava s(&host);
        s.SetGrid(8, 8, 16.0f);
        s.SetActive(true);
        s.SetLavaProductionRate(0.0f);
        s.SetSolidificationRate(0.0f);
        s.StartLavaFlow(0.0f, 8.0f * 16.0f, 0.0f, false);                 // cell (0, 8) is outside; use the explicit cell below
        s.LavaDepth()[3 * 8 + 0] = 1000.0f;
        s.StartLavaFlow(0.0f, 48.0f, 0.0f, false);                         // cell (0, 3)
        host.fires = 0;
        s.DoLavaFlow(10);
        // 1000 * (1e-7 * 10) = 0.001
        CHECK(host.fires == 0 && s.GetLavaHeightByCityCell(0, 3) < 999.9995f && s.GetLavaHeightByCityCell(0, 3) > 999.9985f);
    }

    // solidification: enough lava deposits (land or water multiplier), thin lava evaporates
    {
        cSTEVolcanoLava s(&host);
        s.SetGrid(8, 8, 16.0f);
        s.SetActive(true);
        s.SetLavaProductionRate(0.0f);
        s.SetSolidificationRate(0.01f);
        s.StartLavaFlow(55.0f, 71.0f, 0.0f, false);
        host.neighbours = 0;
        t.minAmountForDepositToHappen = 2.0f;
        t.liquidToSolidConversionFactor = 0.8f;
        t.underWaterSolidificationMultiple = 5.0f;
        s.LavaDepth()[4 * 8 + 3] = 10.0f;
        host.raised = host.decals = host.changed = host.periodic = host.refreshed = 0;
        s.DoLavaFlow(10);
        // land: min(1, 0.01 * 10) = 0.1 -> 1.0 settles, raising the terrain by 1.0 * 0.8
        CHECK(std::fabs(s.GetLavaHeightByCityCell(3, 4) - 9.0f) < 1e-5f);
        CHECK(host.raised == 1 && host.raisedIndex == 35 && std::fabs(host.raisedAmount - 0.8f) < 1e-6f && host.decals == 1);
        CHECK(host.changed == 1 && host.lastChanged.left == 3 && host.lastChanged.right == 3 && host.lastChanged.top == 4 &&
              host.lastChanged.bottom == 4 && host.refreshed == 1);
        host.water = true;                                                 // under water: min(1, 5 * 0.1) = 0.5
        s.DoLavaFlow(10);
        CHECK(std::fabs(s.GetLavaHeightByCityCell(3, 4) - 4.5f) < 1e-4f);
        host.water = false;
        s.LavaDepth()[4 * 8 + 3] = 1.5f;                                   // below the deposit minimum: x0.9
        s.DoLavaFlow(10);
        CHECK(std::fabs(s.GetLavaHeightByCityCell(3, 4) - 1.35f) < 1e-5f);
        s.LavaDepth()[4 * 8 + 3] = 0.05f;                                   // ... or nothing at all below 0.1
        s.DoLavaFlow(10);
        CHECK(s.GetLavaHeightByCityCell(3, 4) == 0.0f);
    }

    // the periodic terrain refresh fires on the hundredth deposit frame and resets the counter
    {
        cSTEVolcanoLava s(&host);
        s.SetGrid(8, 8, 16.0f);
        s.SetActive(true);
        s.SetLavaProductionRate(0.0f);
        s.SetSolidificationRate(0.01f);
        s.StartLavaFlow(55.0f, 71.0f, 0.0f, false);
        host.neighbours = 0;
        host.periodic = 0;
        for (int i = 0; i < 100; ++i) {
            s.LavaDepth()[4 * 8 + 3] = 10.0f;
            s.DoLavaFlow(10);
        }
        CHECK(host.periodic == 1 && s.FrameCounter() == 99);                // started at 100: the first frame fires and resets, 99 more frames count up
    }

    // neighbour choice: straight neighbours first, a diagonal must beat the best straight drop times sqrt(2)
    {
        cSTEVolcanoLava s(&host);
        s.SetGrid(8, 8, 16.0f);
        const int centre = 3 * 8 + 3;
        for (float& depth : s.LavaDepth()) depth = 10.0f;                  // level everywhere, so only the chosen neighbours drop
        host.neighbours = 0xff;
        s.LavaDepth()[centre + 1] = 6.0f;                                   // east: drop 4
        s.LavaDepth()[centre + 9] = 4.5f;                                   // south-east: drop 5.5 < 4 * 1.4142135 = 5.657
        CHECK(s.GetNeighborWithLowestLavaLevel(centre) == 0);
        s.LavaDepth()[centre + 9] = 4.0f;                                   // drop 6 > 5.657
        CHECK(s.GetNeighborWithLowestLavaLevel(centre) == 1);
        host.neighbours = 0x00;
        CHECK(s.GetNeighborWithLowestLavaLevel(centre) == -1);
        host.neighbours = 0x02;                                             // only the diagonal is open
        CHECK(s.GetNeighborWithLowestLavaLevel(centre) == 1);
    }

    // tuning load: max flow rate is divided by a million
    cSCPropertyHelp::SetPropertyValue(&host.tuning, 0x69987e61, 2.0f);
    cSCPropertyHelp::SetPropertyValue(&host.tuning, 0x69987e62, 30.0f);
    lava.ReadTuningParameters();
    CHECK(std::fabs(cSTEVolcanoLava::TuningValues().maxFlowRate - 2e-6f) < 1e-12f && cSTEVolcanoLava::TuningValues().heightDiffForMaxFlowRate == 30.0f);

    std::puts(failures ? "sc4_volcanolava: FAILED" : "sc4_volcanolava ok");
    return failures != 0;
}
