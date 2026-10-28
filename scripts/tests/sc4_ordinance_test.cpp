#include <cstdio>
#include "sc4/Ordinance.h"
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace sc4;
namespace {
class Host final : public IOrdinanceHost {
public:
    int year = 2000, base = 1000;
    std::vector<std::uint32_t> msgs;
    int enumerated = 0;
    void ForEachOrdinanceKey(const std::function<void(const cGZPersistResourceKey&)>& fn) override { ++enumerated; cGZPersistResourceKey key; fn(key); fn(key); }
    cISCResExemplar* LoadExemplar(const cGZPersistResourceKey&) override { return nullptr; }
    int CurrentYear() override { return year; }
    int ResidentialIncomeBase() override { return base; }
    std::string LoadString(const cGZPersistResourceKey&) override { return ""; }
    void PostMessage(std::uint32_t t, std::uint32_t, std::uint32_t) override { msgs.push_back(t); }
};
cSC4OrdinanceSimple* Make(std::uint32_t id, const char* name, std::int64_t e, std::int64_t r, std::int64_t c, float f, std::uint32_t y) {
    auto* o = new cSC4OrdinanceSimple();
    o->AddRef(); o->ForceID(id); o->ForceName(name); o->ForceIncomes(e, r, c, f, y); o->SetEnabled(true);
    return o;
}
}  // namespace
int main() {
    Host host;
    SetOrdinanceHost(&host);
    auto* sim = new cSC4OrdinanceSimulator();
    sim->AddRef();
    CHECK(sim->GetGZCLSID() == 0xa990c08e && sim->GetSimulatorType() == 0x41193c3a);
    auto* a = Make(1, "Tax", 100, 30, 50, 0.5f, 1990);
    auto* b = Make(2, "Future", 0, 0, 0, 0.0f, 2100);
    sim->AddOrdinance(a);
    sim->AddOrdinance(b);
    CHECK(sim->GetOrdinanceByID(1) == a && sim->GetOrdinanceByName("tAX") == a && sim->GetOrdinanceIDByName("Future") == 2);
    CHECK(a->IsAvailable() && !b->IsAvailable());                 // year gating on add
    CHECK(sim->GetOrdinanceAvailableCount() == 1 && sim->GetOrdinanceOnCount() == 0);
    CHECK(sim->IsOrdinanceAvailableButOff(1));
    CHECK(sim->SetOrdinanceOn(1, true) && a->IsOn());
    CHECK(a->GetMonthlyAdjustedIncome() == 100);                  // enactment income added
    a->Simulate();
    CHECK(a->GetMonthlyAdjustedIncome() == 1000 * 0.5f + 50);
    sim->DoMonthlySimulation();
    CHECK(sim->GetOrdinanceMonthlyIncome() == 100);                // pending enactment settled; not an income ordinance
    a->ForceIncomeOrdinance(true);
    sim->DoMonthlySimulation();
    CHECK(sim->GetOrdinanceMonthlyIncome() == a->GetMonthlyAdjustedIncome());
    host.year = 2200;
    sim->DoMonthlySimulation();
    CHECK(b->IsAvailable());
    std::uint32_t ids[4]; std::uint32_t n = 4;
    CHECK(sim->GetOrdinanceAvailableArray(ids, n) == 2 && n == 2);
    CHECK(sim->SetOrdinanceAvailable(1, false) && !a->IsOn());     // unavailable forces off
    CHECK(sim->RemoveOrdinance(b) && !sim->GetOrdinanceByID(2));
    CHECK(sim->DoMessage(0x66956810));
    sim->LoadAllStandardOrdinances();                      // walks the host's ordinance exemplars (here two keys that load nothing)
    CHECK(host.enumerated == 1);
    a->Release(); b->Release(); sim->Release();
    std::printf(failures ? "FAILED\n" : "OK\n");
    return failures != 0;
}
