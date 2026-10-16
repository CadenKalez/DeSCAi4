#include <cstdio>

#include "gz/GZCOM.h"
#include "sc4/NeighborDeal.h"
#include "sc4/Ordinance.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

int main() {
    void* deal = gzcom::GZCOM_CreatecSC4NeighborDeal();
    CHECK(deal != nullptr);
    auto* d = static_cast<sc4::cSC4NeighborDeal*>(deal);
    CHECK(d->GetGZCLSID() == gzcom::kCLSID_cSC4NeighborDeal);
    delete d;
    void* gas = gzcom::GZCOM_CreatecSC4OrdinanceCleanAir();
    CHECK(gas != nullptr);
    delete static_cast<sc4::cSC4OrdinanceSimple*>(gas);
    void* sim = gzcom::GZCOM_CreatecSC4OrdinanceSimulator();
    CHECK(sim != nullptr && gzcom::FindClassFactory(gzcom::kCLSID_cSC4NeighborConnection) != nullptr);
    delete static_cast<sc4::cSC4OrdinanceSimulator*>(sim);
    std::printf(failures ? "FAILED\n" : "gz_gzcomcreators ok\n");
    return failures != 0;
}
