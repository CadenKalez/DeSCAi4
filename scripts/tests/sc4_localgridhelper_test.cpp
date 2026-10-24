// cSC4LocalGridHelper: the "local_grid" effect lifecycle and the transform SetPosition builds, through the
// ILocalGridHost seam. Every CHECK names the decoded instruction or the quirk it pins down.
#include <cmath>
#include <cstdio>
#include <deque>
#include <string>
#include <vector>

#include "sc4/LocalGridHelper.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

using namespace sc4;

namespace {

struct FakeEffect final : ILocalGridEffect {
    int refs = 1;
    bool started = false;
    int startCalls = 0, stopCalls = 0, transformCalls = 0;
    int lastTransition = -1;
    cS3DTransform transform;
    void Start(int transition) override { ++startCalls; started = true; lastTransition = transition; }
    void Stop(int transition) override { ++stopCalls; started = false; lastTransition = transition; }
    bool IsStarted() override { return started; }
    void SetEffectTransform(const cS3DTransform& value) override { ++transformCalls; transform = value; }
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return static_cast<std::uint32_t>(++refs); }
    std::uint32_t Release() override { return static_cast<std::uint32_t>(--refs < 0 ? 0 : refs); }
};

struct FakeEffects final : ILocalGridEffects {
    std::deque<FakeEffect> effects;
    std::vector<std::string> names;
    int createCalls = 0;
    ILocalGridEffect* Create(const char* name) override {
        ++createCalls;
        names.push_back(name ? name : "");
        effects.emplace_back();
        return &effects.back();
    }
    FakeEffect* Last() { return effects.empty() ? nullptr : &effects.back(); }
};

struct FakeTerrain final : ILocalGridTerrain {
    float cell = 16.0f;
    int calls = 0;
    float CellSize() override { ++calls; return cell; }
};

struct FakeHost final : ILocalGridHost {
    FakeEffects effects;
    FakeTerrain terrain;
    bool noEffects = false;
    bool noTerrain = false;
    ILocalGridEffects* Effects() override { return noEffects ? nullptr : &effects; }
    ILocalGridTerrain* Terrain() override { return noTerrain ? nullptr : &terrain; }
};

bool Near(float a, float b) { return std::fabs(a - b) < 1e-6f; }

}  // namespace

int main() {
    // --- null host / null services: every call is a safe no-op --------------------------------------
    SetLocalGridHost(nullptr);
    {
        cSC4LocalGridHelper helper;
        CHECK(!helper.IsInitialized());
        helper.Init();                                     // 0x1000a98ce: no host, no effect
        CHECK(!helper.IsInitialized());
        helper.ShowEffect(true);                           // 0x1000a9aac: no effect, no start
        helper.SetPosition(cS3DVector3{1, 2, 3});          // 0x1000a9940: no effect, no transform
        helper.Shutdown();                                 // 0x1000a98f8: no effect, no release
        CHECK(!helper.IsInitialized());
    }

    FakeHost host;
    SetLocalGridHost(&host);
    FakeEffect* afterShutdown = nullptr;
    {
        cSC4LocalGridHelper helper;
        CHECK(!helper.IsInitialized());
        helper.Init();
        CHECK(helper.IsInitialized());
        CHECK(host.effects.createCalls == 1);
        CHECK(host.effects.names.size() == 1 && host.effects.names[0] == "local_grid");
        FakeEffect* effect = host.effects.Last();
        CHECK(effect != nullptr && effect->refs == 1 && !effect->started);

        // 0x1000a98d1: Init only creates while the slot is null.
        helper.Init();
        CHECK(host.effects.createCalls == 1);
        CHECK(host.effects.names.size() == 1);

        // --- ShowEffect: start only when stopped, stop only when started (0x1000a9ac2 / 0x1000a9ae0) --
        helper.ShowEffect(true);
        CHECK(effect->startCalls == 1 && effect->started && effect->lastTransition == 0);
        helper.ShowEffect(true);
        CHECK(effect->startCalls == 1);
        helper.ShowEffect(false);
        CHECK(effect->stopCalls == 1 && !effect->started && effect->lastTransition == 0);
        helper.ShowEffect(false);
        CHECK(effect->stopCalls == 1);

        // --- SetPosition: floor-snap + 1/16 cell, identity rotation, 16-cell scale (0x1000a9940) ------
        host.terrain.calls = 0;
        helper.SetPosition(cS3DVector3{17.0f, 5.5f, -1.0f});
        CHECK(effect->transformCalls == 1);
        CHECK(host.terrain.calls == 5);                    // five virtual CellSize calls in the original
        const cS3DTransform& t = effect->transform;
        CHECK(t.changeCounter == 2);                       // 0x1000a993a
        CHECK(t.flags == 3);                               // scale 256 != 1.0f (0x1000a9a5f)
        CHECK(t.scale == 256.0f);                          // 16 * 16.0f
        CHECK(Near(t.translation[0], 17.0f));              // floor(17/16)*16 + 1
        CHECK(t.translation[1] == 5.5f);                   // y copied through untouched
        CHECK(Near(t.translation[2], -15.0f));             // floor(-1/16) = -1 -> -16 + 1
        CHECK(t.rotation[0][0] == 1.0f && t.rotation[0][1] == 0.0f && t.rotation[0][2] == 0.0f);
        CHECK(t.rotation[1][0] == 0.0f && t.rotation[1][1] == 1.0f && t.rotation[1][2] == 0.0f);
        CHECK(t.rotation[2][0] == 0.0f && t.rotation[2][1] == 0.0f && t.rotation[2][2] == 1.0f);

        // A negative coordinate floors away from zero; truncation would land on -15, not -31.
        helper.SetPosition(cS3DVector3{-17.0f, 0.0f, -0.5f});
        CHECK(effect->transformCalls == 2);
        CHECK(Near(effect->transform.translation[0], -31.0f));   // floor(-17/16) = -2 -> -32 + 1
        CHECK(Near(effect->transform.translation[2], -15.0f));   // floor(-0.5/16) = -1 -> -16 + 1
        CHECK(host.terrain.calls == 10);

        // --- Shutdown: stop if started, null the slot first, then release (0x1000a98f8) --------------
        helper.ShowEffect(true);
        CHECK(effect->startCalls == 2 && effect->started);
        helper.Shutdown();
        CHECK(!helper.IsInitialized());
        CHECK(effect->refs == 0);
        CHECK(effect->stopCalls == 2 && !effect->started);   // stopped by Shutdown
        helper.Shutdown();                                   // second Shutdown is a no-op
        CHECK(effect->refs == 0 && effect->stopCalls == 2);
        helper.ShowEffect(true);                             // after Shutdown: nothing to start
        helper.SetPosition(cS3DVector3{1, 2, 3});
        CHECK(effect->startCalls == 2 && effect->transformCalls == 2);
        afterShutdown = effect;
    }
    CHECK(afterShutdown != nullptr && afterShutdown->refs == 0);   // the destructor did not release twice

    // --- flags 1 when cell * 16.0f == 1.0f exactly, and the 1/16-cell offset ---------------------------
    {
        host.terrain.cell = 0.0625f;                        // 1/16 -> scale 1.0f
        host.effects.effects.clear();
        host.effects.createCalls = 0;
        host.effects.names.clear();
        cSC4LocalGridHelper helper;
        helper.Init();
        FakeEffect* effect = host.effects.Last();
        CHECK(effect != nullptr);
        helper.SetPosition(cS3DVector3{1.0f, 0.0f, 1.0f});
        CHECK(effect->transform.scale == 1.0f);
        CHECK(effect->transform.flags == 1);                // 0x1000a9a88 "jne" branch
        CHECK(effect->transform.translation[0] == 1.00390625f);   // 1 + (1/16)/16
        CHECK(effect->transform.translation[2] == 1.00390625f);
        helper.Shutdown();
        CHECK(effect->refs == 0);
    }

    // --- missing services: no effects manager means no effect; no terrain means no transform ----------
    {
        FakeHost noEffects;
        noEffects.noEffects = true;
        SetLocalGridHost(&noEffects);
        cSC4LocalGridHelper helper;
        helper.Init();
        CHECK(!helper.IsInitialized());
        CHECK(noEffects.effects.createCalls == 0);

        FakeHost noTerrain;
        noTerrain.noTerrain = true;
        SetLocalGridHost(&noTerrain);
        helper.Init();
        CHECK(helper.IsInitialized());
        FakeEffect* effect = noTerrain.effects.Last();
        CHECK(effect != nullptr);
        // The original dereferences SL::spTerrain here; the seam returns before touching the effect.
        helper.SetPosition(cS3DVector3{8.0f, 8.0f, 8.0f});
        CHECK(effect->transformCalls == 0);
        CHECK(noTerrain.terrain.calls == 0);
        helper.ShowEffect(true);
        CHECK(effect->startCalls == 1);
        helper.Shutdown();
        CHECK(effect->refs == 0);
    }

    // --- destructor: releases the effect but does not stop it (0x1000a98b8) -------------------------
    {
        SetLocalGridHost(&host);
        host.terrain.cell = 16.0f;
        host.effects.effects.clear();
        host.effects.createCalls = 0;
        host.effects.names.clear();
        cSC4LocalGridHelper helper;
        helper.Init();
        FakeEffect* effect = host.effects.Last();
        CHECK(effect != nullptr && effect->refs == 1);
        helper.ShowEffect(true);
        CHECK(effect->started);
        afterShutdown = effect;   // reused as the observation slot; the deque stays alive
    }
    CHECK(afterShutdown != nullptr && afterShutdown->refs == 0);
    CHECK(afterShutdown->startCalls == 1 && afterShutdown->stopCalls == 0);
    CHECK(afterShutdown->started);   // the destructor did not stop it

    SetLocalGridHost(nullptr);
    std::printf(failures ? "localgridhelper: %d FAILURES\n" : "localgridhelper: ok\n", failures);
    return failures ? 1 : 0;
}
