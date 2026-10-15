#include <cstdio>

#include "rz/PortMemoryStreams.h"
#include "sc4/NeighborDeal.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

using namespace sc4;

namespace {
struct Host final : INeighborDealHost {
    int cities[2] = {0, 0};   // handles: &cities[0] = this city, &cities[1] = neighbour
    bool budget = true; double funds = 1000; int reports = 0, changed = 0;
    bool CityHasBudget() override { return budget; }
    double DealCost(double amount, std::uint32_t, void*) override { return amount * 2.5; }
    bool CityEntryAtCell(int x, int z, void*& city) override {
        if (x == 1 && z == 1) { city = &cities[0]; return true; }
        if (x == 2 && z == 2) { city = &cities[1]; return true; }
        return false;
    }
    void* CurrentCity() override { return &cities[0]; }
    double CurrentCityFunds() override { return funds; }
    void ReportDealStatus(bool c, cSC4NeighborDeal*) override { ++reports; changed += c; }
};
}  // namespace

int main() {
    Host host;
    SetNeighborDealHost(&host);
    auto* d = new cSC4NeighborDeal();
    d->AddRef();
    CHECK(d->GetFlags() == 0x81 && d->GetGZCLSID() == 0x2a134f42);
    d->SetType(0x4773ffe0);
    d->SetRegionCellOnCitySideOfConnection(1, 1);
    d->SetRegionCellOnOtherSideOfConnection(2, 2);
    CHECK(d->GetCityOnOtherSideOfConnection() == &host.cities[1]);
    d->SetAmount(10);                                 // silent flag (0x80) set: no report
    CHECK(d->GetAmount() == 10 && d->GetCost() == 25 && host.reports == 0);
    d->SetFlags(kDealFlagSilent, false);
    host.reports = 0;
    d->SetAmount(4);
    CHECK(d->GetCost() == 10 && host.reports == 2);   // status "amount changed" + "cost changed"
    d->SetAmount(0);
    CHECK(d->GetCost() == 1);                         // a zero cost is stored as 1
    host.budget = false;
    d->SetAmount(99);
    CHECK(d->GetAmount() == 0);                       // ignored without a city budget
    host.budget = true;

    // flags
    d->SetFlags(kDealFlagFrozen & 0x100, true);       // 0x100: locked state
    const std::uint32_t before = d->GetFlags();
    d->SetFlags(kDealFlagActive | kDealFlagAffordable, true);   // state bits (0x38) are masked while 0x100 is set
    CHECK(d->CheckFlagsAnd(kDealFlagActive) && !d->CheckFlagsOr(kDealFlagAffordable) && d->GetFlags() != before);
    d->SetFlags(0x100, false);
    d->SetFlags(kDealFlagActive, false);
    d->SetFlags(kDealFlagProposed, true);
    d->Activate();
    CHECK(d->CheckFlagsAnd(kDealFlagActive) && !d->CheckFlagsOr(kDealFlagProposed));
    d->SetFlags(kDealFlagActive, false);
    d->SetFlags(kDealFlagAffordable, true);
    d->SetFlags(kDealFlagProposed, true);
    d->Activate();                                    // a state flag is set: nothing happens
    CHECK(!d->CheckFlagsOr(kDealFlagActive));
    d->SetFlags(kDealStateMask, false);

    // affordability
    d->SetAmount(1000);                               // cost 2500 > funds 1000, deal type e0 spends money
    d->UpdateState();
    CHECK(d->CheckFlagsAnd(kDealFlagUnaffordable));
    d->SetFlags(kDealStateMask, false);
    d->SetType(0x4773ffe1);                           // selling deals never go unaffordable
    d->UpdateState();
    CHECK(d->CheckFlagsAnd(kDealFlagAffordable) && !d->CheckFlagsOr(kDealFlagUnaffordable));
    d->Cancel();
    CHECK(d->CheckFlagsAnd(kDealFlagCancelled) && !d->CheckFlagsOr(kDealFlagActive) && d->GetAmount() == 0);

    // clone from the other side
    d->SetType(0x4773ffe2);
    d->SetFlags(kDealFlagCancelled, false);
    cISC4NeighborDeal* clone = nullptr;
    CHECK(d->Clone(&clone, true) && clone);
    CHECK(clone->GetType() == 0x4773ffe2 && clone->CheckFlagsOr(kDealFlagSilent));
    int x, z;
    clone->GetRegionCellOnCitySideOfConnection(x, z);
    CHECK(x == 1 && z == 1);
    clone->Release();
    d->SetRegionCellOnCitySideOfConnection(2, 2);     // city side now belongs to the neighbour: swap
    d->SetRegionCellOnOtherSideOfConnection(1, 1);
    CHECK(d->Clone(&clone, false));
    clone->GetRegionCellOnCitySideOfConnection(x, z);
    CHECK(x == 1 && z == 1 && clone->GetType() == 0x4773ffe3);
    clone->GetRegionCellOnOtherSideOfConnection(x, z);
    CHECK(x == 2 && z == 2);
    clone->Release();
    d->SetRegionCellOnOtherSideOfConnection(9, 9);    // unknown cell: cancelled
    CHECK(d->Clone(&clone, true) && clone->CheckFlagsOr(kDealFlagCancelled));
    clone->Release();

    // dates and persistence
    cGZDate when;
    when.Set(2459000u);
    d->SetStartDate(when);
    when.Set(2459500u);
    d->SetExpirationDate(when);
    d->SetCurrentDate(when);
    when.Set(2459111u);
    d->SetSuspensionDate(when);
    d->SetRegionCellOnOtherSideOfConnection(2, 2);
    d->SetFlags(kDealFlagSilent, true);
    d->SetType(0x4773ffe4); d->SetAmount(3); d->SetAmountUsed(2); d->SetIncAmount(7); d->SetLastAmount(5);
    PortMemoryOStream os;
    CHECK(d->Write(os));
    PortMemoryIStream is(os.bytes().data(), os.bytes().size());
    auto* r = new cSC4NeighborDeal();
    r->AddRef();
    CHECK(r->Read(is) && is.RemainingBytes() == 0);
    CHECK(r->GetType() == 0x4773ffe4 && r->GetAmount() == 3 && r->GetAmountUsed() == 2 && r->GetIncAmount() == 7 && r->GetLastAmount() == 5);
    CHECK(r->GetCost() == d->GetCost() && r->GetFlags() == d->GetFlags());
    cGZDate got;
    r->GetStartDate(got); CHECK(got.DayNumber() == 2459000u);
    r->GetExpirationDate(got); CHECK(got.DayNumber() == 2459500u);
    r->GetSuspensionDate(got); CHECK(got.DayNumber() == 2459111u);
    void* iface = nullptr;
    CHECK(r->QueryInterface(kGZIID_cISC4NeighborDeal, &iface) && iface == static_cast<cISC4NeighborDeal*>(r) && !r->QueryInterface(2, &iface));
    r->Release(); r->Release();
    d->Release();
    std::printf(failures ? "FAILED\n" : "sc4_neighbordeal ok\n");
    return failures ? 1 : 0;
}
