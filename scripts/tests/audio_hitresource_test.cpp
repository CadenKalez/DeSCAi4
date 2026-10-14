#include "audio/HitList.h"
#include "audio/HitManAccess.h"
#include "audio/HitResource.h"

#include <cassert>
#include <cstdio>

namespace {
struct FakeHM final : RZ::cHitManRegisterView {
    std::int32_t GetRegisterValue(std::int32_t) const override { return 0; }
    std::int32_t Random(std::int32_t, std::int32_t) override { return 0; }
} g_hm;
}
namespace RZ { cHitManRegisterView* HM() { return &g_hm; } }

int main() {
    const auto before = cHitList::LiveListCount();
    {
        auto* res = new cHitResource<cHitList>();
        res->AddValue(5); res->AddValue(6);
        res->SetKey({HitIds::kResTypeHitList, 0x11, 0x22});
        assert(res->GetType() == HitIds::kResTypeHitList && res->GetGroup() == 0x11 && res->GetInstance() == 0x22);
        res->SetInstance(0x33); assert(res->GetKey().instance == 0x33);

        // persist-resource ids return the resource sub-object, others go to the payload
        void* p = nullptr;
        assert(res->QueryInterface(HitIds::kIidPersistResource, &p));
        assert(p == static_cast<cIGZPersistResource*>(res) && p != static_cast<void*>(res));
        void* q = nullptr;
        assert(res->QueryInterface(kGZIID_cIHitList, &q) && q == static_cast<cIHitList*>(res));
        assert(!res->QueryInterface(0xdeadbeef, &q));
        assert(res->GetRefCount() == 2);       // two successful queries took references

        // private copy: same payload and key, independent object
        void* c = nullptr;
        assert(res->CreatePrivateCopy(HitIds::kIidPersistResource, &c));
        auto* copy = static_cast<cIGZPersistResource*>(c);
        assert(copy->GetType() == HitIds::kResTypeHitList && copy->GetInstance() == 0x33);
        assert(copy->GetRefCount() == 3);      // refcount copied (2) + the query reference
        res->AddValue(7); assert(res->NumItems() == 3);
        auto* copyList = static_cast<cHitResource<cHitList>*>(static_cast<cIHitResource*>(copy));
        assert(copyList->NumItems() == 2);

        copy->Release(); copy->Release(); copy->Release();   // drop to deletion (count 3 -> 0)
        assert(res->Release() == 1);                          // two query references outstanding
        assert(res->Release() == 0);                          // last one deletes the object
    }
    assert(cHitList::LiveListCount() == before);
    assert(HitIds::kClsidHitList == HitIds::kResTypeHitList);  // list class id doubles as its resource type
    std::puts("hit resource ok");
}
