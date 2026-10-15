#include <cstdio>

#include "rz/PortMemoryStreams.h"
#include "sc4/AdviceSubject.h"
#include "sc4/DBSegment.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

using namespace sc4;

namespace {
struct Host final : IAdviceSubjectHost {
    std::int32_t CityCellsX() override { return 64; }
    std::int32_t CityCellsZ() override { return 128; }
};
struct Obj final : cIGZUnknown {
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    std::uint32_t refs = 0;
};
}  // namespace

int main() {
    Host host;
    SetAdviceSubjectHost(&host);
    auto* s = new cSC4AdviceSubject();
    s->AddRef();
    CHECK(s->GetGZCLSID() == 0x6a26d049 && !s->HasLocation() && s->GetLocationX() == 0 && s->GetLocationZ() == 0);
    s->SetLocation(-5.0f, 500.0f);
    CHECK(s->HasLocation() && s->GetLocationX() == 0.0f && s->GetLocationZ() == 128.0f);
    s->SetLocation(100.0f, 20.5f);
    CHECK(s->GetLocationX() == 64.0f && s->GetLocationZ() == 20.5f);
    s->SetLocation(10.0f, 20.0f);
    s->SetData1(7); s->SetData2(9);
    CHECK(s->GetData1() == 7 && s->GetData2() == 9);

    Obj obj;
    obj.refs = 1;
    CHECK(s->SetObject(&obj) && obj.refs == 2 && s->GetObjectA() == &obj);
    CHECK(s->SetObject(&obj) && obj.refs == 2);            // same object: no extra reference
    cISC4AdviceSubject* clone = nullptr;
    CHECK(s->Clone(&clone) && clone != nullptr);
    CHECK(clone->HasLocation() && clone->GetLocationX() == 10.0f && clone->GetLocationZ() == 20.0f);
    CHECK(clone->GetData1() == 7 && clone->GetData2() == 9 && clone->GetObjectA() == &obj && obj.refs == 3);
    clone->Release();
    CHECK(obj.refs == 2);
    CHECK(!s->Clone(nullptr));

    // stream round trip (the nested object needs a segment stream, so a plain stream carries none)
    s->SetObject(nullptr);
    CHECK(obj.refs == 1);
    PortMemoryOStream os;
    CHECK(s->Write(os));
    PortMemoryIStream is(os.bytes().data(), os.bytes().size());
    auto* r = new cSC4AdviceSubject();
    r->AddRef();
    CHECK(r->Read(is));
    CHECK(r->HasLocation() && r->GetLocationX() == 10.0f && r->GetData1() == 7 && r->GetData2() == 9 && is.RemainingBytes() == 0);
    s->SetLocation(0, 0);

    void* iface = nullptr;
    CHECK(r->QueryInterface(kGZIID_cISC4AdviceSubject, &iface) && iface == static_cast<cISC4AdviceSubject*>(r));
    CHECK(r->QueryInterface(kGZIID_cIGZSerializable, &iface) && iface == static_cast<cIGZSerializable*>(r));
    CHECK(!r->QueryInterface(0x55, &iface));
    r->Release(); r->Release(); r->Release();
    const std::uint8_t bad[] = {3, 0};
    PortMemoryIStream badStream(bad, sizeof bad);
    CHECK(!s->Read(badStream));
    s->Release();
    std::printf(failures ? "FAILED\n" : "sc4_advicesubject ok\n");
    return failures ? 1 : 0;
}
