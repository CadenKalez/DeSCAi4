#include <cstdio>

#include "rz/PortMemoryStreams.h"
#include "sc4/ResKeyTable.h"
#include "sc4/SCProperty.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

using namespace sc4;

static void Count(const cGZPersistResourceKey&, void* ctx) { ++*static_cast<int*>(ctx); }

int main() {
    auto* t = cSCResKeyTable::Create();
    t->AddRef();
    CHECK(t->Size() == 0 && t->IsExplicit() && t->GetGZCLSID() == 0x67336285);
    t->AddEntry({1, 2, 3});
    t->AddEntry({4, 5, 6});
    t->SetEntry(3, {7, 8, 9});                       // grows with zero keys
    CHECK(t->Size() == 4);
    cGZPersistResourceKey k;
    t->GetEntry(2, k);
    CHECK(k.type == 0 && k.group == 0 && k.instance == 0);
    t->GetEntry(3, k);
    CHECK(k.type == 7 && k.group == 8 && k.instance == 9);
    t->RemoveEntry(std::uint32_t(2));
    CHECK(t->Size() == 3);
    t->RemoveEntry(cGZPersistResourceKey{4, 5, 6});
    CHECK(t->Size() == 2);
    t->RemoveEntry(cGZPersistResourceKey{99, 99, 99});
    t->RemoveEntry(std::uint32_t(50));
    CHECK(t->Size() == 2);
    int n = 0;
    t->EnumEntries(Count, &n);
    CHECK(n == 2);

    // property triples are (group, type, instance)
    cSCProperty prop;
    prop.SetPropertyID(5);
    std::vector<std::uint32_t> triples{10, 11, 12, 20, 21, 22};
    cRZVariant v;
    v.RefUint32(triples.data(), 6);
    prop.SetPropertyValue(v);
    t->Clear();
    t->AddEntries(&prop);
    CHECK(t->Size() == 2);
    t->GetEntry(0, k);
    CHECK(k.group == 10 && k.type == 11 && k.instance == 12);
    t->GetEntry(1, k);
    CHECK(k.group == 20 && k.type == 21 && k.instance == 22);

    // persistence
    PortMemoryOStream os;
    CHECK(t->Write(os));
    PortMemoryIStream is(os.bytes().data(), os.bytes().size());
    auto* r = cSCResKeyTable::Create();
    r->AddRef();
    CHECK(r->Read(is) && r->Size() == 2);
    r->GetEntry(1, k);
    CHECK(k.type == 21 && k.group == 20 && k.instance == 22);
    r->Release();

    // enumerator
    cISCResKeyEnumerator* e = nullptr;
    CHECK(t->CreateEnumerator(&e) && e);
    cGZPersistResourceKey* out[4];
    std::uint32_t got = 0;
    CHECK(e->Next(1, out, got) && got == 1 && out[0]->type == 11);
    CHECK(!e->Next(3, out, got) && got == 1 && out[0]->type == 21);
    CHECK(!e->Next(1, out, got) && got == 0);
    e->Reset();
    CHECK(e->Skip(1) && !e->Skip(1));
    e->Reset();
    cISCResKeyEnumerator* copy = nullptr;
    CHECK(e->Clone(&copy) && copy);
    CHECK(copy->Next(2, out, got) && got == 2);
    void* iface = nullptr;
    CHECK(copy->QueryInterface(kGZIID_cISCResKeyEnumerator, &iface) && iface == copy && !copy->QueryInterface(2, &iface));
    copy->Release(); copy->Release();
    e->Release();
    CHECK(t->QueryInterface(kGZIID_cISCResKeyTable, &iface) && !t->QueryInterface(2, &iface));
    t->Release(); t->Release();
    std::printf(failures ? "FAILED\n" : "sc4_reskeytable ok\n");
    return failures ? 1 : 0;
}
