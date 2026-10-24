#include <cstdio>

#include "audio/HitClassIds.h"
#include "audio/HitList.h"
#include "audio/HitManAccess.h"
#include "audio/HitResource.h"
#include "audio/HitResourceFactory.h"
#include "audio/HitTrackPlayer.h"
#include "gz/cIGZPersistRecord.h"
#include "gz/cGZAny.h"
#include "gz/cGZPropertySet.h"
#include "gz/cIGZIStream.h"
#include "rz/cRZIStream.h"
#include "rz/cRZOStream.h"
#include "rz/cRZString.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

using namespace sc4audio;

int main() {
    cHitResourceFactory* factory = new cHitResourceFactory();
    factory->AddRef();
    void* any = nullptr;
    CHECK(factory->QueryInterface(kGZIID_cIGZPersistResourceFactory, &any) && any == factory);
    CHECK(!factory->QueryInterface(0x1234, &any));
    factory->Release();

    for (std::uint32_t type : {0xa3cd96cfu, 0x7b1acfcdu, 0xebfee345u, 0xebfee349u, 0x0b8d821au, 0x0b9eb87eu}) {
        cIHitPersistResource* resource = nullptr;
        CHECK(factory->CreateInstance(type, kGZIID_cIHitPersistResource, reinterpret_cast<void**>(&resource)));
        CHECK(resource && resource->GetType() == type && resource->GetGroup() == 0 && resource->GetInstance() == 0);
        if (resource) CHECK(resource->GetRefCount() == 1);     // the creation reference was dropped
        if (resource) resource->Release();
    }
    void* none = nullptr;
    CHECK(!factory->CreateInstance(0x12345678u, kGZIID_cIHitPersistResource, &none));

    // cHitResource<T> key setters, private copies and destruction for the playable track and the hit list
    for (std::uint32_t type : {HitIds::kResTypeTrackPlayer, HitIds::kResTypeHitList}) {
        cIHitPersistResource* r = nullptr;
        CHECK(factory->CreateInstance(type, kGZIID_cIHitPersistResource, reinterpret_cast<void**>(&r)) && r);
        if (!r) continue;
        CHECK(r->SetType(0x11) && r->SetGroup(0x22) && r->SetInstance(0x33));
        const cGZPersistResourceKey key = r->GetKey();
        CHECK(key.type == 0x11 && key.group == 0x22 && key.instance == 0x33);
        void* copy = nullptr;
        CHECK(r->CreatePrivateCopy(kGZIID_cIHitPersistResource, &copy) && copy && copy != r);
        if (copy) {
            auto* c = static_cast<cIHitPersistResource*>(copy);
            CHECK(c->GetInstance() == 0x33);       // the copy keeps the key
            while (c->Release() != 0) {}           // drops the copy (destructor)
        }
        r->Release();
    }

    // cHitResource<cHitTrackPlayer>: the shared template forwards Read/Write to the payload's own
    // ReadResource/WriteResource. cHitTrackPlayer's are the 8-byte `return false` bodies at
    // 0x100473b32/0x100473b3a, so both instantiations of the template are `return false` here.
    // The Debug*Tag methods are the other 8-byte `return false` bodies (0x100473da6/ae/b6,
    // cHitList's at 0x1004755e6/ee/f6) and are not part of any vtable.
    {
        auto* trackResource = new cHitResource<cHitTrackPlayer>();
        trackResource->AddRef();
        cRZNullIStream in;
        cRZNullOStream out;
        CHECK(!trackResource->Read(in, 0, nullptr));
        CHECK(!trackResource->Write(out, 0, nullptr));
        cRZString tag("unchanged");
        CHECK(!trackResource->DebugInstanceTag(tag));
        CHECK(!trackResource->DebugClassTag(tag));
        CHECK(!trackResource->DebugTypeTag(tag));
        CHECK(tag.str() == "unchanged");
        CHECK(trackResource->Release() == 0);  // only the test's reference; deletes
    }

    // track property sets start with the default parent sound and the "Track" type name
    cIHitPersistResource* track = nullptr;
    CHECK(factory->CreateInstance(0x0b8d821au, kGZIID_cIHitPersistResource, reinterpret_cast<void**>(&track)));
    cIGZPropertySet* set = nullptr;
    CHECK(track && track->QueryInterface(0xebb10cdd, reinterpret_cast<void**>(&set)) && set);
    if (set) {
        AnyUint32 parent;
        CHECK(set->Value(0xff4c97be, &parent) && parent.get() == 0xab903a1b);
        AnyString name;
        CHECK(set->Value(0xff1cdf6a, &name) && name.string() && name.string()->str() == "Track");
        set->Release();
    }
    if (track) track->Release();
    factory->Release();
    std::printf(failures ? "FAILED\n" : "audio_hitresourcefactory: all passed\n");
    return failures ? 1 : 0;
}
