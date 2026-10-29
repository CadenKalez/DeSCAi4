#include <cstdio>
#include <set>

#include "rz/PortMemoryStreams.h"
#include "sc4/DummyAutoOccupant.h"
#include "sc4/Occupant.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

using namespace sc4;

int main() {
    auto* o = new cSC4Occupant();
    o->AddRef();
    CHECK(o->GetType() == kOccupantDefaultType);
    CHECK(o->GetGZCLSID() == kGZCLSID_cSC4Occupant);
    CHECK(o->IsFlagSet(kOccupantFlagVisible) && o->GetFlags() == 4);
    CHECK(!o->IsInitialized());
    CHECK(o->Init() && o->IsInitialized());

    // removal counters
    CHECK(o->CanRemove(1) && o->CanRemove(2));
    o->SetRemovalFlags(1);
    CHECK(!o->CanRemove(1) && o->CanRemove(2));
    o->SetRemovalFlags(2);
    CHECK(!o->CanRemove(2));
    o->UnsetRemovalFlags(3);
    CHECK(o->CanRemove(1) && o->CanRemove(2));

    // flags
    o->SetFlag(0x10);
    CHECK(o->IsFlagSet(0x10) && o->GetFlags() == 0x14);
    o->ClearFlag(0x04);
    CHECK(!o->IsFlagSet(kOccupantFlagVisible));
    o->SetVisibility(true, false);
    CHECK(o->IsFlagSet(kOccupantFlagVisible));

    // groups
    o->AddOccupantGroup(0x1111);
    o->AddOccupantGroup(0x2222);
    o->AddOccupantGroup(0x1111);
    CHECK(o->IsOccupantGroup(0x1111) && o->IsOccupantGroup(0x2222) && !o->IsOccupantGroup(0x3333));
    std::set<std::uint32_t> groups;
    CHECK(o->GetOccupantGroups(groups) && groups.size() == 2);

    // bounding cells and manager box
    std::uint8_t cells[4] = {};
    CHECK(!o->GetOccupantManagerBBox(cells));
    const std::uint8_t box[4] = {1, 2, 3, 4};
    o->SetOccupantManagerBBox(box);
    CHECK(o->GetOccupantManagerBBox(cells) && cells[3] == 4);
    o->SetHighlight(3, false);
    CHECK(o->GetHighlight() == 3);

    // stream round trip (saved flags drop bits not in 0xdd; highlight is kept; loaded state bit set)
    PortMemoryOStream os;
    CHECK(o->Write(os));
    const auto& bytes = os.bytes();
    PortMemoryIStream is(bytes.data(), bytes.size());
    auto* r = new cSC4Occupant();
    r->AddRef();
    CHECK(r->Read(is));
    CHECK(r->GetHighlight() == 3 && r->GetType() == o->GetType());
    CHECK(!r->IsOccupantGroup(0x2222));    // property values are not stored by the property record (original quirk)
    CHECK(r->GetFlags() == (o->GetFlags() & 0xdd));
    CHECK(r->GetOccupantManagerBBox(cells) && cells[1] == 2);
    r->Release();

    // interfaces
    void* raw = nullptr;
    CHECK(o->QueryInterface(kGZIID_cISC4Occupant, &raw) && raw == static_cast<cISC4Occupant*>(o));
    static_cast<cISC4Occupant*>(raw)->Release();
    CHECK(o->QueryInterface(kGZIID_cIGZSerializable, &raw) && raw == static_cast<cIGZSerializable*>(o));
    static_cast<cIGZSerializable*>(raw)->Release();
    CHECK(!o->QueryInterface(0x12345678, &raw));
    o->Release();
    {   // the dummy automaton target: position, a zero-size box and one group
        cSC4DummyOccupant plain;
        const float spot[3] = {4.0f, 5.0f, 6.0f};
        plain.SetPosition(spot);
        float q[3], qlo[3], qhi[3];
        plain.GetPosition(q);
        plain.GetBoundingBox(qlo, qhi);
        CHECK(q[1] == 5.0f && qlo[2] == 6.0f && qhi[0] == 4.0f);
        cSC4DummyAutoOccupant dummy;
        const float where[3] = {1.0f, 2.0f, 3.0f};
        dummy.SetPosition(where);
        float p[3], lo[3], hi[3];
        dummy.GetPosition(p);
        dummy.GetBoundingBox(lo, hi);
        CHECK(p[0] == 1.0f && p[1] == 2.0f && p[2] == 3.0f);
        CHECK(lo[0] == 1.0f && hi[2] == 3.0f && lo[1] == hi[1]);
        dummy.SetGroup(0x4410);
        CHECK(dummy.IsOccupantGroup(0x4410) && !dummy.IsOccupantGroup(0x4411));
    }
    std::puts(failures ? "sc4_occupant: FAILED" : "sc4_occupant: ok");
    return failures != 0;
}
