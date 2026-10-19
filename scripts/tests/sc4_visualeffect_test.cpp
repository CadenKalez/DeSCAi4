// cSC4VisualEffect: start/stop bookkeeping, transform distribution to components, parameter records and finishing.
#include <cstdio>

#include "s3d/S3DMath.h"
#include "sc4/VisualEffect.h"

using namespace sc4;
namespace SL { cIGZMessageServer2* spMS2 = nullptr; }
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

namespace {
struct Component final : cISC4VisualEffectComponent {
    int starts = 0, stops = 0, lastStop = -1, transforms = 0, applied = 0; bool started = false; int floatCalls = 0;
    cS3DTransform lastLocal, lastWorld;
    void Start(int) override { ++starts; started = true; }
    void Stop(int t) override { ++stops; lastStop = t; if (t != 0) started = false; }
    bool IsStarted() const override { return started; }
    void SetTransforms(const cS3DTransform& a, const cS3DTransform& b) override { ++transforms; lastLocal = a; lastWorld = b; }
    void ApplyEffect(float) override { ++applied; }
    bool SetFloatParams(int, const float*, int) override { ++floatCalls; return true; }
    bool SetVoidParams(int, void**, int) override { return false; }
};
struct Manager final : IEffectsManagerLink {
    int refs = 1, starts = 0, stops = 0;
    void AddRef() override { ++refs; }
    void Release() override { --refs; }
    void StartAncilliary(cSC4VisualEffect*) override { ++starts; }
    void StopAncilliary(cSC4VisualEffect*) override { ++stops; }
};
}  // namespace

int main() {
    Manager manager;
    auto* effect = new cSC4VisualEffect;
    effect->AddRef();
    Component* component = new Component;
    component->AddRef();
    cS3DTransform offset;
    offset.flags = nS3D::kTransformTranslation;
    offset.translation[0] = 5;
    CHECK(effect->AddComponentEffect(component, offset));
    CHECK(component->transforms == 1);

    effect->Start(1);                              // no manager yet: nothing happens
    CHECK(!effect->IsStarted());
    effect->SetManager(&manager);
    effect->SetSlot(3);
    effect->Start(1);
    CHECK(effect->IsStarted() && component->starts == 1 && manager.starts == 1);

    cS3DTransform source;
    source.flags = nS3D::kTransformTranslation;
    source.translation[1] = 2;
    effect->SetSourceTransform(source);
    CHECK(component->transforms == 2);
    CHECK(component->lastLocal.translation[0] == 5 && component->lastLocal.translation[1] == 2);

    float values[2] = {1, 2};
    CHECK(effect->SetFloatParams(4, values, 2) && component->floatCalls == 1);
    CHECK(!effect->SetFloatParams(4, values, 3));      // a record keeps its size
    CHECK(!effect->SetFloatParams(0x14, values, 1));   // type out of range
    effect->ApplyParams();
    CHECK(component->floatCalls == 2);

    effect->ApplyEffect(0.1f);
    CHECK(component->applied == 1);

    // default stop fades: stays started, "stopping"
    effect->Stop(0);
    CHECK(effect->IsStarted() && effect->IsStopping() && manager.stops == 1);
    component->started = false;                        // the component finished fading
    CHECK(effect->CheckIfStopped() && !effect->IsStarted());
    effect->Shutdown();
    CHECK(manager.refs == 1);
    if (failures == 0) std::puts("sc4_visualeffect: all passed");
    return failures ? 1 : 0;
}
