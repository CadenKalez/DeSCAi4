#include <cstdio>
#include <vector>

#include "rz/PortMemoryStreams.h"
#include "rz/cRZMessage2Standard.h"
#include "sc4/LandValueSimulator.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace sc4;

struct FakeCity final : ISimGridCity {
    std::int32_t CitySizeXInCells() const override { return 32; }
    std::int32_t CitySizeZInCells() const override { return 32; }
    float CellSizeXMeters() const override { return 16.0f; }
    float CellSizeZMeters() const override { return 16.0f; }
};

struct FakeTerrain final : ILandValueTerrain {
    float MinAltitude() override { return 0.0f; }
    float MaxAltitude() override { return 320.0f; }
    float AltitudeAt(float x, float) override { return 40.0f + x / 8.0f; }   // 40 .. 100
    bool CellHasLand(int x, int) override { return x < 24; }
};

struct FakeHost final : ILandValueHost {
    FakeTerrain terrain;
    cSC4SimGrid<std::int16_t> desirability;
    int idleKinds = 0, notifications = 0, posts = 0, distanceCalls = 0;
    std::vector<std::int64_t> history;
    bool zoned = true;
    void AddIdleClient(cIGZMessageTarget2*, int, const char*, int) override { ++idleKinds; }
    void RemoveIdleClient(cIGZMessageTarget2*) override {}
    void AddNotification(cIGZMessageTarget2*, std::uint32_t) override { ++notifications; }
    void RemoveNotification(cIGZMessageTarget2*, std::uint32_t) override { --notifications; }
    void PostMessage(std::uint32_t message) override { if (message == kMessageLandValueEndOfCycle) ++posts; }
    bool AcquireServices() override { return true; }
    void ReleaseServices() override {}
    const cISCPropertyHolder* SimulatorExemplar() override { return nullptr; }
    ILandValueTerrain* Terrain() override { return &terrain; }
    int CellZoneType(int x, int z) override { return zoned && x < 16 && z < 16 ? 1 : 0; }
    void CreateHistory(std::uint32_t, int) override {}
    void AddHistory(std::uint32_t, std::int64_t v) override { history.push_back(v); }
    const void* GrowthDeveloperForType(std::uint32_t type) override { return reinterpret_cast<const void*>(std::uintptr_t(type)); }
    cSC4SimGrid<std::int16_t>* DesirabilityMap(const void*) override { return &desirability; }
    float AverageSlope(int, int, int, int) override { return 2.0f; }
    void AddDistanceEffect(std::vector<std::vector<std::uint8_t>>&, int, int, float, float) override { ++distanceCalls; }
    std::uint64_t BeginSlice(std::uint32_t) override { return 0; }
    bool SliceExpired(std::uint64_t) override { return false; }
};

int main() {
    FakeCity city;
    SetSimGridCity(&city);

    // cursor semantics
    TractCursor cursor;
    cursor.SetBounds(2, 2);
    int x, z, n = 0;
    cursor.Reset();
    while (cursor.Next(x, z)) ++n;
    CHECK(n == 4 && cursor.state == 2 && !cursor.Next(x, z));
    cursor.SetBounds(0, 0);
    cursor.Reset();
    CHECK(cursor.Next(x, z) && x == 0 && z == 0 && !cursor.Next(x, z));

    FakeHost host;
    host.desirability.Init();
    host.desirability.SetTractSize(2);
    host.desirability.SetTractValues(10);
    auto* sim = new cSC4LandValueSimulator(&host);
    sim->AddRef();
    sim->SetTractSize(2);
    CHECK(sim->GetGZCLSID() == 0xe990c18e && sim->Init() && sim->IsInitialised());
    CHECK(host.idleKinds == 4 && host.notifications == 5);
    sim->SetAltitudeCurve({{0.0f, 0.0f}, {100.0f, 100.0f}});

    auto* tickMessage = new cRZMessage2Standard();
    tickMessage->AddRef();
    tickMessage->SetType(0x66956814);
    int ticks = 0;
    while (sim->Stage() != 9 && ticks++ < 100000) sim->DoMessage(tickMessage);
    CHECK(sim->Stage() == 9 && host.posts == 1 && host.distanceCalls == 1);
    CHECK(host.history.size() == 1 && host.history[0] == sim->GetAverageLandValue());
    // land tracts exist (cells x<24) and the walk never touched zoned-only logic wrongly
    CHECK(sim->GetModeAltitude() >= 40.0f && sim->GetModeAltitude() <= 100.0f);
    CHECK(sim->GetSlopeMap()->GetTractValue(0, 0) == 2.0f);
    CHECK(sim->GetTotalLandValueMap()->GetTractCountX() > 0);
    CHECK(sim->GetLandValue(0, 0) == sim->GetTotalLandValueMap()->GetCellValue(0, 0));

    // a stage-9 simulator stays finished until the end-of-cycle message with the dirty flag
    sim->DoMessage(tickMessage);
    CHECK(sim->Stage() == 9);
    tickMessage->SetType(0x7699455);
    sim->DoMessage(tickMessage);
    CHECK(sim->IsDirty());
    tickMessage->SetType(0x66956816);
    sim->DoMessage(tickMessage);
    CHECK(!sim->IsDirty() && sim->Stage() == 0);

    // land value type thresholds (d3/d4 default 0 -> value >= d3, not < d4 -> type 3)
    CHECK(sim->GetLandValueType(0, 0) == 3);

    PortMemoryOStream out;
    sim->Write(out);
    CHECK(out.bytes().size() >= 3 && out.bytes()[0] == 1 && out.bytes()[1] == 0);
    const unsigned char bad[2] = {7, 0};
    PortMemoryIStream badStream(bad, 2);
    CHECK(!sim->Read(badStream));

    // the all-land retry: nothing zoned
    host.zoned = false;
    tickMessage->SetType(0x66956814);
    ticks = 0;
    while (sim->Stage() != 9 && ticks++ < 100000) sim->DoMessage(tickMessage);
    CHECK(sim->Stage() == 9);

    void* iface = nullptr;
    CHECK(sim->QueryInterface(0xe7781d8c, &iface) && !sim->QueryInterface(5, &iface));
    static_cast<cIGZUnknown*>(iface)->Release();
    tickMessage->Release();
    CHECK(sim->Shutdown() && !sim->IsInitialised());
    sim->Release();
    std::printf(failures ? "FAILED\n" : "OK\n");
    return failures != 0;
}
