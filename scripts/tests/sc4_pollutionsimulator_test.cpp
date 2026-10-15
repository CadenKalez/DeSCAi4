#include <cmath>
#include <cstdio>

#include "sc4/PollutionSimulator.h"
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace sc4;
namespace {
class City final : public ISimGridCity {
public:
    std::int32_t CitySizeXInCells() const override { return 64; }
    std::int32_t CitySizeZInCells() const override { return 64; }
    float CellSizeXMeters() const override { return 16.0f; }
    float CellSizeZMeters() const override { return 16.0f; }
};
class Occupant final : public IPollutionOccupant {
public:
    cSCPropertyHolder holder;
    bool powered = true;
    cISCPropertyHolder* PropertyHolder() override { return &holder; }
    void Position(float p[3]) override { p[0] = p[1] = p[2] = 0; }
};
class Host final : public IPollutionHost {
public:
    bool power = true, poweredOccupants = true;
    float funding = 0.5f, slots = 2.0f;
    int low = 0, high = 100, load = 5;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> messages;
    std::vector<std::pair<std::uint32_t, std::int64_t>> history;
    bool PowerAvailable() override { return power; }
    bool OccupantPowered(IPollutionOccupant*) override { return poweredOccupants; }
    float SanitationFunding() override { return funding; }
    float RecyclingCapacitySlots() override { return slots; }
    void LandfillRange(int& l, int& h) override { l = low; h = high; }
    int LandfillTransportLoad() override { return load; }
    void PostMessage(std::uint32_t m, std::uint32_t a) override { messages.push_back({m, a}); }
    void AddHistory(std::uint32_t id, std::int64_t v) override { history.push_back({id, v}); }
};
}  // namespace

int main() {
    City city;
    SetSimGridCity(&city);
    Host host;
    cSC4PollutionSimulator sim(&host);
    sim.CreateGrids();
    sim.ReadSimulatorTunableValues(nullptr);
    CHECK(sim.Tunables().airThreshold == 16000 && sim.Tunables().landfillTileCapacity == 0x7fff);
    CHECK(sim.GetGZCLSID() == 0x8990c065 && sim.GetRequesterSatisfactionId() == 0xe9a4300b);

    // grids
    sim.SetAirValue(10, 12, 20000);
    int v = 0;
    sim.GetAirValue(10, 12, v);
    CHECK(v == 20000 && sim.IsAirPolluted(10, 12) && !sim.IsAirPolluted(40, 40));
    sim.SetWaterValue(4, 4, 100);
    CHECK(!sim.IsWaterPolluted(4, 4));
    sim.SetGarbageValue(6, 6, 17000);
    CHECK(sim.IsGarbageAccumulated(6, 6));
    sim.SetIsActiveLandfill(6, 6, true, 0xe);                      // quirk: writes tract (6,6) directly
    CHECK(sim.activeLandfillCount_ == 1);
    sim.SetIsActiveLandfill(6, 6, true, 0x3);                      // not a landfill zone: ignored
    CHECK(sim.activeLandfillCount_ == 1);
    sim.SetIsRadioactive(2, 2, true);
    CHECK(sim.IsRadioactive(2, 2) && !sim.IsRadioactive(30, 30));
    CHECK(sim.dirtyAir && sim.dirtyRadiation);

    // garbage supply never goes negative
    sim.AddToGarbageSupply(50);
    sim.AddToGarbageSupply(-80);
    CHECK(sim.GetGarbageSupply() == 0);
    sim.AddToGarbageSupply(30);
    CHECK(sim.GetGarbageSupply() == 30);

    // occupant helpers
    Occupant incinerator;
    cSCPropertyHelp::SetPropertyValue(&incinerator.holder, kPollutionOccIncinerator, std::uint32_t{1000});
    cSCPropertyHelp::SetPropertyValue(&incinerator.holder, kPollutionOccIncineratorUsed, std::uint32_t{250});
    sim.SetEfficiency(0.5f);
    std::uint32_t out = 0;
    sim.GetIncineratorCurrentCapacity(&incinerator, out);
    CHECK(out == 500);
    sim.GetIncineratorUsedCapacity(&incinerator, out);
    CHECK(out == 250);
    CHECK(sim.GetUnusedCapacity(kPollutionOccIncinerator, kPollutionOccIncineratorUsed, &incinerator, out) && out == 250);
    host.poweredOccupants = false;
    sim.GetIncineratorCurrentCapacity(&incinerator, out);
    CHECK(out == 0);
    host.poweredOccupants = true;
    // age curve scales current capacity
    cSCPropertyHelp::SetPropertyValue(&incinerator.holder, kPollutionOccAgeCurve, std::vector<float>{0, 1.0f, 10, 0.0f});
    cSCPropertyHelp::SetPropertyValue(&incinerator.holder, kPollutionOccAge, 5.0f);
    sim.GetIncineratorCurrentCapacity(&incinerator, out);
    CHECK(out == 250);
    float cond = 0;
    CHECK(sim.GetCurrentOccupantCondition(&incinerator, cond) && std::fabs(cond - 0.5f) < 1e-6f);
    float eff = 0;
    CHECK(sim.GetCurrentOccupantEfficiency(&incinerator, eff) && std::fabs(eff - 0.25f) < 1e-6f);
    host.power = false;
    CHECK(!sim.GetCurrentOccupantEfficiency(&incinerator, eff) && eff == 0.0f);
    host.power = true;
    CHECK(std::list<IPollutionOccupant*>().empty());
    CHECK(sim.AddOrRemoveOccupant(sim.IncineratorList(), &incinerator, true) && sim.IncineratorList().size() == 1);
    CHECK(sim.AddOrRemoveOccupant(sim.IncineratorList(), &incinerator, false) && sim.IncineratorList().empty());
    CHECK(!sim.AddOrRemoveOccupant(sim.IncineratorList(), &incinerator, false));

    // eatGarbage priority chain
    sim.SetEfficiency(1.0f);
    sim.recyclingAllowance_ = 10;
    std::uint32_t wte = 30, incin = 20;
    float eaten = sim.eatGarbage(8.0f, wte, incin);
    CHECK(eaten == 8.0f && sim.recycledThisMonth_ == 8 && wte == 30 && incin == 20);
    eaten = sim.eatGarbage(10.0f, wte, incin);                    // 2 recycled, 8 incinerated
    CHECK(eaten == 10.0f && sim.recycledThisMonth_ == 10 && incin == 12 && sim.incineratedThisMonth_ == 8);
    eaten = sim.eatGarbage(100.0f, wte, incin);                   // 12>=? budget 12 <= 100: emptied, quirk
    CHECK(incin == 0 && sim.incineratedThisMonth_ == 8);
    CHECK(wte == 0);                                               // 30 <= 100: emptied by the same quirk
    // remaining goes to landfill transport (curve empty -> 0), then abduction
    CHECK(eaten == 100.0f);                                        // whole request goes to abduction (allowance 5000)
    CHECK(sim.abductedThisMonth_ == 100);
    CHECK(sim.eatGarbage(0.0f, wte, incin) == 0.0f);

    // recycling effect
    Occupant r1, r2;
    cSCPropertyHelp::SetPropertyValue(&r1.holder, kPollutionOccRecyclingFactor, 1.0f);
    cSCPropertyHelp::SetPropertyValue(&r2.holder, kPollutionOccRecyclingFactor, 1.0f);
    cSCPropertyHelp::SetPropertyValue(&r2.holder, kPollutionOccAgeCurve, std::vector<float>{0, 0.5f, 10, 0.5f});
    sim.RecyclingList().push_back(&r2);
    sim.RecyclingList().push_back(&r1);
    sim.recyclingCenterCountSnapshot = 2;
    sim.SetEfficiency(1.0f);
    sim.CalculateRecyclingCenterEffect();
    CHECK(sim.RecyclingList().size() == 1 && sim.RecyclingList().front() == &r2);  // r1 has no efficiency, dropped
    // r1 has no age curve -> condition fails -> efficiency 0; r2 0.5: r2 sorts first
    // end of cycle
    sim.airSum = 400; sim.waterSum = 200; sim.cellCount = 4; sim.radiationCount = 1;
    sim.SetTotalGarbageUncollected(40);
    sim.FundingEfficiencyCurve().SetResponseCurveData(std::vector<float>{0, 0, 100, 100}.data(), 4);
    host.messages.clear();
    host.history.clear();
    sim.EndOfCycle();
    CHECK(sim.GetAverageAirValue() == 100 && sim.GetAverageWaterValue() == 50 && sim.GetAverageGarbageValue() == 10);
    CHECK(sim.IsGarbageUncollected());
    CHECK(host.history.size() == 5 && host.history[4].second == 25 + 100);
    CHECK(std::fabs(sim.GetEfficiency() - 0.5f) < 1e-5f);
    CHECK(!sim.dirtyAir && host.messages.size() >= 3);
    std::printf(failures ? "FAILED\n" : "sc4_pollutionsimulator: all passed\n");
    return failures ? 1 : 0;
}
