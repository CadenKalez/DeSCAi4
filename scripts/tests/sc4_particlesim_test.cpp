#include <cstdio>

#include "gz/COMLibrary.h"
#include "sc4/ParticleSimulatorBase.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace sc4;

struct Sim final : cSCParticleSimulatorBase {
    int destroyed = 0;
    void DestroyParticle(cSCParticle* p) override { ++destroyed; cSCParticleSimulatorBase::DestroyParticle(p); }
};

int main() {
    auto* sim = new Sim();
    sim->AddRef();
    CHECK(sim->Init() && !sim->IsStarted() && !sim->Stop(false, false));
    CHECK(sim->Start() && sim->IsStarted());
    cSCParticle* a = sim->CreateParticle();
    cSCParticle* b = sim->CreateParticle();
    a->alive = b->alive = 1;
    sim->AddParticle(a);
    sim->AddParticle(b);
    CHECK(sim->LiveParticleCount() == 2 && sim->GetParticles().size() == 2);
    sim->RemoveParticle(a);
    CHECK(a->alive == 0);
    CHECK(!sim->CheckForStop());                          // particles still alive
    CHECK(!sim->Stop(true, false) && sim->IsInDelayedStop() && sim->Start());   // a delayed stop can be cancelled by Start
    CHECK(!sim->IsInDelayedStop());
    CHECK(sim->Stop(false, false) && !sim->IsStarted() && sim->destroyed == 2 && sim->LiveParticleCount() == 0);
    CHECK(sim->GetParticles().empty());

    // delayed stop finishes when the last particle is gone
    CHECK(sim->Start());
    cSCParticle* c = sim->CreateParticle();
    sim->AddParticle(c);
    sim->Stop(true, false);
    CHECK(!sim->CheckForStop());
    sim->NoteParticleDied();
    CHECK(sim->CheckForStop() && !sim->IsStarted());
    void* raw = nullptr;
    CHECK(sim->QueryInterface(1, &raw) && !sim->QueryInterface(2, &raw));

    // release-when-done: Stop(true, true) takes a reference that CheckForStop gives back
    Sim* other = new Sim();
    other->AddRef();
    other->Init();
    other->Start();
    other->Stop(true, true);
    CHECK(other->RefCount() == 2);
    other->Release();                                      // the caller's reference; the pending stop still owns one
    CHECK(other->RefCount() == 1);
    delete other;

    sim->Release();                                        // refcount 2 (AddRef + QueryInterface) -> 1
    sim->Release();

    // library list ordering
    cRZString pathA("alpha.dll"), pathB("beta.dll"), pathLong("alpha.dll.bak");
    cGZCOMLibrary a1(pathA), b1(pathB), l1(pathLong);
    CHECK(a1 < b1 && !(b1 < a1) && a1 < l1 && !(l1 < a1) && !(a1 < a1));
    CHECK(!a1.Load() && !a1.IsLoaded() && a1.GetHandle() == nullptr && a1.GetDirector() == nullptr);
    cRZString out;
    a1.GetPath(out);
    CHECK(std::string(out.Data()) == "alpha.dll");
    void* iface = nullptr;
    CHECK(a1.QueryInterface(0x07d4922e, &iface) && !a1.QueryInterface(5, &iface));
    std::printf(failures ? "FAILED\n" : "OK\n");
    return failures != 0;
}
