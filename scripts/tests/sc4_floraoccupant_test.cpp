#include <cstdio>
#include "rz/PortMemoryStreams.h"
#include "sc4/FloraOccupant.h"
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace sc4;
namespace {
class Host final : public IFloraHost {
public:
    cSC4StaticFloraData* StaticFloraData(std::uint32_t) override { return nullptr; }
};
}
int main() {
    Host host;
    SetFloraHost(&host);
    auto* p = new cSC4FloraOccupant();
    p->AddRef();
    CHECK(p->GetType() == kOccupantTypeFlora);
    p->SetFloraType(0x4242);
    p->SetBirthDate(100);
    p->SetLastSeedingDate(200);
    p->SetState(-2);
    p->SetOrientation(3);
    const float pos[3] = {10, 20, 30};
    p->SetPosition(pos);
    std::uint32_t tag = 0;
    CHECK(!p->GetLotTag(tag) || tag == 0);
    p->SetLotTag(0xffffffffu);
    CHECK(!p->GetLotTag(tag));
    p->SetLotTag(9);
    float mn[3], mx[3];
    p->GetBoundingBox(mn, mx);
    CHECK(mn[0] == 9 && mx[0] == 11 && mn[1] == 20 && mx[1] == 21 && mx[2] == 31);
    PortMemoryOStream os;
    CHECK(p->Write(os));
    const auto& bytes = os.bytes();
    PortMemoryIStream is(bytes.data(), bytes.size());
    auto* r = new cSC4FloraOccupant();
    r->AddRef();
    CHECK(r->Read(is));
    CHECK(r->GetFloraType() == 0x4242 && r->GetBirthDate() == 100 && r->GetLastSeedingDate() == 200);
    CHECK(r->GetState() == -2 && r->GetOrientation() == 3 && r->GetLotTag(tag) && tag == 9);
    r->Release();
    p->Release();
    std::puts(failures ? "sc4_floraoccupant: FAILED" : "sc4_floraoccupant ok");
    return failures != 0;
}
