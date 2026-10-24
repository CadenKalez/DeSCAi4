// cHitResource<T> — adapts a Hit payload type T (cHitList, cHitTrackPlayer, ...) to the persistent
// resource system by adding a (type, group, instance) key and the cIHitPersistResource interface.
// Recovered from the cHitResource<cHitList> instantiation @ 0x1004754ea..0x1004757bc and the
// cHitResource<cHitTrackPlayer> instantiation @ 0x100473a90..0x100474302.
//
// Layout: T at +0x00, then the resource sub-object vptr, then the key. In the binary the resource
// vptr is at +0x48 for cHitList and +0x208 for cHitTrackPlayer, and the key follows it (+0x50 /
// +0x210; see the GetKey disassembly). The port's T sizes differ (std::vector/deque members) but the
// template keeps the same T + vptr + key order. QueryInterface hands out the resource sub-object for
// the persist-resource and cIHitResource ids and defers everything else to T. CreatePrivateCopy
// copies T (including its refcount, as the original does) and the key, then queries the copy.
//
// DebugInstanceTag/DebugClassTag/DebugTypeTag are not interface methods and are not in either
// vtable; both instantiations return false and leave the string untouched (0x1004755e6/ee/f6,
// 0x100473da6/ae/b6).
#pragma once
#include <cstdint>

#include "audio/HitClassIds.h"
#include "gz/cIGZPersistResource.h"
#include "gz/cIGZString.h"

using cIHitResource = cIHitPersistResource;  // cIHitResource adds nothing beyond the persist interface

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

    // cIHitPersistResource
    cGZPersistResourceKey GetKey() const override { return key_; }
    bool SetKey(const cGZPersistResourceKey& key) override { key_ = key; return true; }
    std::uint32_t GetType() const override { return key_.type; }
    std::uint32_t GetGroup() const override { return key_.group; }
    std::uint32_t GetInstance() const override { return key_.instance; }
    bool SetType(std::uint32_t type) override { key_.type = type; return true; }
    bool SetGroup(std::uint32_t group) override { key_.group = group; return true; }
    bool SetInstance(std::uint32_t instance) override { key_.instance = instance; return true; }
    std::uint32_t GetRefCount() const override { return T::GetRefCount(); }

    // Stream I/O: both instantiations forward to the payload's own ReadResource/WriteResource and
    // return its result (the cHitList one is a tail call; cHitTrackPlayer::ReadResource is inline
    // and returns false, so those two instantiations are just `return false`). The payload reads the
    // persist-DB record view (iid kIidRecordAccess) that the stream itself QueryInterfaces.
    bool Read(cIGZIStream& stream, std::uint32_t flags, cIGZUnknown* userData) override {
        return T::ReadResource(stream, flags, userData);
    }
    bool Write(cIGZOStream& stream, std::uint32_t flags, cIGZUnknown* userData) override {
        return T::WriteResource(stream, flags, userData);
    }

    // Debug tags (own methods, not part of cIHitPersistResource and not in the vtable): always false.
    bool DebugInstanceTag(cIGZString&) { return false; }
    bool DebugClassTag(cIGZString&) { return false; }
    bool DebugTypeTag(cIGZString&) { return false; }

    bool CreatePrivateCopy(std::uint32_t iid, void** out) override {
        auto* copy = new cHitResource(*this);
        const bool ok = copy->QueryInterface(iid, out);
        return ok;
    }

private:
    cGZPersistResourceKey key_;
};
