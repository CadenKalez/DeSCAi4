#include <cstdio>
#include <vector>

#include "rz/PortMemoryStreams.h"
#include "sc4/PropOccupantAutomataController.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace sc4;

namespace {
struct FakePropHost final : IPropHost {
    int ClockHour() override { return 12; }
    int ClockMinute() override { return 0; }
    void SimulatorDate(cGZDate& out) override { out.Set(1, 1, 2005); }
    std::uint32_t RandomPercent() override { return 0; }
    cISC4OccupantManager* OccupantManager() override { return nullptr; }
};
// A controller that only records activation.
struct Ctl final : cSC4AutomataController {
    std::vector<bool> activations;
    void Activate(bool on) override { activations.push_back(on); }
};
struct Manager final : IPropControllerHost {
    std::vector<std::string> created;
    std::vector<Ctl*> controllers;
    std::vector<cSC4AutomataController*> removed;
    bool present = true;
    bool HasControllerManager() override { return present; }
    cSC4AutomataController* CreateController(cISC4Occupant*, const cIGZString& name) override {
        created.push_back(std::string(name.Data(), name.Strlen()));
        auto* c = new Ctl();
        c->AddRef();   // the manager's own reference
        controllers.push_back(c);
        return c;
    }
    void RemoveController(cSC4AutomataController* c) override { removed.push_back(c); }
};
}  // namespace

int main() {
    FakePropHost propHost;
    SetPropHost(&propHost);
    Manager manager;
    SetPropControllerHost(&manager);

    auto* p = new cSC4PropOccupantAutomataController();
    p->AddRef();
    CHECK(p->GetGZCLSID() == 0x2a4017a0 && p->Controller() == nullptr && !p->ControllerActive());
    CHECK(p->AsOccupant() == static_cast<cISC4Occupant*>(p) && p->AsPropOccupant() == static_cast<cSC4PropOccupant*>(p));
    CHECK(p->SetOrientation(7) && p->GetOrientation() == 3);
    p->SetOrientation(-4);
    CHECK(p->GetOrientation() == 0);

    // template names create controllers, same name does nothing
    p->SetControllerTemplate("Smoke");
    CHECK(manager.created.size() == 1 && manager.created[0] == "Smoke" && p->Controller() == manager.controllers[0]);
    CHECK(manager.controllers[0]->RefCountValue() == 2);               // manager + the prop's reference
    CHECK(manager.controllers[0]->activations.empty() == false && manager.controllers[0]->activations[0] == false);   // restored inactive
    p->SetControllerTemplate("Smoke");
    CHECK(manager.created.size() == 1);
    p->ActivateController(true);
    CHECK(p->ControllerActive() && manager.controllers[0]->activations.back());

    // a state suffixes the template name and recreates the controller (old one is deactivated, removed, released)
    Ctl* first = manager.controllers[0];
    p->SetState(2);
    CHECK(manager.created.size() == 2 && manager.created[1] == "Smoke_2" && p->Controller() == manager.controllers[1]);
    CHECK(manager.removed.size() == 1 && manager.removed[0] == first && first->RefCountValue() == 1);
    CHECK(manager.controllers[1]->activations.back() == true);    // the active flag is re-applied
    p->SetState(2);
    CHECK(manager.created.size() == 2);                            // unchanged state: nothing
    p->SetState(0);
    CHECK(manager.created.size() == 3 && manager.created[2] == "Smoke");
    p->SetControllerTemplate("");                                  // an empty name only tears the controller down
    CHECK(p->Controller() == nullptr && manager.created.size() == 3);
    p->SetControllerTemplate("Smoke");
    p->ActivateController(false);
    CHECK(!p->ControllerActive());

    // Init reads the template name from the exemplar properties
    auto* q = new cSC4PropOccupantAutomataController();
    q->AddRef();
    cRZString name("Chimney");
    q->AsPropertyHolder()->AddProperty(kPropertyControllerTemplate, static_cast<const cIGZString&>(name), true);
    CHECK(q->Init());
    CHECK(q->IsInitialized() && q->ControllerTemplateName().Strlen() == 7 && q->ControllerActive() && q->Controller());
    float lo[3], hi[3];
    q->GetBoundingBox(lo, hi);
    CHECK(lo[0] == -8 && hi[0] == 8 && lo[1] == 0 && hi[1] == 16 && lo[2] == -8 && hi[2] == 8);   // default 16 m box, centred on (0,0,0) by SetPosition
    CHECK(q->Init());                                                  // already initialised: nothing more
    // fallback to the exemplar name
    auto* r = new cSC4PropOccupantAutomataController();
    r->AddRef();
    cRZString exemplarName("Lamp");
    r->AsPropertyHolder()->AddProperty(kPropertyExemplarName, static_cast<const cIGZString&>(exemplarName), true);
    r->Init();
    CHECK(std::string(r->ControllerTemplateName().ToChar()) == "Lamp");

    // stream round trip
    {
        PortMemoryOStream os;
        CHECK(q->Write(os));
        PortMemoryIStream is(os.bytes().data(), os.bytes().size());
        auto* s = new cSC4PropOccupantAutomataController();
        s->AddRef();
        CHECK(s->Read(is) && s->ControllerActive() && std::string(s->ControllerTemplateName().ToChar()) == "Chimney");
        s->Release();
        const std::uint8_t wrong[] = {2, 0};
        PortMemoryIStream bad(wrong, sizeof wrong);
        auto* t = new cSC4PropOccupantAutomataController();
        t->AddRef();
        CHECK(!t->Read(bad));
        t->Release();
    }

    // interfaces
    void* iface = nullptr;
    CHECK(q->QueryInterface(kGZCLSID_cSC4PropOccupantAutomataController, &iface) && iface == static_cast<cISC4PropOccupantAutomataController*>(q));
    CHECK(q->QueryInterface(kGZIID_cISC4PropOccupantAutomataController, &iface));
    CHECK(q->AddRef() == 4);   // the object + two query references + this probe
    q->Release(); q->Release(); q->Release();

    // shutdown removes and releases the controller
    cSC4AutomataController* held = q->Controller();
    held->AddRef();
    const std::size_t removedBefore = manager.removed.size();
    CHECK(q->Shutdown() && q->Controller() == nullptr && manager.removed.size() == removedBefore + 1 && held->RefCountValue() == 2);
    held->Release();
    q->Release();
    r->Release();
    p->Release();
    for (Ctl* c : manager.controllers) if (c->RefCountValue() >= 1) { while (c->RefCountValue() > 1) c->Release(); c->Release(); }
    std::printf(failures ? "FAILED\n" : "sc4_propoccupantautomatacontroller OK\n");
    return failures ? 1 : 0;
}
