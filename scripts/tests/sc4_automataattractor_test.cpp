#include <cmath>
#include <cstdio>

#include "rz/PortMemoryStreams.h"
#include "sc4/AutomataAttractor.h"
#include "sc4/DBSegment.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace sc4;

namespace {
struct Occ final : cSC4Occupant {
    float pos[3] = {40, 7, 56};
    void GetPosition(float* xyz) override { for (int i = 0; i < 3; ++i) xyz[i] = pos[i]; }
};
struct Automaton final : IControllerAutomaton {
    int refs = 0, released = 0;
    void AddRef() override { ++refs; }
    void Release() override { ++released; }
    void SetController(void*) override {}
    void SetPosition(const float*) override {}
    IControllerLeader* GetLeader() override { return nullptr; }
};
struct Queue final : IControllerQueue {
    std::vector<IControllerAutomaton*> queued;
    float p[3] = {0, 0, 0};
    void InitQueuePoint(cISC4Occupant*) override {}
    bool IsValid() override { return true; }
    const float* QueuePoint() override { return p; }
    const float* QueueDirection() override { return p; }
    void AddToQueue(IControllerAutomaton* a) override { queued.push_back(a); }
    void RemoveFromQueue(IControllerAutomaton* a) override { for (auto it = queued.begin(); it != queued.end(); ++it) if (*it == a) { queued.erase(it); break; } }
};
struct Unknown final : cIGZUnknown {
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return 2; }
    std::uint32_t Release() override { return 1; }
};
struct CtrlHost final : IAutomataControllerHost {
    Unknown city;
    Queue queue;
    cIGZUnknown* CityContext() override { return &city; }
    bool GetOccupantLot(cISC4Occupant*, SC4CellRect&, std::uint32_t&) override { return false; }
    bool PointInCity(float, float) override { return true; }
    float TerrainHeight(float, float) override { return 0; }
    float CityTerrainHeight(float, float) override { return 0; }
    float GroundHeight(float, float) override { return 0; }
    float CityWidth() override { return 100; }
    float CityDepth() override { return 100; }
    IControllerQueue* GetQueue(cISC4Occupant*, bool) override { return &queue; }
    void RegisterActiveController(void*) override {}
    bool BeginEffectQuery() override { return false; }
    bool IsEffectActive(std::uint32_t) override { return false; }
    void EndEffectQuery() override {}
    bool SoundAvailable() override { return false; }
    void PostSound(std::uint32_t, std::uint32_t, std::int64_t, std::int64_t, std::int64_t) override {}
    std::uint32_t RandomBelow(std::uint32_t) override { return 0; }
    bool ViewAvailable() override { return false; }
    bool IsPointInViewFrustum(const float*) override { return false; }
};
struct AttHost final : IAutomataAttractorHost {
    std::map<cISC4Occupant*, std::int32_t> types;
    bool GetAutomatonStateId(cISC4Occupant* o, std::int32_t& id) override {
        auto it = types.find(o);
        if (it == types.end()) return false;
        id = it->second;
        return true;
    }
};
}  // namespace

int main() {
    CtrlHost host;
    AttHost attHost;
    SetAutomataControllerHost(&host);
    SetAutomataAttractorHost(&attHost);

    auto* tmpl = static_cast<tAttractorTemplate*>(cSC4AutomataController::CreateTemplateInstance(cRZString("Shop"), kControllerTypeAttractor));
    CHECK(tmpl && tmpl->controllerType == kControllerTypeAttractor && tmpl->classKey == kControllerTypeAttractor && tmpl->field60 == -1);
    tmpl->radius = 100.0f;
    tmpl->maxAutomata = 4;
    tmpl->groups = {7, 8};
    tmpl->ignoreStates = {5};
    tmpl->strengthCurve.SetResponseCurveData(std::vector<SC4Point>{{0.0f, 1.0f}, {100.0f, 0.5f}});
    tmpl->curve.SetResponseCurveData(std::vector<SC4Point>{{0.0f, 0.0f}, {24.0f, 1.0f}});
    {   // every attractor request builds a fresh 0x148-byte tTemplate (the type picks it, no factory seam)
        tControllerTemplate* second = cSC4AutomataController::CreateTemplateInstance(cRZString("x"), kControllerTypeAttractor);
        CHECK(second != nullptr && second != static_cast<tControllerTemplate*>(tmpl) && second->controllerType == kControllerTypeAttractor);
        delete second;
    }

    auto* a = new cSC4AutomataAttractor();
    a->AddRef();
    CHECK(a->GetGZCLSID() == 0x6a1628ba && a->GetControllerType() == 0x6a1628ba && a->Flags() == kCtrlFlagReady);
    a->SetTemplate(tmpl);
    tmpl->flags = kCtrlFlagReady;
    // SetAutomataGroups only for attractor templates, never self-assigns
    a->SetAutomataGroups({9});
    CHECK(tmpl->groups.size() == 1 && tmpl->groups[0] == 9);
    a->SetAutomataGroups(tmpl->groups);
    CHECK(tmpl->groups.size() == 1);
    tmpl->controllerType = kControllerTypeGenerator;
    a->SetAutomataGroups({1, 2, 3});
    CHECK(tmpl->groups.size() == 1);
    tmpl->controllerType = kControllerTypeAttractor;
    tmpl->groups = {7, 8};

    // Init copies the template curve
    CHECK(a->Init() && a->IsTickable());
    // strength: day bit gate, radius falloff, both curves
    ControllerTime t;
    t.time = 12.0f;
    t.days = 3;
    const float expected = (1.0f - 50.0f / 100.0f) * 0.5f * 0.75f;
    CHECK(std::fabs(a->GetCurrentStrength(t, 50.0f) - expected) < 1e-5f);
    tmpl->field60 = ~(1 << 3);
    CHECK(a->GetCurrentStrength(t, 50.0f) == 0.0f);
    t.days = 35;   // 35 & 31 = 3 again
    CHECK(a->GetCurrentStrength(t, 50.0f) == 0.0f);
    tmpl->field60 = -1;

    // filter
    auto* shopper = new Occ();
    shopper->AddRef();
    shopper->AddOccupantGroup(8);
    auto* other = new Occ();
    other->AddRef();
    other->AddOccupantGroup(99);
    auto* self = new Occ();
    self->AddRef();
    self->AddOccupantGroup(7);
    CHECK(a->TestAutomataFilter(shopper) && !a->TestAutomataFilter(other));
    attHost.types[shopper] = 5;
    CHECK(!a->TestAutomataFilter(shopper));   // excluded type
    attHost.types[shopper] = 6;
    CHECK(a->TestAutomataFilter(shopper));
    tmpl->ignoreStates.clear();
    attHost.types[shopper] = 5;
    CHECK(a->TestAutomataFilter(shopper));
    a->SetOccupant(self);
    CHECK(!a->TestAutomataFilter(self));      // never attracts its own occupant
    tmpl->groups.clear();
    CHECK(!a->TestAutomataFilter(shopper));
    tmpl->groups = {7, 8};

    // enter / exit tracking
    Automaton m1, m2;
    CHECK(!a->HandleEnterState(&m1, kBehaviorTypeQueue) && a->AutomataCount() == 1 && m1.refs == 1 && host.queue.queued.size() == 1);
    a->HandleEnterState(&m1, kBehaviorTypeQueue);   // already tracked: no second reference
    CHECK(a->AutomataCount() == 1 && m1.refs == 1);
    a->HandleEnterState(&m2, 9);
    CHECK(a->AutomataCount() == 2 && m2.refs == 1);
    CHECK(!a->HandleExitState(&m1, kBehaviorTypeQueue) && a->AutomataCount() == 1 && m1.released == 1);
    a->HandleExitState(&m1, 9);
    CHECK(m1.released == 1 && a->Automata().size() == 1);
    tmpl->maxAutomata = 0;
    Automaton m3;
    a->HandleEnterState(&m3, 9);
    CHECK(m3.refs == 0 && a->AutomataCount() == 1);   // unlimited templates do not track
    tmpl->maxAutomata = 4;

    // stream round trip
    {
        a->HandleExitState(&m2, 9);
        PortMemoryOStream os;
        CHECK(a->Write(os));
        PortMemoryIStream is(os.bytes().data(), os.bytes().size());
        auto* r = new cSC4AutomataAttractor();
        r->AddRef();
        CHECK(r->Read(is) && is.RemainingBytes() == 0);
        CHECK(r->Flags() == a->Flags() && r->GetTemplateName().Strlen() == 4);
        r->Release();
    }
    shopper->Release(); other->Release(); self->Release();
    a->Release();   // FinalRelease -> Shutdown + delete
    delete tmpl;
    std::printf(failures ? "FAILED\n" : "sc4_automataattractor OK\n");
    return failures ? 1 : 0;
}
