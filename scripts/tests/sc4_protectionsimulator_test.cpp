#include <cmath>
#include <cstdio>

#include "sc4/ProtectionSimulator.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

using namespace sc4;

namespace {

class FakeCity : public ISimGridCity {
public:
    std::int32_t CitySizeXInCells() const override { return 16; }
    std::int32_t CitySizeZInCells() const override { return 16; }
    float CellSizeXMeters() const override { return 16.0f; }
    float CellSizeZMeters() const override { return 16.0f; }
};

struct Station : IProtectionStation {
    float x, z;
    std::uint32_t type = kProtectionBuildingOccupantType;
    std::uint32_t bits = 4, offset = 0, mask = 0;
    int available = 2;
    Station(float px, float pz) : x(px), z(pz) {}
    std::uint32_t OccupantType() override { return type; }
    void Position(float p[3]) override { p[0] = x; p[1] = 0; p[2] = z; }
};

struct Host : IProtectionHost {
    int passes = 0, cells = 0, taken = 0;
    bool above = true;
    int avail = 3;
    bool GetStationEffect(IProtectionStation* s, ProtectionEffect& e) override {
        auto* st = static_cast<Station*>(s);
        e.x = st->x; e.z = st->z; e.radius = 64.0f; e.peak = 100; e.edge = 20;
        return true;
    }
    void OnAdjustedCell(int, int, int) override { ++cells; }
    void OnPassFinished() override { ++passes; }
    void SetStationDispatchMask(IProtectionStation* s, std::uint32_t m) override { static_cast<Station*>(s)->mask = m; }
    void SetStationDispatchOffset(IProtectionStation* s, std::uint32_t o) override { static_cast<Station*>(s)->offset = o; }
    void GetStationInfo(IProtectionStation* s, int* a, std::uint32_t* o, std::uint32_t* b) override {
        auto* st = static_cast<Station*>(s);
        if (a) *a = st->available;
        if (o) *o = st->offset;
        if (b) *b = st->bits;
    }
    int MinimumDispatchesForService() override { return 2; }
    std::uint32_t DispatchPurpose() override { return 7; }
    int AvailableDispatchCount(std::uint32_t) override { return avail; }
    std::vector<std::uint32_t> ActiveDispatches(std::uint32_t) override { return {}; }
    bool GetDispatch(std::uint32_t, ProtectionDispatchInfo&) override { return false; }
    bool LotAllowsDispatch(IProtectionStation*) override { return true; }
    bool IsAboveWater(float, float) override { return above; }
    bool IsZonedAt(int, int) override { return true; }
    void RegisterStationProperty(IProtectionStation*, bool) override {}
    void GetDispatchEffect(const ProtectionDispatchInfo&, ProtectionEffect&) override {}
    void TakeStationDispatch(IProtectionStation*) override { ++taken; }
};

}  // namespace

int main() {
    FakeCity city;
    SetSimGridCity(&city);

    // radius effect: linear falloff, saturating
    std::vector<std::vector<std::int16_t>> grid(8, std::vector<std::int16_t>(8, 0));
    AddRadiusEffect(grid, 8, 8, 4, 4, 3.0f, 90.0f, 0.0f);
    CHECK(grid[4][4] == 90);
    CHECK(grid[5][4] == 60);           // 90 - 1 * 30
    CHECK(grid[4][7] == 0 && grid[7][4] == 0);   // distance 3 is not < radius
    CHECK(grid[0][0] == 0);
    for (auto& c : grid) for (auto& v : c) v = 32760;
    AddRadiusEffect(grid, 8, 8, 4, 4, 3.0f, 90.0f, 0.0f);
    CHECK(grid[4][4] == 32767);

    Host host;
    cSC4ProtectionSimulator sim(&host, 1, 4);
    sim.CreateGrids(16, 16);
    sim.zoneColumns.assign(16, std::vector<std::int8_t>(16, 1));
    CHECK(sim.cellsPerDay == 64);      // 16 * 16 / 4
    sim.scale = 0.5f;

    Station a(32.0f, 32.0f), b(160.0f, 160.0f);
    sim.DoOccupantInserted(&a);
    CHECK(sim.Stations().size() == 1 && a.offset == 1 && a.mask == 0xf && sim.dispatchBitsInUse == 5);
    sim.DoOccupantInserted(&b);
    CHECK(b.offset == 5 && sim.dispatchBitsInUse == 9);     // first free run above station a's bits 1..4
    CHECK(sim.GetMaxPossibleDispatchCount() == 8);

    const int cx = static_cast<int>(32.0f * sim.UnadjustedGrid()->GetOneOverTractWidthX());
    CHECK(sim.UnadjustedGrid()->GetTractValue(cx, cx) == 100);
    CHECK(sim.AdjustedGrid()->GetTractValue(cx, cx) == 50);  // rounded unadjusted * scale
    CHECK(sim.AdjustedGrid()->GetTractValue(15, 0) == 0);

    // adjusting an effect scales radius and strengths (truncated)
    ProtectionEffect e;
    e.radius = 10; e.peak = 7; e.edge = 3;
    cSC4ProtectionSimulator::AdjustProtectionEffect(e, 0.5f);
    CHECK(e.radius == 5.0f && e.peak == 3 && e.edge == 1);

    // dispatching
    bool enough = false;
    CHECK(sim.CanDispatch(10, 10, &enough) && enough);
    host.avail = 1;
    CHECK(sim.CanDispatch(10, 10, &enough) && !enough);
    host.above = false;
    CHECK(!sim.CanDispatch(10, 10, &enough));
    std::uint32_t id = 99;
    CHECK(sim.DispatchFromNearestStation(150, 150, &id) == &b && id == 0 && host.taken == 1);
    b.available = 0;
    CHECK(sim.DispatchFromNearestStation(150, 150, &id) == &a);

    // daily sweep: 4 days cover the whole 16x16 city -> one pass finishes on the next tick
    for (int i = 0; i < 4; ++i) sim.DoDailySimulation();
    CHECK(host.cells == 256 && host.passes == 1);   // the very first tick finds the unreset walk empty and closes an empty pass
    sim.DoDailySimulation();
    CHECK(host.passes == 2);
    CHECK(sim.AverageProtection() > 0.0f && sim.CoverageShare() > 0.0f && sim.CoverageShare() <= 1.0f);

    // removal clears the area
    sim.DoOccupantRemoved(&a);
    CHECK(sim.Stations().size() == 1 && sim.UnadjustedGrid()->GetTractValue(cx, cx) == 0);
    a.type = 0;
    sim.DoOccupantInserted(&a);        // wrong occupant type ignored
    CHECK(sim.Stations().size() == 1);

    std::printf(failures ? "FAILED\n" : "sc4_protectionsimulator: all passed\n");
    return failures ? 1 : 0;
}
