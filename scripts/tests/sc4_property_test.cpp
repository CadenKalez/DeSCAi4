#include <cstdio>

#include "rz/PortMemoryStreams.h"
#include "rz/cRZString.h"
#include "rz/cRZUnknownList.h"
#include "sc4/SCProperty.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

using namespace sc4;

int main() {
    // cRZUnknownList
    auto* list = new cRZUnknownList();
    list->AddRef();
    auto* a = new cSCProperty(1); a->AddRef();
    auto* b = new cSCProperty(2); b->AddRef();
    CHECK(list->PushBack(a) && list->PushBack(b) && list->Size() == 2);
    void* raw = nullptr;
    CHECK(list->GetElement(1, kGZIID_cISCProperty, &raw) && raw == static_cast<cISCProperty*>(b));
    static_cast<cISCProperty*>(raw)->Release();
    CHECK(!list->GetElement(2, kGZIID_cISCProperty, &raw));
    CHECK(!list->PeekFront(0x1234, &raw));                       // wrong interface
    cIGZUnknownList* clone = nullptr;
    list->CreateClone(&clone);
    CHECK(clone && clone->Size() == 2);
    CHECK(clone->PopBack(kGZIID_cISCProperty, &raw) && raw == static_cast<cISCProperty*>(b));
    static_cast<cISCProperty*>(raw)->Release();
    CHECK(clone->Size() == 1 && list->Size() == 2);
    clone->Release();
    CHECK(list->Clear() && list->Size() == 0);
    list->Release();
    a->Release(); b->Release();

    // holder: typed adds, sorting, replace semantics
    cSCPropertyHolder h;
    h.AddProperty(30u, std::uint32_t{300}, true);
    h.AddProperty(10u, std::uint32_t{100}, true);
    h.AddProperty(20u, std::uint32_t{200}, true);
    h.AddProperty(10u, std::uint32_t{111}, true);               // existing id -> value replaced
    CHECK(h.Count() == 3 && h.HasProperty(20) && !h.HasProperty(21));
    std::uint32_t v = 0;
    CHECK(h.GetProperty(10, v) && v == 111);
    CHECK(!h.GetProperty(99, v));
    cRZString s("hello");
    h.AddProperty(40u, static_cast<const cIGZString&>(s), true);
    cRZString out;
    CHECK(h.GetProperty(40, static_cast<cIGZString&>(out)) && out.Strlen() == 5);

    // enumeration is id-ordered
    struct Acc { std::uint32_t ids[8]; int n = 0; } acc;
    h.EnumProperties([](cISCProperty* p, void* c) {
        auto* a = static_cast<Acc*>(c);
        a->ids[a->n++] = p->GetPropertyID();
    }, &acc);
    auto& order = acc.ids; int n = acc.n;
    CHECK(n == 4 && order[0] == 10 && order[1] == 20 && order[2] == 30 && order[3] == 40);

    // remove + compact
    CHECK(h.RemoveProperty(20, false) && !h.RemoveProperty(20, false) && h.Count() == 3);
    h.CompactProperties();

    // list of properties
    cIGZUnknownList* props = nullptr;
    CHECK(h.GetPropertyList(&props) && props->Size() == 3);
    props->Release();

    // serialization: header and ids, then reading back adds nothing (empty values) — original quirk
    PortMemoryOStream os;
    CHECK(h.Write(os));
    const auto& bytes = os.bytes();
    CHECK(bytes.size() == 2 + 4 + 3 * 8);
    CHECK(bytes[0] == 2 && bytes[1] == 0 && bytes[2] == 3);
    PortMemoryIStream is(bytes.data(), bytes.size());
    cSCPropertyHolder r;
    CHECK(r.Read(is) && r.Count() == 0);

    // wrong version is rejected
    unsigned char bad[6] = {3, 0, 0, 0, 0, 0};
    PortMemoryIStream is2(bad, 6);
    CHECK(!r.Read(is2));

    std::printf(failures ? "FAILED (%d)\n" : "PASS sc4_property\n", failures);
    return failures != 0;
}
