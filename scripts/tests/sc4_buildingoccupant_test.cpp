#include <cstdio>

#include "rz/PortMemoryStreams.h"
#include "sc4/BuildingOccupant.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace sc4;

int main() {
    auto* b = new cSC4BuildingOccupant();
    b->AddRef();
    CHECK(b->GetType() == kOccupantTypeBuilding && b->GetGZCLSID() == 0xa9bd882d);
    CHECK(b->GetCompletionPercent() == 1.0f && !b->IsLit() && b->GetBuildingAge() == 0);
    const float lo[3] = {16, 0, 32}, hi[3] = {48, 20, 64};
    b->SetBoundingBox(lo, hi);
    b->SetBuildingType(0xdeadbeef);
    b->SetOrientation(7);
    CHECK(b->GetOrientation() == 3);
    b->SetOrientation(-4);
    CHECK(b->GetOrientation() == 0);
    b->SetOrientation(2);
    float pos[3];
    b->GetPosition(pos);
    CHECK(pos[0] == 32 && pos[1] == 0 && pos[2] == 48);
    const float moved[3] = {100, 5, 200};
    b->SetPosition(moved);
    float mn[3], mx[3];
    b->GetBoundingBox(mn, mx);
    CHECK(mn[0] == 84 && mx[0] == 116 && mn[1] == 5 && mx[1] == 25 && mn[2] == 184 && mx[2] == 216);
    b->SetCompletionPercent(0.5f);
    b->SetLit(true);
    CHECK(b->IsLit());

    PortMemoryOStream os;
    CHECK(b->Write(os));
    const auto& bytes = os.bytes();
    PortMemoryIStream is(bytes.data(), bytes.size());
    auto* r = new cSC4BuildingOccupant();
    r->AddRef();
    CHECK(r->Read(is));
    CHECK(r->GetBuildingType() == 0xdeadbeef && r->GetOrientation() == 2 && r->IsLit());
    r->GetBoundingBox(mn, mx);
    CHECK(mn[0] == 84 && mx[2] == 216 && r->GetCompletionPercent() == 0.5f);

    void* raw = nullptr;
    CHECK(b->QueryInterface(kGZIID_cISC4BuildingOccupant, &raw) && raw == static_cast<cISC4BuildingOccupant*>(b));
    static_cast<cISC4BuildingOccupant*>(raw)->Release();
    CHECK(b->QueryInterface(kGZIID_cISC4Occupant, &raw));
    static_cast<cIGZUnknown*>(raw)->Release();
    CHECK(cSC4BuildingOccupant::ConvertZoneToDensity(1) == 1 && cSC4BuildingOccupant::ConvertZoneToDensity(5) == 2 &&
          cSC4BuildingOccupant::ConvertZoneToDensity(9) == 3 && cSC4BuildingOccupant::ConvertZoneToDensity(10) == 0);
    r->Release();
    b->Release();
    std::puts(failures ? "sc4_buildingoccupant: FAILED" : "sc4_buildingoccupant ok");
    return failures != 0;
}
