#include <cstdio>
#include <vector>

#include "rz/PortMemoryStreams.h"
#include "rz/cRZMessage2Standard.h"
#include "sc4/CommercialSimulator.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace sc4;

struct FakeChange final : ICommercialChange {
    int kind = 3;
    bool QueryInterface(std::uint32_t iid, void** out) override {
        if (iid != kIIDNetworkChange) return false;
        *out = this;
        return true;
    }
    std::uint32_t AddRef() override { return 1; }
    std::uint32_t Release() override { ++releases; return 1; }
    const int* GetKind() override { return &kind; }
    int releases = 0;
};

struct FakeHost final : ICommercialHost {
    std::vector<int> idleKinds, fills;
    int notifications = 0, removed = 0, released = 0, idleRemoved = 0;
    bool acquire = true;
    void AddIdleClient(cIGZMessageTarget2*, int kind, const char*, int priority) override {
        if (priority == 2) idleKinds.push_back(kind);
    }
    void RemoveIdleClient(cIGZMessageTarget2*) override { ++idleRemoved; }
    void AddNotification(cIGZMessageTarget2*, std::uint32_t) override { ++notifications; }
    void RemoveNotification(cIGZMessageTarget2*, std::uint32_t) override { ++removed; }
    bool AcquireServices() override { return acquire; }
    void ReleaseServices() override { ++released; }
    void FloodFillDistance(cSC4SimGrid<std::uint8_t>&, int kind) override { fills.push_back(kind); }
};

struct FakeCity final : ISimGridCity {
    std::int32_t CitySizeXInCells() const override { return 64; }
    std::int32_t CitySizeZInCells() const override { return 64; }
    float CellSizeXMeters() const override { return 16.0f; }
    float CellSizeZMeters() const override { return 16.0f; }
};

int main() {
    FakeCity city;
    SetSimGridCity(&city);
    FakeHost host;
    auto* sim = new cSC4CommercialSimulator(&host);
    sim->AddRef();
    CHECK(sim->GetGZCLSID() == 0x8990c080);
    CHECK(sim->Init() && sim->IsInitialised());
    CHECK((host.idleKinds == std::vector<int>{0, 10, 4, 6}) && host.notifications == 2);
    auto* low = sim->GetProximityMap(0x3320);
    auto* high = sim->GetProximityMap(0x3330);
    CHECK(low && high && low != high && !sim->GetProximityMap(0x1111));
    CHECK(low->GetInstanceID() == 0x89d5b788 && high->GetInstanceID() == 0x89d5b6ef);

    // idle state machine: 0 -> (1) low fill -> 2 -> high fill -> 3, waits for the dirty flag
    auto* tick = new cRZMessage2Standard();
    tick->AddRef();
    tick->SetType(kMessageIdleTick);
    CHECK(sim->DoMessage(tick) && sim->State() == 2 && host.fills == std::vector<int>{2});
    CHECK(sim->DoMessage(tick) && sim->State() == 3 && host.fills == (std::vector<int>{2, 3}));
    CHECK(sim->DoMessage(tick) && sim->State() == 3);

    // a kind-3 change message sets the dirty flag; the next idle tick restarts the cycle
    FakeChange change;
    auto* msg = new cRZMessage2Standard();
    msg->AddRef();
    msg->SetType(kMessageCommercialChangeA);
    msg->SetData3(kCommercialNetworkChangeTag);
    msg->SetVoid1(&change);
    CHECK(sim->DoMessage(msg) && sim->IsDirty() && change.releases == 1);
    CHECK(sim->DoMessage(tick) && sim->State() == 0 && !sim->IsDirty());
    change.kind = 1;
    CHECK(sim->DoMessage(msg) && !sim->IsDirty());

    // stream header: version 1 then the flag byte (true unless clean and in state 3)
    PortMemoryOStream out;
    sim->Write(out);
    CHECK(out.bytes().size() >= 3 && out.bytes()[0] == 1 && out.bytes()[1] == 0 && out.bytes()[2] == 1);
    const unsigned char bad[3] = {2, 0, 0};
    PortMemoryIStream badStream(bad, 3);
    CHECK(!sim->Read(badStream));

    void* iface = nullptr;
    CHECK(sim->QueryInterface(0x77abeb2, &iface) && !sim->QueryInterface(5, &iface));
    static_cast<cIGZUnknown*>(iface)->Release();
    tick->Release();
    msg->Release();
    CHECK(sim->Shutdown() && !sim->IsInitialised() && host.removed == 2 && host.released == 1 && host.idleRemoved == 1);
    CHECK(!sim->GetProximityMap(0x3320));
    sim->Release();
    std::printf(failures ? "FAILED\n" : "OK\n");
    return failures != 0;
}
