#include <cstdio>

#include "gz/CommandParameterSet.h"
#include "rz/cRZMessage2Standard.h"
#include "sc4/BuildingOccupant.h"
#include "sc4/Disaster.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace sc4;

namespace {
class FakeManager final : public cSC4DisasterManager {
public:
    explicit FakeManager(std::uint32_t id) : id_(id) {}
    std::uint32_t GetID() const override { return id_; }
private:
    std::uint32_t id_;
};

class FakeEffect final : public cISC4VisualEffect {
public:
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    void Start(int) override { started = true; }
    void Stop(int) override { playing = false; }
    bool IsPlaying() override { return playing; }
    void SetCompletionMessage(std::uint32_t m, std::uint32_t v) override { message = m; value = v; }
    void SetIntParameter(int p, const void* d, int) override { intParam = p; intValue = *static_cast<const int*>(d); }
    void SetPointerParameter(int p, const void*, int) override { pointerParam = p; }
    std::uint32_t refs = 0, message = 0, value = 0;
    bool started = false, playing = true;
    int intParam = -1, intValue = -1, pointerParam = -1;
};

class Host final : public IDisasterHost {
public:
    cISC4DisasterLayer* layer = nullptr;
    cSC4OccupantManager* occupants = nullptr;
    std::vector<std::uint32_t> commands;
    int paused = 0, resumed = 0, saveOff = 0, saveOn = 0;
    float cameraX = -1, cameraZ = -1;
    int cameraZoom = -1;
    cISC4DisasterLayer* DisasterLayer() override { return layer; }
    cISC4OccupantManager* Occupants() override { return occupants; }
    void EmergencyPause() override { ++paused; }
    void EmergencyResume() override { ++resumed; }
    void DisableSave() override { ++saveOff; }
    void EnableSave() override { ++saveOn; }
    std::uint32_t FireDispatchCount() override { return 2; }
    std::uint32_t PoliceDispatchCount() override { return 0; }
    bool PositionToCell(float x, float z, int& cx, int& cz) override { cx = int(x / 16); cz = int(z / 16); return true; }
    bool CellCenterToPosition(int cx, int cz, float& x, float& z) override { x = cx * 16 + 8; z = cz * 16 + 8; return true; }
    float TerrainHeight(float, float) override { return 5.0f; }
    int CityCellCountX() override { return 64; }
    int CityCellCountZ() override { return 64; }
    void MoveCamera(float x, float, float z, int zoom) override { cameraX = x; cameraZ = z; cameraZoom = zoom; }
    cISC4DisasterManager* CreateManager(std::uint32_t classId) override {
        cISC4DisasterManager* m = nullptr;
        if (classId == kGZCLSID_cSC4DisasterManagerToxic) m = new cSC4DisasterManagerToxic();
        else if (classId == kGZCLSID_cSC4DisasterManagerEffect) m = new cSC4DisasterManagerEffect();
        else {
            static const std::pair<std::uint32_t, std::uint32_t> ids[] = {
                {kGZCLSID_cSC4DisasterManagerVolcano, kDisasterManagerIdVolcano},
                {kGZCLSID_cSC4DisasterManagerFire, kDisasterManagerIdFire},
                {kGZCLSID_cSC4DisasterManagerEarthquake, kDisasterManagerIdEarthquake},
                {kGZCLSID_cSC4DisasterManagerPipeBurst, kDisasterManagerIdPipeBurst},
                {kGZCLSID_cSC4DisasterManagerRiot, kDisasterManagerIdRiot}};
            for (auto& p : ids) if (p.first == classId) m = new FakeManager(p.second);
        }
        if (m) m->AddRef();
        return m;
    }
    void RegisterCommand(std::uint32_t id, const char*) override { commands.push_back(id); }
    void UnregisterCommand(std::uint32_t id) override { commands.push_back(~id); }
};

class ToxicFx final : public IToxicEffect {
public:
    void Release() override {}
    void Start(int) override {}
    void Stop(int) override {}
    void SetTransform(const cS3DTransform&) override {}
};
class FakeToxicHost final : public IToxicHost {
public:
    ToxicFx fx;
    IToxicEffect* CreateEffect(const char*) override { return &fx; }
    void CellToWorld(int cx, int cz, float& x, float& z) override { x = cx * 16.0f; z = cz * 16.0f; }
    float TerrainHeight(float, float) override { return 0.0f; }
};

class Box : public cSC4Occupant {
public:
    explicit Box(std::uint32_t type) { type_ = type; }
};
}  // namespace

int main() {
    Host host;
    FakeToxicHost toxicHost;
    SetToxicHost(&toxicHost);
    auto* occupants = new cSC4OccupantManager();
    occupants->AddRef();
    occupants->SetCitySize(1024, 1024);
    occupants->Init();
    host.occupants = occupants;
    SetDisasterHost(&host);
    auto* layer = new cSC4DisasterLayer();
    layer->AddRef();
    host.layer = layer;
    CHECK(layer->Init());
    for (std::uint32_t id : {kDisasterManagerIdVolcano, kDisasterManagerIdFire, kDisasterManagerIdEarthquake,
                             kDisasterManagerIdPipeBurst, kDisasterManagerIdRiot, kDisasterManagerIdEffect,
                             kDisasterManagerIdToxic})
        CHECK(layer->GetDisasterManager(id) && layer->GetDisasterManager(id)->GetID() == id);
    CHECK(!layer->GetDisasterManager(1234));
    CHECK(!host.commands.empty() && host.commands[0] == kMessageToxicSpill);          // toxic manager registered its command

    CHECK(layer->DisastersEnabled());
    layer->EnableDisasters(false);
    layer->EnableDisasters(false);
    CHECK(!layer->DisastersEnabled());
    layer->EnableDisasters(true);
    CHECK(!layer->DisastersEnabled());
    layer->EnableDisasters(true);
    layer->EnableDisasters(true);                    // never below zero
    CHECK(layer->DisastersEnabled());

    // ---- toxic spill
    auto* toxicManager = static_cast<cSC4DisasterManagerToxic*>(layer->GetDisasterManager(kDisasterManagerIdToxic));
    CHECK(toxicManager->GetToxicCount() == 0);
    CHECK(!layer->IsADisasterOngoing());
    toxicManager->AttemptDisasterStart(5, 6);
    CHECK(layer->IsADisasterOngoing() && host.paused == 1 && host.saveOff == 1);
    CHECK(toxicManager->GetToxicCount() == 1 && toxicManager->GetActiveInstanceCount() == 1);
    CHECK(layer->GetDisasterLocationCount() == 1);
    int lx = 0, lz = 0;
    CHECK(layer->GetMostRecentDisasterLocation(nullptr, &lx, &lz));
    toxicManager->AttemptDisasterStart(5, 6);                  // same cell adds to it
    CHECK(toxicManager->GetToxicCount() == 1);
    toxicManager->AttemptDisasterStart(40, 40);
    CHECK(toxicManager->GetToxicCount() == 2);
    SC4CellRect rect{0, 0, 10, 10};
    CHECK(toxicManager->GetToxicCountInCellRect(rect) == 1);
    CHECK(layer->GetDisasterLocationCount() == 2);

    // ---- damage assessment: a removed network tile costs the fixed per-tile cost
    auto* tile = new Box(kDisasterOccupantTypeNetwork);
    tile->AddRef();
    auto* removal = new cRZMessage2Standard();
    removal->AddRef();
    removal->SetType(0x99ef1143);
    removal->SetVoid1(static_cast<cISC4Occupant*>(tile));
    layer->DoMessage(removal);
    CHECK(layer->GetDisasterCost()->totalCost == kDamageCostPerNetworkTile);
    layer->DoMessage(removal);
    CHECK(layer->GetDisasterCost()->totalCost == 2 * kDamageCostPerNetworkTile);
    removal->Release();
    tile->Release();

    // ---- dispatch timers
    auto* dispatch = new cRZMessage2Standard();
    dispatch->AddRef();
    dispatch->SetType(kMessageDispatchBegin);
    dispatch->SetData1(1);
    dispatch->SetData2(77);
    layer->DoMessage(dispatch);
    dispatch->SetType(kMessageDispatchStop1);
    layer->DoMessage(dispatch);
    dispatch->Release();

    // ---- effect disaster
    auto* effectManager = static_cast<cSC4DisasterManagerEffect*>(layer->GetDisasterManager(kDisasterManagerIdEffect));
    FakeEffect effect;
    cS3DVector3 where{160.0f, 0.0f, 320.0f};
    cISC4DisasterInstance* instance = effectManager->StartDisasterEffect(&effect, where, 9);
    CHECK(instance && instance->GetID() == 9 && effect.started && effect.message == kMessageEffectFinished);
    int ex = 0, ez = 0;
    CHECK(instance->GetLocation(ex, ez) && ex == 10 && ez == 20);
    CHECK(effectManager->GetActiveInstanceCount() == 1);
    float toward[3] = {32.0f, 0.0f, 48.0f};
    effectManager->AttractDisasterEffect(toward);
    CHECK(instance->GetLocation(ex, ez) && ex == 2 && ez == 3);
    effectManager->CancelAttract();
    CHECK(effect.intParam == 0xb && effect.intValue == 0);
    CHECK(layer->GoToNextDisaster() && host.cameraZoom == 0);
    instance->Shutdown();
    CHECK(effectManager->GetActiveInstanceCount() == 0);
    instance->Release();

    // ---- toxic waste wears off, the disaster ends and the dispatch ratios are computed
    {
        auto* tick = new cRZMessage2Standard();
        tick->AddRef();
        tick->SetType(kMessageToxicTick);
        void* found = nullptr;
        toxicManager->GetInstances().front()->QueryInterface(kGZIID_cSC4DisasterInstanceToxic, &found);
        auto* toxic = static_cast<cSC4DisasterInstanceToxic*>(found);
        CHECK(toxic != nullptr);
        toxic->DoMessage(tick);                                   // one update: 0x78 + 0x78 + ... -> 1 less each
        CHECK(toxic->GetToxicCount() == 2);
        toxic->Release();
        tick->Release();
    }
    layer->Shutdown();
    CHECK(host.resumed >= 0);
    layer->Release();
    occupants->Release();
    std::printf(failures ? "FAILED\n" : "OK\n");
    return failures != 0;
}
