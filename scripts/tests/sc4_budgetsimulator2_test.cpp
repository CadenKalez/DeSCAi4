// Init / Shutdown / DoMessage / registry callbacks of cSC4BudgetSimulator with fake services.
#include <cstdio>

#include "sc4/BudgetSimulator.h"
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace sc4;
namespace {
class Env final : public IBudgetSimEnvironment {
public:
    int difficulty = 1;
    std::vector<std::pair<std::uint32_t, std::int64_t>> history;
    std::vector<std::uint32_t> messages;
    void SimulatorDate(int& y, int& m) override { y = 1; m = 1; }
    std::int64_t OrdinanceMonthlyIncome() override { return 0; }
    std::int64_t OrdinanceMonthlyExpense() override { return 0; }
    std::int64_t NeighborsIncome() override { return 0; }
    std::int64_t NeighborsExpense() override { return 0; }
    int DifficultyLevel() override { return difficulty; }
    void ForEachTaxableLot(const std::function<void(const LotTaxInfo&)>&) override {}
    void PostMessage(std::uint32_t type, std::uint32_t, std::int64_t, std::int64_t) override { messages.push_back(type); }
    void AddHistory(std::uint32_t id, std::int64_t v) override { history.push_back({id, v}); }
    void PostBudgetMessage(std::uint32_t, cSC4DepartmentBudget*, std::uint32_t, std::uint32_t) override {}
    void NotifyLocalFundingChanged(cISCPropertyHolder*) override {}
    std::string LoadLocalizedString(std::uint32_t, std::uint32_t) override { return ""; }
    std::string BuildingName(std::uint32_t) override { return ""; }
};
class Svc final : public IBudgetSimServices {
public:
    std::vector<std::string> log;
    std::vector<std::uint32_t> subs, unsubs;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> resets;
    bool cheats = true, layer = true, spawnOk = true;
    double rnd = 0.0;
    int lastX = -1, lastZ = -1, spawned = 0;
    bool lastExpense = false; int dialogs = 0;
    void RegisterSimulatorNotifications(void*) override { log.push_back("sim"); }
    void UnregisterSimulator(void*) override { log.push_back("unsim"); }
    void SubscribeMessage(std::uint32_t m) override { subs.push_back(m); }
    void UnsubscribeMessage(std::uint32_t m) override { unsubs.push_back(m); }
    void AcquireServices() override { log.push_back("acq"); }
    void ReleaseServices() override { log.push_back("rel"); }
    void RegisterCheatCodes() override { log.push_back("cheat"); }
    void UnregisterCheatCodes() override { log.push_back("uncheat"); }
    void RegisterGameDataNodes(cSC4BudgetSimulator*) override { log.push_back("nodes"); }
    void UnregisterGameDataNodes(cSC4BudgetSimulator*) override { log.push_back("unnodes"); }
    void ResetHistory(std::uint32_t id, std::uint32_t d) override { resets.push_back({id, d}); }
    bool CityCheatsEnabled() override { return cheats; }
    double RandomDouble() override { return rnd; }
    void CityCellSize(float& x, float& z) override { x = 128; z = 64; }
    bool SpawnDisasterAt(int x, int z) override { lastX = x; lastZ = z; ++spawned; return spawnOk; }
    bool HasDisasterLayer() override { return layer; }
    void ShowBudgetDialog(bool e) override { lastExpense = e; ++dialogs; }
};
}  // namespace

int main() {
    Env env; Svc svc;
    cSC4BudgetSimulator::SetEnvironment(&env);
    cSC4BudgetSimulator::SetServices(&svc);
    auto* sim = new cSC4BudgetSimulator();
    sim->AddRef();
    auto& t = sim->GetTunables();
    t.startingFunds = {5000, 7500, 9000};
    t.defaultTaxRate = {5.0f, 7.0f, 9.0f};
    t.taxScale = 25.0f;

    // Init on a fresh city
    CHECK(sim->Init());
    CHECK(sim->GetTotalFunds() == 7500);
    CHECK(sim->GetTaxRate(0) == 7.0f && sim->GetTaxRate(11) == 7.0f);
    CHECK(svc.log.size() >= 4 && svc.log[0] == "sim" && svc.log[1] == "acq" && svc.log[2] == "cheat" && svc.log.back() == "nodes");
    CHECK(svc.subs.size() == 4 && svc.subs[0] == 0x26d31ec4 && svc.subs[3] == 0x08dac3e7);
    CHECK(svc.resets.size() == 12 && svc.resets[0].first == 0xaabaaa9f && svc.resets[11].first == 0xaabaaaaa && svc.resets[5].second == 365);
    CHECK(!env.history.empty() && env.history.back().first == kBudgetHistoryFunds && env.history.back().second == 7);   // 7500/1000
    const std::size_t logSize = svc.log.size();
    CHECK(sim->Init() && svc.log.size() == logSize);              // second Init is a no-op

    // registry callbacks
    std::int64_t v = 0;
    cSC4BudgetSimulator::sExchangeFunds(sim, v, false);
    CHECK(v == 7500);
    v = 1234;
    cSC4BudgetSimulator::sExchangeFunds(sim, v, true);
    CHECK(sim->GetTotalFunds() == 1234);
    std::int64_t amt = 66;
    cSC4BudgetSimulator::sDepositFunds(sim, &amt);
    CHECK(sim->GetTotalFunds() == 1300);
    cSC4BudgetSimulator::sDepositFunds(sim, nullptr);
    CHECK(sim->GetTotalFunds() == 1300);

    // DoMessage: cheats
    BudgetMessage m; m.type = 0x230e27ac;
    m.data1 = 0x493f4fe6; m.text = "setfunds 4242";
    CHECK(sim->DoMessage(m) && sim->GetTotalFunds() == 4242);
    m.text = "setfunds";
    CHECK(!sim->DoMessage(m) && sim->GetTotalFunds() == 4242);
    m.data1 = 0x4ab87766;
    CHECK(sim->DoMessage(m) && sim->GetTotalFunds() == 5242);
    m.data1 = 0xdeadbeef;
    CHECK(!sim->DoMessage(m));
    m.data1 = 0xcbf31943;
    svc.rnd = 0.99;                                              // counter 1: chance 1-1/1.2 = 0.1667 -> no disaster
    CHECK(sim->DoMessage(m) && sim->GetTotalFunds() == 15242 && svc.spawned == 0);
    svc.rnd = 0.0;                                               // counter 2: spawn at x = 128*0.125*3 = 48, z = 64*0.125*3 = 24
    CHECK(sim->DoMessage(m) && sim->GetTotalFunds() == 25242 && svc.spawned == 1 && svc.lastX == 48 && svc.lastZ == 24);
    svc.rnd = 0.0;                                               // the counter was reset: counter 1 again, rnd 0 < 0.1667
    CHECK(sim->DoMessage(m) && svc.spawned == 2);
    svc.spawnOk = false;
    CHECK(sim->DoMessage(m) && svc.spawned == 3);                // counter stays (2 -> 3 now)
    svc.cheats = false;
    const std::int64_t before = sim->GetTotalFunds();
    CHECK(sim->DoMessage(m) && sim->GetTotalFunds() == before);

    // new city message and others
    BudgetMessage n; n.type = 0x26d31ec4;
    env.difficulty = 2;
    CHECK(sim->DoMessage(n) && sim->GetTotalFunds() == 9000 && sim->GetTaxRate(3) == 9.0f);
    env.messages.clear();
    n.type = 0x66956810;
    CHECK(sim->DoMessage(n) && env.messages.size() == 1);
    env.history.clear();
    n.type = 0x66956814;
    sim->SetTaxRate(1, 2.25f);                                   // 22.5 rounds to even: 22
    sim->SetTaxRate(2, 2.4f);
    CHECK(sim->DoMessage(n) && env.history.size() == 12 && env.history[0].first == 0xaabaaa9f);
    CHECK(env.history[0].second == 90 && env.history[1].second == 22 && env.history[2].second == 24);
    n.type = 0x29222f02; n.data1 = 0x892230f2;
    CHECK(sim->DoMessage(n) && sim->expenseView && svc.dialogs == 1 && svc.lastExpense);
    n.data1 = 0xc92230ee;
    CHECK(sim->DoMessage(n) && !sim->expenseView && svc.dialogs == 2 && !svc.lastExpense);
    n.data1 = 1;
    CHECK(sim->DoMessage(n) && svc.dialogs == 2);
    n.type = 0x12345;
    CHECK(sim->DoMessage(n));

    // Shutdown order
    svc.log.clear();
    sim->Shutdown();
    CHECK(svc.log.size() == 4 && svc.log[0] == "unnodes" && svc.log[1] == "uncheat" && svc.log[2] == "rel" && svc.log[3] == "unsim");
    CHECK(svc.unsubs.size() == 4);
    sim->Init();                                                 // can be initialised again
    CHECK(svc.log.back() == "nodes");
    sim->Release();

    cSC4BudgetSimulator::SetServices(nullptr);
    std::printf(failures ? "FAILED\n" : "ok\n");
    return failures ? 1 : 0;
}
