#include <cmath>
#include <cstdio>
#include <map>

#include "sc4/ResidentialSimulator.h"
#include "sc4/SCProperty.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

using namespace sc4;

namespace {

struct Occ : IResidentialOccupant {
    cSCPropertyHolder props;
    std::vector<std::uint32_t> groups;
    cISCPropertyHolder* PropertyHolder() override { return &props; }
    bool IsInGroup(std::uint32_t g) override { return std::find(groups.begin(), groups.end(), g) != groups.end(); }
};

struct Record : IBudgetRecord {
    bool explicitEntry = false;
    float def = 0.8f;
    std::map<std::uint32_t, float> set;
    bool HasExplicitFunding(cISCPropertyHolder*, std::uint32_t) override { return explicitEntry; }
    float DefaultFunding() override { return def; }
    void SetFunding(cISCPropertyHolder*, const SC4Percentage& v, std::uint32_t id) override { set[id] = v.value; }
};

struct Host : IResidentialHost {
    std::uint32_t day = 100;
    struct Msg { std::uint32_t id; bool begin; std::uint32_t days; };
    std::vector<Msg> messages;
    std::vector<std::array<int, 2>> strikerCalls;   // {education, add}
    bool occupantManager = true;
    bool budgeted = true;
    Record record, recordB;
    bool nullRecord = false;
    std::map<IResidentialOccupant*, ResidentialQueryData> school, hospital;
    std::uint8_t cellWorkforce = 100, globalWorkforce = 50;
    float life = 90.0f;
    float staffRating = 0.5f;
    std::uint32_t CurrentDay() override { return day; }
    void PostStrikeMessage(std::uint32_t id, IResidentialOccupant*, bool begin, std::uint32_t days) override { messages.push_back({id, begin, days}); }
    void SetStrikerGroup(bool education, IResidentialOccupant*, bool add) override { strikerCalls.push_back({education, add}); }
    bool CityHasOccupantManager() override { return occupantManager; }
    bool IsBudgeted(IResidentialOccupant*, std::uint32_t) override { return budgeted; }
    IBudgetRecord* GetBudgetRecord(IResidentialOccupant*, std::uint32_t id) override {
        if (nullRecord) return nullptr;
        return (id == 0xea5654b6 || id == 0xca565486) ? static_cast<IBudgetRecord*>(&record) : &recordB;
    }
    void GetSchoolQueryData(IResidentialOccupant* o, ResidentialQueryData& out) override { out = school[o]; }
    void GetHospitalQueryData(IResidentialOccupant* o, ResidentialQueryData& out) override { out = hospital[o]; }
    void GetOccupantEfficiencyRatings(IResidentialOccupant*, float& staff, bool) override { staff = staffRating; }
    std::uint8_t CellWorkforce(unsigned, unsigned) override { return cellWorkforce; }
    std::uint8_t GlobalWorkforce() override { return globalWorkforce; }
    float CellLifeExpectancyBase(unsigned, unsigned) override { return life; }
};

}  // namespace

int main() {
    Host host;
    cRZRandom random(777);
    cSC4ResidentialSimulator sim(&host, &random);
    CHECK(kGZCLSID_cSC4ResidentialSimulator == 0x4990c013);

    // ---- default tunables
    CHECK(sim.Health().minimumFunding == 0.6f && sim.Health().strikeThreshold == 1.2f && sim.Health().strikeDays == 10);
    CHECK(sim.Health().strikeEndFunding == 0.9f && sim.Health().messageId == 0x077accd2 && sim.Education().messageId == 0x077accd0);
    CHECK(sim.Health().staffCurve.GetOutputValue(120.0f) == 110.0f && sim.Health().strikeChance.GetOutputValue(0.0f) == 20.0f);
    CHECK(sim.wealthCurves[8].GetOutputValue(100.0f) == 100.0f);
    CHECK(sim.value520 == 30 && sim.value524 == 30 && sim.value568 == 1.1f && sim.value560 == 10.0f);

    // ---- tunables from an exemplar
    cSCPropertyHolder ex;
    cSCPropertyHelp::SetPropertyValue(&ex, 0xe918946f, 50.0f);                                        // health min funding 50%
    cSCPropertyHelp::SetPropertyValue(&ex, 0xa9189479, std::vector<float>{30.0f, 150.0f});             // value90 / threshold
    cSCPropertyHelp::SetPropertyValue(&ex, 0x8918948b, std::int32_t(7));
    cSCPropertyHelp::SetPropertyValue(&ex, 0x691894a4, std::vector<float>{0.0f, 5.0f, 100.0f, 50.0f}); // health strike chance
    cSCPropertyHelp::SetPropertyValue(&ex, 0xc91894a8, 40.0f);                                        // education min funding
    cSCPropertyHelp::SetPropertyValue(&ex, 0x8a891421, std::uint32_t(12));
    cSCPropertyHelp::SetPropertyValue(&ex, 0xec43826b, std::vector<std::uint32_t>{1, 2, 3});
    cSCPropertyHelp::SetPropertyValue(&ex, 0xea540623, std::vector<float>{0.0f, 1.0f, 10.0f, 2.0f});   // wealth curve 2
    sim.ReadSimulatorTunableValues(&ex);
    CHECK(std::fabs(sim.Health().minimumFunding - 0.5f) < 1e-6f && std::fabs(sim.Education().minimumFunding - 0.4f) < 1e-6f);
    CHECK(std::fabs(sim.Health().value90 - 0.3f) < 1e-6f && std::fabs(sim.Health().strikeThreshold - 1.5f) < 1e-6f);
    CHECK(sim.Health().strikeDays == 7 && sim.Education().strikeDays == 10);
    CHECK(sim.Health().strikeChance.GetOutputValue(0.0f) == 5.0f && sim.Education().strikeChance.GetOutputValue(0.0f) == 20.0f);
    CHECK(sim.value520 == 12 && sim.value524 == 30 && sim.list528.size() == 3 && sim.list528[2] == 3);
    CHECK(sim.wealthCurves[2].GetOutputValue(10.0f) == 2.0f && sim.wealthCurves[3].GetOutputValue(100.0f) == 100.0f);
    sim.ReadSimulatorTunableValues(nullptr);       // back to defaults
    CHECK(sim.Health().minimumFunding == 0.6f && sim.Health().strikeDays == 10);

    // ---- building lists
    std::vector<Occ> occs(6);
    occs[0].groups = {0x1a03}; occs[1].groups = {0x1a03, 7}; occs[2].groups = {0x1a02}; occs[3].groups = {0x1a02}; occs[4].groups = {9};
    sim.SchoolLists()[0] = {&occs[0], &occs[1]};
    sim.SchoolLists()[3] = {&occs[4]};
    sim.ExtraSchools() = {&occs[5]};
    sim.HospitalLists()[1] = {&occs[2], &occs[3]};
    sim.ExtraHospitals() = {&occs[5]};
    CHECK(sim.GetSchoolBuildingCount() == 4 && sim.GetHospitalBuildingCount() == 3);
    {
        std::list<IResidentialOccupant*> out;
        sim.GetSchoolBuildings(out, nullptr);
        CHECK(out.size() == 4);
        out.clear();
        std::vector<std::uint32_t> filter{7, 9};
        sim.GetSchoolBuildings(out, &filter);
        CHECK(out.size() == 2 && out.front() == &occs[1] && out.back() == &occs[4]);
        out.clear();
        sim.HealthAffecting1() = {&occs[0]};
        sim.HealthAffecting2() = {&occs[1], &occs[2]};
        sim.GetHealthAffectingBuildings(out);
        CHECK(out.size() == 3 && out.front() == &occs[0]);
    }

    // ---- chance of strike: independent events combine as 1 - prod(1 - p)
    {
        // health chance curve default (0,20)-(100,100): funding 0 -> 20%, 0.5 -> 60%
        cSCPropertyHelp::SetPropertyValue(&occs[2].props, 0x092453cb, 0.0f);
        cSCPropertyHelp::SetPropertyValue(&occs[3].props, 0x092453cb, 0.5f);
        const float c = sim.ChanceOfHealthStrike();
        CHECK(std::fabs(c - (1.0f - 0.8f * 0.4f)) < 1e-5f);
        // an occupant outside the group repeats the previous chance
        std::list<IResidentialOccupant*> mixed = {&occs[3], &occs[0]};
        const float m = sim.GetChanceOfStrike(sim.Health(), mixed, 0x092453cb, 0x1a02);
        CHECK(std::fabs(m - (1.0f - 0.4f * 0.4f)) < 1e-5f);
        CHECK(sim.GetChanceOfStrike(sim.Health(), {}, 0x092453cb, 0x1a02) == 0.0f);
        sim.Health().onStrike = true;
        CHECK(sim.ChanceOfHealthStrike() == 1.0f && sim.GetChanceOfStrike(sim.Health(), mixed, 0x092453cb, 0x1a02) == 0.0f);
        sim.Health().onStrike = false;
        cSCPropertyHelp::SetPropertyValue(&occs[0].props, 0x292453c5, 1.0f);
        cSCPropertyHelp::SetPropertyValue(&occs[1].props, 0x292453c5, 1.0f);
        CHECK(std::fabs(sim.ChanceOfSchoolStrike() - 1.0f) < 1e-6f);   // funding 1.0 -> 100% on the default curve
    }

    // ---- begin / end strike
    {
        host.messages.clear();
        host.day = 200;
        CHECK(sim.BeginStrike(sim.Education(), &occs[0]) && sim.SchoolIsOnStrike() && sim.Education().strikeStartDay == 200);
        CHECK(!sim.BeginStrike(sim.Education(), &occs[0]));
        CHECK(host.messages.size() == 1 && host.messages[0].id == 0x077accd0 && host.messages[0].begin);
        host.day = 215;
        CHECK(sim.EndSchoolStrike() && !sim.SchoolIsOnStrike());
        CHECK(host.messages.size() == 2 && !host.messages[1].begin && host.messages[1].days == 15);
        CHECK(!sim.EndSchoolStrike() && !sim.EndHealthStrike());
    }

    // ---- per-occupant striking state
    {
        Occ o;
        o.groups = {0x1a02};
        host.strikerCalls.clear();
        host.day = 300;
        cSCPropertyHelp::SetPropertyValue(&o.props, 0x092453cb, 0.0f);     // idle funding property used when not striking
        sim.Health().strikeChance.SetResponseCurveData(std::vector<SC4Point>{{0.0f, 1000.0f}, {100.0f, 1000.0f}});   // always strike
        CHECK(sim.UpdateStrikingState(&o, sim.Health(), 0x092453cb, 0x092453cb));
        std::uint32_t start = 0;
        cSCPropertyHelp::GetPropertyValue(&o.props, 0xeaa69f83, start);
        CHECK(start == 300 && host.strikerCalls.size() == 1 && host.strikerCalls[0][0] == 0 && host.strikerCalls[0][1] == 1);
        // still striking: funding below the end threshold and within the allowed days
        host.day = 305;
        cSCPropertyHelp::SetPropertyValue(&o.props, 0x092453cb, 0.5f);
        CHECK(!sim.UpdateStrikingState(&o, sim.Health(), 0x092453cb, 0x092453cb));
        // funding recovers to the end threshold -> strike ends
        cSCPropertyHelp::SetPropertyValue(&o.props, 0x092453cb, 0.9f);
        CHECK(sim.UpdateStrikingState(&o, sim.Health(), 0x092453cb, 0x092453cb));
        cSCPropertyHelp::GetPropertyValue(&o.props, 0xeaa69f83, start);
        CHECK(start == 0 && host.strikerCalls.size() == 2 && host.strikerCalls[1][1] == 0);
        // strike lasting longer than the day limit ends even with poor funding
        cSCPropertyHelp::SetPropertyValue(&o.props, 0xeaa69f83, std::uint32_t(100));
        cSCPropertyHelp::SetPropertyValue(&o.props, 0x092453cb, 0.1f);
        host.day = 110;
        CHECK(!sim.UpdateStrikingState(&o, sim.Health(), 0x092453cb, 0x092453cb));   // 10 days: still within limit
        host.day = 111;
        CHECK(sim.UpdateStrikingState(&o, sim.Health(), 0x092453cb, 0x092453cb));
        // education department uses the education striker group
        Occ e;
        e.groups = {0x1a03};
        sim.Education().strikeChance.SetResponseCurveData(std::vector<SC4Point>{{0.0f, 1000.0f}, {100.0f, 1000.0f}});
        host.strikerCalls.clear();
        cSCPropertyHelp::SetPropertyValue(&e.props, 0x292453c5, 0.0f);
        CHECK(sim.UpdateStrikingState(&e, sim.Education(), 0x292453c5, 0x292453c5));
        CHECK(host.strikerCalls.size() == 1 && host.strikerCalls[0][0] == 1 && host.strikerCalls[0][1] == 1);
        host.occupantManager = false;
        host.strikerCalls.clear();
        cSCPropertyHelp::SetPropertyValue(&e.props, 0xeaa69f83, std::uint32_t(0));
        CHECK(sim.UpdateStrikingState(&e, sim.Education(), 0x292453c5, 0x292453c5) && host.strikerCalls.empty());
        host.occupantManager = true;
    }

    // ---- monthly strike check
    {
        sim.Health().onStrike = false;
        host.messages.clear();
        host.day = 400;
        Occ a, b;
        a.groups = b.groups = {0x1a02};
        cSCPropertyHelp::SetPropertyValue(&a.props, 0x092453cb, 0.0f);
        cSCPropertyHelp::SetPropertyValue(&b.props, 0x092453cb, 0.0f);
        std::vector<std::list<IResidentialOccupant*>> lists(2);
        lists[0] = {&a};
        lists[1] = {&b};
        sim.Health().strikeChance.SetResponseCurveData(std::vector<SC4Point>{{0.0f, 1000.0f}, {100.0f, 1000.0f}});
        sim.DoMonthlyStrikeCheck(sim.Health(), lists, 0x092453cb, 0x092453cb, 0x1a02);
        CHECK(sim.HealthIsOnStrike());
        CHECK(host.messages.size() == 1 && host.messages[0].begin);    // one strike begins even with two strikers
        // everyone recovers -> the department strike ends
        cSCPropertyHelp::SetPropertyValue(&a.props, 0x092453cb, 1.0f);
        cSCPropertyHelp::SetPropertyValue(&b.props, 0x092453cb, 1.0f);
        host.day = 405;
        sim.DoMonthlyStrikeCheck(sim.Health(), lists, 0x092453cb, 0x092453cb, 0x1a02);
        CHECK(!sim.HealthIsOnStrike() && host.messages.size() == 2 && !host.messages[1].begin && host.messages[1].days == 5);
        std::vector<std::list<IResidentialOccupant*>> none;
        sim.DoMonthlyStrikeCheck(sim.Health(), none, 1, 2, 3);
    }

    // ---- funding percentages
    {
        Occ o;
        SC4Percentage staff{0.3f}, budget{0.4f}, outA, outB;
        host.record.set.clear(); host.recordB.set.clear();
        sim.SetOccupantFundingPercentages(&o, staff, budget, true);
        CHECK(host.record.set.count(0xea5654b6) && host.record.set[0xea5654b6] == 0.3f);
        CHECK(host.recordB.set.count(0x4a5654ba) && host.recordB.set[0x4a5654ba] == 0.4f);
        sim.SetOccupantFundingPercentages(&o, staff, budget, false);
        CHECK(host.record.set.count(0xca565486) && host.recordB.set.count(0xea56549e));
        host.record.explicitEntry = false;
        sim.GetOccupantFundingPercentages(&o, outA, outB, true);
        CHECK(outA.value == 0.8f && outB.value == 0.8f);
        host.record.explicitEntry = true;
        sim.GetRawOccupantFundingPercentages(&o, outA, outB, false);
        CHECK(outA.value == 0.0f && outB.value == 0.8f);
        host.budgeted = false;
        sim.GetOccupantFundingPercentages(&o, outA, outB, false);
        CHECK(outA.value == 1.0f && outB.value == 1.0f);
        host.budgeted = true;
        host.nullRecord = true;
        outA.value = outB.value = 7.0f;
        sim.GetOccupantFundingPercentages(&o, outA, outB, true);
        CHECK(outA.value == 7.0f && outB.value == 7.0f);        // untouched when the record is missing
        host.nullRecord = false;
        // staff efficiency goes through the department curve only when the occupant is budgeted
        host.staffRating = 1.2f;
        CHECK(std::fabs(sim.GetBaseStaffEfficiency(&o, true) - 1.1f) < 1e-5f);   // curve(120) = 110
        host.budgeted = false;
        CHECK(sim.GetBaseStaffEfficiency(&o, true) == 1.2f);
        host.budgeted = true;
        // auto budget flag
        CHECK(!cSC4ResidentialSimulator::GetAutoBudget(&o));
        cSC4ResidentialSimulator::SetAutoBudget(&o, true);
        CHECK(cSC4ResidentialSimulator::GetAutoBudget(&o));
        sim.SetGlobalAutoBudgetForSchools(true);
        CHECK(sim.GetGlobalAutoBudgetForSchools() && !sim.GetGlobalAutoBudgetForHospitals());
    }

    // ---- system totals and ratings
    {
        host.school[&occs[0]] = {"a", 1, 10, 2, 3, 4, 0.5f};
        host.school[&occs[1]] = {"b", 5, 20, 6, 7, 8, 1.5f};
        host.school[&occs[4]] = {"c", 9, 30, 10, 11, 12, 2.0f};
        ResidentialQueryData totals;
        totals.field28 = 99;
        std::vector<std::uint32_t> groups{0x1a03, 9};
        CHECK(sim.GetSchoolSystemTotals(totals, groups) == 3);
        CHECK(totals.field28 == 15 && totals.served == 60 && totals.field30 == 18 && totals.field34 == 21 && totals.field38 == 24 && totals.field3c == 4.0f);
        CHECK(sim.GetSchoolSystemTotals(totals, {}) == 0 && totals.served == 0);
        sim.SetPopulation(120);
        CHECK(std::fabs(sim.GetSchoolSystemRating() - 0.5f) < 1e-6f);
        sim.SetPopulation(40);
        CHECK(sim.GetSchoolSystemRating() == 1.0f);
        sim.SetPopulation(0);
        CHECK(sim.GetSchoolSystemRating() == 1.0f);
        host.hospital[&occs[2]] = {"h", 0, 0, 0, 0, 0, 0};
        CHECK(sim.GetHealthSystemRating() == 0.0f);
        CHECK(sim.GetPopulation() == 0 && sim.GetMaxEQ() == 200 && sim.GetMaxHQ() == 200);
    }

    // ---- exemplar-driven queries
    {
        CHECK(cSC4ResidentialSimulator::GetAverageNewAgeByWealth(nullptr, 1) == 25 && cSC4ResidentialSimulator::GetAverageNewAgeByWealth(nullptr, 2) == 30);
        cSCPropertyHolder e2;
        cSCPropertyHelp::SetPropertyValue(&e2, 0x0929cc36, std::uint8_t(44));
        CHECK(cSC4ResidentialSimulator::GetAverageNewAgeByWealth(&e2, 3) == 44 && cSC4ResidentialSimulator::GetAverageNewAgeByWealth(&e2, 1) == 25);
        host.cellWorkforce = 100;
        CHECK(std::fabs(sim.GetCellWorkforcePercent(&e2, 1, 1) - 0.5f) < 1e-6f);      // default curve (0,0)-(200,100)
        host.globalWorkforce = 200;
        CHECK(std::fabs(sim.GetGlobalWorkforcePercent(&e2) - 1.0f) < 1e-6f);
        CHECK(sim.GetCellWorkforcePercent(nullptr, 1, 1) == 0.0f);
        host.life = 90.0f;
        CHECK(sim.GetCellLifeExpectancy(&e2, 0, 0) == 90);
        cSCPropertyHelp::SetPropertyValue(&e2, 0x6941a67f, std::vector<float>{0.0f, 0.0f, 100.0f, 255.0f});
        host.life = 100.0f;
        CHECK(sim.GetCellLifeExpectancy(&e2, 0, 0) == 255);
        host.life = 1000.0f;
        CHECK(sim.GetCellLifeExpectancy(&e2, 0, 0) == 255);
    }

    if (failures) return 1;
    std::printf("sc4_residentialsimulator: all passed\n");
    return 0;
}
