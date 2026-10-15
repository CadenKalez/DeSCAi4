#include <cstdio>

#include "rz/PortMemoryStreams.h"
#include "sc4/NeighborConnection.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

using namespace sc4;

namespace {
struct Host final : INeighborConnectionHost {
    int city = 0, convX = -1, convZ = -1;
    int CityCellsX() override { return 64; }
    int CityCellsZ() override { return 64; }
    bool CityEntryAtCell(int x, int z, void*& c) override { if (x == 3 && z == 4) { c = &city; return true; } return false; }
    void ConvertConnectionCell(int x, int z, int& ox, int& oz, void*) override { convX = x; convZ = z; ox = 3; oz = 4; }
};
}  // namespace

int main() {
    Host host;
    SetNeighborConnectionHost(&host);
    auto* c = new cSC4NeighborConnection();
    c->AddRef();
    int x = 0, z = 0;
    c->Repair(nullptr);   // sets the other-side cell from the connection cell (0,0 -> 3,4)
    CHECK(host.convX == 0 && c->GetCityOnOtherSideOfConnection() == &host.city);
    c->GetRegionCellOnOtherSideOfConnection(x, z); CHECK(x == 3 && z == 4);
    CHECK(!c->GetConnectionInnerCell(x, z));   // (0,0) is inside on both axes
    CHECK(c->GetIsActive() && !c->GetVisitedThisCycle());
    c->SetIsActive(false); c->SetVisitedThisCycle(true);
    CHECK(!c->GetIsActive() && c->GetVisitedThisCycle());
    c->SetIsActive(true); c->SetVisitedThisCycle(false);
    CHECK(c->GetIsActive() && !c->GetVisitedThisCycle());
    PortMemoryOStream os;
    CHECK(c->Write(os));
    PortMemoryIStream is(os.bytes().data(), os.bytes().size());
    auto* r = new cSC4NeighborConnection();
    r->AddRef();
    CHECK(r->Read(is) && is.RemainingBytes() == 0);
    r->GetRegionCellOnOtherSideOfConnection(x, z); CHECK(x == 3 && z == 4);
    void* iface = nullptr;
    CHECK(r->QueryInterface(kGZIID_cISC4NeighborConnection, &iface) && iface == static_cast<cISC4NeighborConnection*>(r) && !r->QueryInterface(2, &iface));
    r->Release(); r->Release(); c->Release();
    std::printf(failures ? "FAILED\n" : "sc4_neighborconnection ok\n");
    return failures ? 1 : 0;
}
