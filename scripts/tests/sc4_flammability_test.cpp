#include <cstdio>
#include <unistd.h>
#include <string>

#include "rz/cRZString.h"
#include "sc4/DBSegment.h"
#include "sc4/RandomIterator.h"
#include "sc4/SCPropertyHelp.h"
#include "sc4/cSC4FlammabilitySimulator.h"

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

class FakeOccupant : public ISimOccupant {
public:
    FakeOccupant(std::uint32_t type, SC4Rect rect, std::int32_t flammability) : type_(type), rect_(rect) {
        cSCPropertyHelp::SetPropertyValue(&props_, kPropertyOccupantFlammability, flammability);
    }
    std::uint32_t GetOccupantType() override { return type_; }
    cISCPropertyHolder* GetPropertyHolder() override { return &props_; }
    SC4Rect GetBoundingRect() override { return rect_; }
    bool HasProperty(std::uint32_t id) override { return props_.HasProperty(id); }
    cSCPropertyHolder props_;
    std::uint32_t type_;
    SC4Rect rect_;
};

class FakeHost : public IFlammabilityHost, public ISerializableFactory {
public:
    int CitySizeXInCells() override { return 16; }
    int CitySizeZInCells() override { return 16; }
    bool RandomDisastersBlocked() override { return false; }
    bool PickRandomLotCell(int&, int&) override { return false; }
    void StartFire(int x, int z, bool lot, int intensity) override { fires.push_back({x, z, lot, intensity}); }
    float GetOrdinanceFlameResistance() override { return resistance; }
    int GetCurrentWeather() override { return weather; }
    bool HasWaterAt(int, int) override { return false; }
    int GetZoneTypeAt(int x, int z) override { return (x < 8 && z < 8) ? 1 : 0; }
    int GetLotZoneTypeOfOccupant(ISimOccupant*) override { return -1; }
    std::vector<ISimOccupant*> GatherOccupants(const SC4Rect& r, const std::uint32_t* types, int n) override {
        std::vector<ISimOccupant*> out;
        for (auto* o : occupants) {
            const SC4Rect b = o->GetBoundingRect();
            if (b.x1 < r.x0 || b.x0 > r.x1 || b.z1 < r.z0 || b.z0 > r.z1) continue;
            for (int i = 0; i < n; ++i)
                if (types[i] == o->GetOccupantType()) { out.push_back(o); break; }
        }
        return out;
    }
    cISCPropertyHolder* GetSimulatorTunables(const cGZPersistResourceKey&) override { return &tunables; }
    void PostFlammabilityMessage() override { ++messages; }
    void AddHistoryValue(std::uint32_t, std::uint8_t v) override { history.push_back(v); }
    void RegisterSimulator(const char*) override { registered = true; }
    void UnregisterSimulator() override { registered = false; }
    cIGZSerializable* CreateSerializable(std::uint32_t classId) override {
        if (classId == 0x49b9e602) {
            auto* g = new cSC4SimGrid<std::uint8_t>();
            g->AddRef();
            return static_cast<cIGZSerializable*>(g);
        }
        if (classId == kFlammabilitySimulatorClassId) {
            auto* s = new cSC4FlammabilitySimulator(this);
            s->AddRef();
            return static_cast<cIGZSerializable*>(s);
        }
        return nullptr;
    }

    struct Fire { int x, z; bool lot; int intensity; };
    std::vector<Fire> fires;
    std::vector<ISimOccupant*> occupants;
    std::vector<std::uint8_t> history;
    cSCPropertyHolder tunables;
    float resistance = 1.0f;
    int weather = 0;
    int messages = 0;
    bool registered = false;
};

}  // namespace

int main() {
    FakeCity city;
    SetSimGridCity(&city);
    FakeHost host;
    SetSerializableFactory(&host);

    // tunables: threshold 50, merge distance 8, min weight 1, no spontaneous lot fires, curve always fires
    cSCPropertyHelp::SetPropertyValue(&host.tunables, 0x29b4013c, std::vector<std::uint32_t>{50, 8, 1});
    cSCPropertyHelp::SetPropertyValue(&host.tunables, 0x29b4013e, 0.0f);
    cSCPropertyHelp::SetPropertyValue(&host.tunables, 0xc9b40128, 2.0f);
    cSCPropertyHelp::SetPropertyValue(&host.tunables, 0x29b4013d, std::vector<float>{0.0f, 100.0f, 1000.0f, 100.0f});

    // --- iterators visit everything exactly once ---
    {
        RandomIterator walk;
        walk.SetRect(0, 0, 15, 15);
        walk.Reset(true);
        bool seen[16][16] = {};
        int a, b, n = 0;
        while (walk.Next(a, b)) { CHECK(!seen[a][b]); seen[a][b] = true; ++n; }
        CHECK(n == 256 && walk.IsDone());
        RandomIterator list;
        list.SetModular(false);
        list.SetRect(2, 3, 9, 12);
        list.Reset(true);
        n = 0;
        while (list.Next(a, b)) { CHECK(a >= 2 && a <= 9 && b >= 3 && b <= 12); ++n; }
        CHECK(n == 8 * 10);
        LinearRandomIterator lin(37, false);
        lin.Reset(true);
        bool hit[37] = {};
        std::uint32_t v;
        n = 0;
        while (lin.Next(v)) { CHECK(v < 37 && !hit[v]); hit[v] = true; ++n; }
        CHECK(n == 37);
        LinearRandomIterator mod(100, true);
        mod.Reset(true);
        bool hit2[100] = {};
        n = 0;
        while (mod.Next(v)) { CHECK(!hit2[v]); hit2[v] = true; ++n; }
        CHECK(n == 100);
    }

    cSC4FlammabilitySimulator sim(&host);
    CHECK(sim.Init() && host.registered);
    CHECK(sim.GetFlammabilityGrid()->GetTractCountX() == 16);
    CHECK(!sim.IsCellFlammable(1, 1));

    // an occupant added: raw = its flammability, effective = raw * factor (starts at 1.0)
    FakeOccupant building(kOccupantTypeBuilding, SC4Rect{2, 2, 3, 3}, 100);
    FakeOccupant prop(kOccupantTypeProp, SC4Rect{3, 3, 3, 3}, 60);
    FakeOccupant ignored(kOccupantTypeBuilding, SC4Rect{10, 10, 10, 10}, 90);
    cSCPropertyHelp::SetPropertyValue(&ignored.props_, kPropertyNoFlammabilityFlag, true);
    host.occupants = {&building, &prop, &ignored};
    sim.OnMessage(kFlammabilityMsgOccupantAdded, &building);
    sim.OnMessage(kFlammabilityMsgOccupantAdded, &prop);
    sim.OnMessage(kFlammabilityMsgOccupantAdded, &ignored);
    CHECK(sim.IsCellFlammable(2, 2) && sim.IsCellFlammable(3, 3));
    CHECK(sim.GetFlammabilityGrid()->GetTractValue(2, 2) == 100);
    // quirk kept: an added occupant writes its own value to the effective grid even where the raw grid
    // keeps a higher one (the prop overwrote the building's 100 with 60 until the next recompute)
    CHECK(sim.GetFlammabilityGrid()->GetTractValue(3, 3) == 60);
    CHECK(!sim.IsCellFlammable(10, 10));                            // flagged occupant skipped
    CHECK(!sim.IsCellFlammable(-1, 4) && !sim.IsCellFlammable(4, 16));

    // ordinance multiplier applies on the next messages (rain doubles by the 2.0 tunable)
    host.resistance = 0.5f;
    host.weather = 2;
    sim.OnMessage(kFlammabilityMsgOrdinanceChanged, nullptr);
    sim.OnMessage(kFlammabilityMsgOccupantMoved, &building);
    CHECK(sim.GetFlammabilityGrid()->GetTractValue(2, 2) == 100);   // 100 * (0.5 * 2.0)
    host.resistance = 0.4f;
    host.weather = 0;
    sim.OnMessage(kFlammabilityMsgWeatherChanged, nullptr);
    sim.OnMessage(kFlammabilityMsgOccupantMoved, &building);
    CHECK(sim.GetFlammabilityGrid()->GetTractValue(2, 2) == 40);
    host.resistance = 1.0f;
    sim.OnMessage(kFlammabilityMsgOrdinanceChanged, nullptr);
    sim.OnMessage(kFlammabilityMsgOccupantMoved, &building);
    sim.OnMessage(kFlammabilityMsgOccupantMoved, &prop);

    // remove the prop: its cell falls back to what is still there
    host.occupants = {&building, &ignored};
    sim.OnMessage(kFlammabilityMsgOccupantRemoved, &prop);
    CHECK(sim.GetFlammabilityGrid()->GetTractValue(3, 3) == 100);

    // a full pass: 256 cells / 40 per day = 6 cells per tick; run 80 ticks to complete one
    for (int day = 0; day < 80; ++day) sim.OnMessage(kMessageDailyTick, nullptr);
    CHECK(host.messages >= 1);
    CHECK(!host.history.empty());
    // zoned cells are x,z < 8 (64 cells); flammable ones: building 2x2 at 100 = 400 total / 64 = 6
    CHECK(sim.GetAverageFlammability() == 6);
    // the four cells above the threshold merge into one hotspot of weight 4
    CHECK(sim.GetHotspots().size() == 1 && sim.GetHotspots().front().weight == 4);
    std::uint8_t hv = 0; int hx = -1, hz = -1;
    CHECK(sim.GetFlammabilityHotspot(&hv, &hx, &hz) && hv == 110 && hx >= 2 && hx <= 3 && hz >= 2 && hz <= 3);
    CHECK(!host.fires.empty());     // the curve always fires at a hotspot
    CHECK(!host.fires.front().lot && host.fires.front().intensity == 5);

    // round trip through a DB segment: the grids travel as class objects
    std::string path = "/tmp/sc4_flam_test_" + std::to_string(getpid()) + ".sc4";
    const cGZPersistResourceKey key{0x10101010, 0x20202020, 1};
    {
        cSC4DBSegment* seg = new cSC4DBSegment();
        seg->AddRef();
        seg->Init();
        cRZString p(path.c_str());
        seg->SetPath(p);
        CHECK(seg->Open(false, true));
        cISC4DBSegmentOStream* os = nullptr;
        CHECK(seg->OpenOStream(key, &os, true));
        CHECK(sim.Write(*os));
        CHECK(seg->CloseOStream(os));
        os->Release();
        CHECK(seg->Close());
        seg->Release();
    }
    {
        cSC4DBSegment* seg = new cSC4DBSegment();
        seg->AddRef();
        seg->Init();
        cRZString p(path.c_str());
        seg->SetPath(p);
        CHECK(seg->Open(true, false));
        CHECK(seg->LoadClassObjects(0x48));
        cISC4DBSegmentIStream* is = nullptr;
        CHECK(seg->OpenIStream(key, &is));
        FakeHost host2;
        cSC4FlammabilitySimulator loaded(&host2);
        CHECK(loaded.Read(*is));
        CHECK(loaded.GetAverageFlammability() == 6);
        CHECK(loaded.GetHotspots().size() == 1 && loaded.GetHotspots().front().weight == 4);
        CHECK(loaded.GetFlammabilityGrid() && loaded.GetFlammabilityGrid()->GetTractValue(2, 2) == 110);
        seg->CloseIStream(is);
        is->Release();
        seg->Close();
        seg->Release();
    }
    unlink(path.c_str());

    // SC4ReadGZSerializable round trip: the city-load serializer creates the object from its class id
    // through the factory, then QueryInterface(0xe4fda3d4) hands out the cIGZSerializable view.
    std::string rtPath = "/tmp/sc4_flam_rt_" + std::to_string(getpid()) + ".sc4";
    const cGZPersistResourceKey rtKey{0x50505050, 0x60606060, 1};
    {
        cSC4DBSegment* seg = new cSC4DBSegment();
        seg->AddRef();
        seg->Init();
        cRZString p(rtPath.c_str());
        seg->SetPath(p);
        CHECK(seg->Open(false, true));
        cISC4DBSegmentOStream* os = nullptr;
        CHECK(seg->OpenOStream(rtKey, &os, true));
        // the object itself, written through the cIGZSerializable view (version 2 + two grids + hotspots);
        // an extra reference keeps the stack object alive across the serializer's own add/release pair.
        sim.AddRef();
        CHECK(SC4WriteGZSerializable(*os, static_cast<cIGZUnknown*>(&sim)));
        CHECK(seg->CloseOStream(os));
        os->Release();
        CHECK(seg->Close());
        seg->Release();
    }
    {
        cSC4DBSegment* seg = new cSC4DBSegment();
        seg->AddRef();
        seg->Init();
        cRZString p(rtPath.c_str());
        seg->SetPath(p);
        CHECK(seg->Open(true, false));
        CHECK(seg->LoadClassObjects(0x48));
        cISC4DBSegmentIStream* is = nullptr;
        CHECK(seg->OpenIStream(rtKey, &is));
        void* raw = nullptr;
        CHECK(SC4ReadGZSerializable(*is, kGZIID_cISC4FlammabilitySimulator, &raw, false));
        CHECK(raw != nullptr);
        // downcast through the interface type, never a void chain (see Budget.cpp's QueryInterface trap)
        auto* rt = static_cast<cSC4FlammabilitySimulator*>(static_cast<cIGZSerializable*>(raw));
        CHECK(rt && rt->GetGZCLSID() == kFlammabilitySimulatorClassId);
        CHECK(rt->GetFlammabilityGrid() && rt->GetFlammabilityGrid()->GetTractValue(2, 2) == 110);
        CHECK(rt->GetHotspots().size() == 1 && rt->GetHotspots().front().weight == 4);
        static_cast<cIGZSerializable*>(raw)->Release();
        CHECK(seg->CloseIStream(is));
        is->Release();
        CHECK(seg->Close());
        seg->Release();
    }
    unlink(rtPath.c_str());

    CHECK(sim.Shutdown() && !host.registered);
    std::printf(failures ? "FAILED (%d)\n" : "PASS sc4_flammability\n", failures);
    return failures != 0;
}
