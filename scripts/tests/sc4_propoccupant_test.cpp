#include <cstdio>
#include "rz/PortMemoryStreams.h"
#include "sc4/PropOccupant.h"
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace sc4;
namespace {
class Host final : public IPropHost {
public:
    int ClockHour() override { return 12; }
    int ClockMinute() override { return 30; }
    void SimulatorDate(cGZDate& out) override { out.Set(6, 10, 2005); }
    std::uint32_t RandomPercent() override { return 0; }
    cISC4OccupantManager* OccupantManager() override { return nullptr; }
};
}
int main() {
    Host host;
    SetPropHost(&host);
    CHECK(GetPropTypeIndex(0xaaaa) == GetPropTypeIndex(0xaaaa) && GetPropTypeFromIndex(GetPropTypeIndex(0xbbbb)) == 0xbbbb);
    auto* p = new cSC4PropOccupant();
    p->AddRef();
    CHECK(p->GetType() == kOccupantTypeProp && p->GetRandomChance() == 100 && !p->HasDateRange());
    p->SetPropType(0x1234abcd);
    p->SetOrientation(9);
    CHECK(p->GetOrientation() == 3 && p->GetPropType() == 0x1234abcd);
    const float lo[3] = {0, 1, 2}, hi[3] = {4, 5, 6};
    p->SetBoundingBox(lo, hi);
    p->SetTimeOfDayRange(8.0f, 18.0f);
    float a, b;
    p->GetTimeOfDayRange(a, b);
    CHECK(a == 8.0f && b == 18.0f);
    p->SetSimulatorDateRange(365, 10, 12, 20);          // every 20 December for ten days
    CHECK(p->HasDateRange() && !p->GetPropTimerState(kPropTimerDate));
    p->SetSimulatorDateRange(365, 10, 6, 5);            // 5 June: already past in 2005 -> next year, not active now
    CHECK(!p->GetPropTimerState(kPropTimerDate));
    p->SetSimulatorDateRange(365, 10, 6, 10);           // today
    CHECK(p->GetPropTimerState(kPropTimerDate));
    p->SetPropUsage(7);
    p->SetLotTag(55);
    PortMemoryOStream os;
    CHECK(p->Write(os));
    const auto& bytes = os.bytes();
    PortMemoryIStream is(bytes.data(), bytes.size());
    auto* r = new cSC4PropOccupant();
    r->AddRef();
    CHECK(r->Read(is));
    CHECK(r->GetPropType() == 0x1234abcd && r->GetOrientation() == 3 && r->HasDateRange() && r->GetPropUsage() == 7);
    std::uint32_t tag = 0;
    CHECK(r->GetLotTag(tag) && tag == 55 && r->GetPropTimerBitfield() == p->GetPropTimerBitfield());
    float mn[3], mx[3];
    r->GetBoundingBox(mn, mx);
    CHECK(mn[2] == 2 && mx[0] == 4);
    r->Release();
    p->Release();
    std::puts(failures ? "sc4_propoccupant: FAILED" : "sc4_propoccupant ok");
    return failures != 0;
}
