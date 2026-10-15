#include "audio/HitResourceFactory.h"

#include "audio/HitList.h"
#include "audio/HitPropertySetResource.h"
#include "audio/HitResource.h"
#include "audio/HitTrackPlayer.h"
#include "gz/cGZAny.h"
#include "gz/cIGZIStream.h"
#include "gz/cIGZOStream.h"
#include "gz/cIGZPersistRecord.h"

namespace sc4audio {

namespace {
constexpr std::uint32_t kTypeTrackPlayer = 0xa3cd96cf;
constexpr std::uint32_t kTypeHitList = 0x7b1acfcd;
constexpr std::uint32_t kTypePropertySetXml = 0xebfee345, kTypePropertySetBinary = 0xebfee349;
constexpr std::uint32_t kTypeTrackLegacy = 0x0b8d821a, kTypeTrackXml = 0x0b9eb87e;
constexpr std::uint32_t kRegParentTrack = 0xff4c97be;     // track resources start with the default parent sound
constexpr std::uint32_t kRegTypeName = 0xff1cdf6a;
constexpr std::uint32_t kDefaultParentTrack = 0xab903a1b;

// Builds `object`, hands out `iid` and drops the creation reference.
template <class T>
bool Publish(T* object, std::uint32_t iid, void** out) {
    object->AddRef();
    const bool ok = object->QueryInterface(iid, out);
    object->Release();
    return ok;
}
}  // namespace

bool cHitResourceFactory::QueryInterface(std::uint32_t iid, void** out) {
    if (iid != 1 && iid != kGZIID_cIGZPersistResourceFactory) return false;
    *out = this;
    AddRef();
    return true;
}

std::uint32_t cHitResourceFactory::Release() {
    if (--refCount_ != 0) return static_cast<std::uint32_t>(refCount_);
    delete this;
    return 0;
}

// Resource type -> payload. Unknown types fail without creating anything.
bool cHitResourceFactory::CreateInstance(std::uint32_t type, std::uint32_t iid, void** out, std::uint32_t, cIGZUnknown*) {
    switch (type) {
        case kTypeTrackPlayer: {
            auto* resource = new cHitResource<cHitTrackPlayer>();
            resource->SetKey(cGZPersistResourceKey{kTypeTrackPlayer, 0, 0});
            return Publish(resource, iid, out);
        }
        case kTypeHitList: {
            auto* resource = new cHitResource<cHitList>();
            resource->SetKey(cGZPersistResourceKey{kTypeHitList, 0, 0});
            return Publish(resource, iid, out);
        }
        case kTypePropertySetXml:
        case kTypePropertySetBinary: {
            auto* resource = new cHitPropertySetResource(cGZPersistResourceKey{type, 0, 0});
            return Publish(resource, iid, out);
        }
        case kTypeTrackLegacy:
        case kTypeTrackXml: {
            auto* resource = new cHitPropertySetTrackResource(cGZPersistResourceKey{type, 0, 0});
            resource->SetValue(kRegParentTrack, AnyUint32(kDefaultParentTrack));
            resource->SetValue(kRegTypeName, AnyString("Track"));
            return Publish(resource, iid, out);
        }
        default:
            return false;
    }
}

bool cHitResourceFactory::CreateInstance(cIGZPersistDBRecord& record, std::uint32_t iid, void** out, std::uint32_t flags,
                                         cIGZUnknown* owner) {
    const cGZPersistResourceKey key = record.GetKey();
    cIHitPersistResource* resource = nullptr;
    if (!CreateInstance(key.type, kGZIID_cIHitPersistResource, reinterpret_cast<void**>(&resource), flags, owner)) return false;
    bool ok = false;
    resource->SetKey(key);
    cIGZIStream* stream = nullptr;
    if (record.QueryInterface(kGZIID_cIGZIStream, reinterpret_cast<void**>(&stream)) && resource->QueryInterface(iid, out)) {
        ok = resource->Read(*stream, flags, owner);
    }
    if (stream) stream->Release();
    resource->Release();
    return ok;
}

bool cHitResourceFactory::Write(const cIGZPersistResource& resource, cIGZPersistDBRecord& record) {
    cIGZOStream* stream = nullptr;
    if (!record.QueryInterface(kGZIID_cIGZOStream, reinterpret_cast<void**>(&stream))) return false;
    bool ok = false;
    cIHitPersistResource* persist = nullptr;
    const_cast<cIGZPersistResource&>(resource).QueryInterface(kGZIID_cIHitPersistResource, reinterpret_cast<void**>(&persist));
    if (persist) {
        ok = persist->Write(*stream, 0, nullptr);
        persist->Release();
    }
    stream->Release();
    return ok;
}

}  // namespace sc4audio
