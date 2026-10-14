// cHitResource<T> — adapts a Hit payload type T (cHitList, cHitTrackPlayer, ...) to the persistent
// resource system by adding a (type, group, instance) key and the cIGZPersistResource interface.
// Recovered from the cHitResource<cHitList> instantiation @ 0x1004754ea..0x1004757bc.
//
// Layout (cHitList case, 0x60 bytes): T at +0x00, resource sub-object vptr at +sizeof(T)=+0x48,
// key at +0x50. QueryInterface hands out the resource sub-object for the persist-resource and
// cIHitResource ids and defers everything else to T. CreatePrivateCopy copies T (including its
// refcount, as the original does) and the key, then queries the copy.
#pragma once
#include <cstdint>

#include "audio/HitClassIds.h"
#include "gz/cIGZPersistResource.h"

using cIHitResource = cIGZPersistResource;  // cIHitResource adds nothing beyond the persist interface

template <class T>
class cHitResource : public T, public cIHitResource {
public:
    cHitResource() = default;
    cHitResource(const cHitResource&) = default;

    // cIGZUnknown: both bases declare it, so resolve to T's implementation.
    bool QueryInterface(std::uint32_t iid, void** out) override {
        if (iid == HitIds::kIidPersistResource || iid == HitIds::kIidHitResource) {
            *out = static_cast<cIHitResource*>(this);
            T::AddRef();
            return true;
        }
        return T::QueryInterface(iid, out);
    }
    std::uint32_t AddRef() override { return T::AddRef(); }
    std::uint32_t Release() override { return T::Release(); }

    // cIGZPersistResource
    cGZPersistResourceKey GetKey() const override { return key_; }
    bool SetKey(const cGZPersistResourceKey& key) override { key_ = key; return true; }
    std::uint32_t GetType() const override { return key_.type; }
    std::uint32_t GetGroup() const override { return key_.group; }
    std::uint32_t GetInstance() const override { return key_.instance; }
    bool SetType(std::uint32_t type) override { key_.type = type; return true; }
    bool SetGroup(std::uint32_t group) override { key_.group = group; return true; }
    bool SetInstance(std::uint32_t instance) override { key_.instance = instance; return true; }
    std::uint32_t GetRefCount() const override { return T::GetRefCount(); }

    // Stream I/O: the original forwards to T::ReadResource/WriteResource, which read through a
    // DB-record interface (id kIidRecordAccess) that is not recovered yet. Use the payload's own
    // byte/dword API (e.g. cHitList::ReadResourceBytes) until it is.
    bool Read(cIGZIStream&, std::uint32_t, cIGZUnknown*) override { return false; }
    bool Write(cIGZOStream&, std::uint32_t, cIGZUnknown*) override { return false; }

    bool CreatePrivateCopy(std::uint32_t iid, void** out) override {
        auto* copy = new cHitResource(*this);
        const bool ok = copy->QueryInterface(iid, out);
        return ok;
    }

private:
    cGZPersistResourceKey key_;
};
