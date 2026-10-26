#include <cmath>
#include <cstdio>
#include <vector>

#include "rz/PortMemoryStreams.h"
#include "sc4/cSC4Demand.h"

using namespace sc4;
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

struct Host final : IDemandHost {
    struct M { std::uint32_t id, a; std::int32_t b; };
    std::vector<M> msgs;
    void PostMessage(std::uint32_t m, std::uint32_t a, std::int32_t b) override { msgs.push_back({m, a, b}); }
};

int main() {
    Host host;
    cSC4Demand d(&host);
    d.SetId(0x1010);
    d.SetActiveDemandMax(100.0f);
    d.SetActiveDemandMin(-100.0f);
    CHECK(d.QueryActiveDemandMax() == 100.0f && d.QueryActiveDemandMin() == -100.0f);
    d.SetActiveDemandMax(-500.0f);                 // cannot go below the minimum
    CHECK(d.QueryActiveDemandMax() == -100.0f);
    d.SetActiveDemandMax(100.0f);
    d.SetActiveDemandMin(500.0f);                  // cannot exceed the maximum
    CHECK(d.QueryActiveDemandMin() == 100.0f);
    d.SetActiveDemandMin(-100.0f);

    // demand 60 over supply 10 -> +50 (no modifiers)
    d.SetDemandValue(60.0f);
    d.SetSupplyValue(10.0f);
    d.CalculateActiveDemand(false);
    CHECK(d.QueryActiveDemandValue() == 50.0f);
    CHECK(host.msgs.back().id == kDemandChangedMessage && host.msgs.back().a == 0x1010 && host.msgs.back().b == 500);

    // modifiers add min(max, demand) * (economy + tax), then clamp to the range
    d.SetEconomyModifier(0.5f);
    d.SetTaxModifier(-0.25f);
    d.CalculateActiveDemand(false);               // 50 + min(25, 15) = 65
    CHECK(d.QueryActiveDemandValue() == 65.0f);

    // demand cap lowers the ceiling to max * (1 - cap)
    d.SetDemandCap(0.4f);
    d.CalculateActiveDemand(false);
    CHECK(std::fabs(d.QueryActiveDemandValue() - 60.0f) < 1e-4f);
    d.SetDemandCap(0.0f);

    // negative demand scales by the (negative) minimum, so the message is negative too
    d.SetEconomyModifier(0); d.SetTaxModifier(0);
    d.SetDemandValue(0.0f);
    d.SetSupplyValue(40.0f);
    d.CalculateActiveDemand(false);
    CHECK(d.QueryActiveDemandValue() == -40.0f && host.msgs.back().b == -400);

    // smoothing: 7% of the span (14) per step; zero is snapped to, not crossed
    d.SetDemandValue(100.0f);
    d.SetSupplyValue(0.0f);
    d.CalculateActiveDemand(false);               // target +100
    d.CalculateActiveDemand(true);                // from +100: no change needed
    CHECK(d.QueryActiveDemandValue() == 100.0f);
    d.SetDemandValue(0.0f); d.SetSupplyValue(0.0f);
    d.CalculateActiveDemand(true);                // target 0, step 14
    CHECK(std::fabs(d.QueryActiveDemandValue() - 86.0f) < 1e-4f);
    d.SetSupplyValue(100.0f);                     // target -100 while at +86
    d.CalculateActiveDemand(true);                // would cross zero in many steps: snaps to 0 now
    CHECK(d.QueryActiveDemandValue() == 0.0f);
    d.CalculateActiveDemand(true);
    CHECK(std::fabs(d.QueryActiveDemandValue() + 14.0f) < 1e-4f);

    // end of cycle folds the new values in
    d.SetSupplyValue(0); d.SetDemandValue(0);
    d.AddToSupplyValue(5); d.AddToDemandValue(20);
    CHECK(d.EndOfCycle());
    CHECK(d.QuerySupplyValue() == 5 && d.QueryDemandValue() == 20 && d.QueryNewSupply() == 0 && d.QueryNewDemand() == 0);
    CHECK(!d.EndOfCycle());

    // serialization (v2) and legacy v1 percentage
    d.SetEconomyModifier(0.3f);
    d.SetDemandCap(0.2f);
    PortMemoryOStream out;
    CHECK(d.Write(out));
    cSC4Demand r(&host);
    PortMemoryIStream in(out.bytes().data(), out.bytes().size());
    CHECK(r.Read(in));
    CHECK(r.GetId() == 0x1010 && r.QuerySupplyValue() == 5 && r.GetEconomyModifier() == 0.3f &&
          std::fabs(r.GetDemandCap() - 0.2f) < 1e-6f && r.GetGZCLSID() == 0x8990c12d);
    std::vector<unsigned char> v1;
    auto put16 = [&](std::uint16_t x) { v1.push_back(x & 255); v1.push_back(x >> 8); };
    auto put32 = [&](std::uint32_t x) { for (int i = 0; i < 4; ++i) v1.push_back((x >> (8 * i)) & 255); };
    put16(1); put32(7);
    for (int i = 0; i < 9; ++i) put32(0);          // supply .. active
    v1.push_back(0);                               // dirty
    put32(0); put32(0);                            // economy and tax modifiers
    v1.push_back(25);                              // 25% -> 0.25
    PortMemoryIStream in1(v1.data(), v1.size());
    cSC4Demand legacy(&host);
    CHECK(legacy.Read(in1) && std::fabs(legacy.GetDemandCap() - 0.25f) < 1e-6f && legacy.GetId() == 7);
    std::vector<unsigned char> v3 = {3, 0};
    PortMemoryIStream in3(v3.data(), v3.size());
    CHECK(!legacy.Read(in3));                      // newer versions are refused

    if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::puts("sc4_demand ok");
    return 0;
}
