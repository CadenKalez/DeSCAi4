#include <cstdio>
#include <map>

#include "sc4/OrdinanceTypes.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace sc4;

namespace {
class Host final : public IOrdinanceHost {
public:
    int year = 2000, base = 1000;
    cISCResExemplar* LoadExemplar(const cGZPersistResourceKey&) override { return nullptr; }
    int CurrentYear() override { return year; }
    int ResidentialIncomeBase() override { return base; }
    std::string LoadString(const cGZPersistResourceKey&) override { return ""; }
    void PostMessage(std::uint32_t, std::uint32_t, std::uint32_t) override {}
};
class SimHost final : public IOrdinanceSimHost {
public:
    std::map<std::uint64_t, std::int64_t> items;
    std::map<std::uint32_t, std::int64_t> totals;
    std::uint32_t used = 0; int plants = 0, police = 0; float pollution = 0;
    bool BudgetLineItem(std::uint32_t d, std::uint32_t i, std::int64_t& v) override {
        auto it = items.find((std::uint64_t(d) << 32) | i);
        if (it == items.end()) return false;
        v = it->second; return true;
    }
    bool BudgetDepartmentTotal(std::uint32_t d, std::int64_t& v) override {
        auto it = totals.find(d);
        if (it == totals.end()) return false;
        v = it->second; return true;
    }
    bool LandfillCapacity(std::uint32_t& t, std::uint32_t& u) override { t = 100; u = used; return true; }
    int PowerPlantsOfType(int type) override { return type == 3 ? plants : 0; }
    int PoliceStationCount() override { return police; }
    float PollutionAtLevel() override { return pollution; }
};
template <class T> T* Make(float factor, std::int64_t constant) {
    auto* o = new T();
    o->AddRef(); o->ForceIncomes(0, 0, constant, factor, 1900); o->SetEnabled(true);
    return o;
}
}  // namespace

int main() {
    Host host; SimHost sim;
    SetOrdinanceHost(&host);
    SetOrdinanceSimHost(&sim);
    auto* clean = Make<cSC4OrdinanceCleanAir>(0.5f, 50);
    CHECK(clean->GetGZCLSID() == 0xa2bf1e43 && clean->GetMonthlyConstantIncome() == 50);
    CHECK(clean->GetCurrentMonthlyIncome() == 550);       // base 1000 * 0.5 + 50
    clean->Release();

    auto* clinics = Make<cSC4OrdinanceFreeClinics>(2.0f, 10);
    CHECK(clinics->GetCurrentMonthlyIncome() == 10);       // department missing
    sim.items[(std::uint64_t(0x9188f42) << 32) | 0x3110000] = 100;
    CHECK(clinics->GetCurrentMonthlyIncome() == 210);
    clinics->Release();

    auto* sports = Make<cSC4OrdinanceJuniorSportsLeague>(1.0f, 0);
    CHECK(!sports->CheckConditions());                     // total is zero
    sim.totals[0x8a4260a6] = 70;
    CHECK(sports->CheckConditions() && sports->GetCurrentMonthlyIncome() == 70);
    sports->Release();

    auto* gas = Make<cSC4OrdinanceLandfillGasRecovery>(3.0f, 5);
    CHECK(!gas->CheckConditions());                        // nothing in the landfill
    sim.used = 10; sim.plants = 1;
    CHECK(gas->CheckConditions());
    gas->Simulate();
    CHECK(gas->GetMonthlyAdjustedIncome() == 35);
    sim.plants = 0;
    CHECK(!gas->CheckConditions());
    gas->Release();

    auto* gamble = Make<cSC4OrdinanceLegalizeGambling>(9.0f, 123);
    CHECK(gamble->GetCurrentMonthlyIncome() == 123);
    gamble->Release();

    auto* watch = Make<cSC4OrdinanceNeighborhoodWatch>(0, 0);
    CHECK(!watch->CheckConditions());
    sim.police = 4;
    CHECK(watch->CheckConditions());
    watch->Release();

    auto* reading = Make<cSC4OrdinanceProReadingCampaign>(0.5f, 1);
    sim.items[(std::uint64_t(0x9188f4c) << 32) | 0x3190000] = 40;
    sim.items[(std::uint64_t(0x9188f4c) << 32) | 0x34a000] = 60;
    CHECK(reading->GetCurrentMonthlyIncome() == 51);
    reading->Release();

    auto* tourist = Make<cSC4OrdinanceTouristPromotion>(0.5f, 7);
    CHECK(tourist->GetCurrentMonthlyIncome() == 7);
    sim.totals[0x8a416368] = 20;
    tourist->Simulate();
    CHECK(tourist->GetMonthlyAdjustedIncome() == 17);
    tourist->Release();

    auto* trash = Make<cSC4OrdinanceTrashPresort>(1.0f, 0);
    sim.totals[0x6a42a8f5] = 9;
    CHECK(trash->GetCurrentMonthlyIncome() == 9 && !trash->CheckConditions());
    sim.pollution = 0.2f;
    CHECK(trash->CheckConditions());
    trash->Release();
    std::printf(failures ? "FAILED\n" : "sc4_ordinancetypes ok\n");
    return failures != 0;
}
