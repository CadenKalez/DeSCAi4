// cIHitPersistResource — an object that can be loaded from / saved to the persistent resource DB.
// Slots 3..14 (byte offsets +0x18..+0x70), from the cIHitResource sub-vtable of cHitResource<T>.
#pragma once
#include <cstdint>

#include "gz/cGZPersistResourceKey.h"
#include "gz/cIGZOStream.h"
#include "gz/cIGZIStream.h"

class cIGZPersistResource : public cIGZUnknown {
public:
    virtual cGZPersistResourceKey GetKey() const = 0;                       // [3]
    virtual bool SetKey(const cGZPersistResourceKey& key) = 0;              // [4]
    virtual std::uint32_t GetType() const = 0;                              // [5]
    virtual std::uint32_t GetGroup() const = 0;                             // [6]
    virtual std::uint32_t GetInstance() const = 0;                          // [7]
    virtual bool SetType(std::uint32_t type) = 0;                           // [8]
    virtual bool SetGroup(std::uint32_t group) = 0;                         // [9]
    virtual bool SetInstance(std::uint32_t instance) = 0;                   // [10]
    virtual std::uint32_t GetRefCount() const = 0;                          // [11]
    virtual ~cIGZPersistResource() = default;                               // [12],[13]
};
constexpr std::uint32_t kGZIID_cIGZPersistResource = 0x456b8f1d;

class cIHitPersistResource : public cIGZUnknown {
public:
    virtual cGZPersistResourceKey GetKey() const = 0;                       // [3]
    virtual bool SetKey(const cGZPersistResourceKey& key) = 0;              // [4]
    virtual std::uint32_t GetType() const = 0;                              // [5]
    virtual std::uint32_t GetGroup() const = 0;                             // [6]
    virtual std::uint32_t GetInstance() const = 0;                          // [7]
    virtual bool SetType(std::uint32_t type) = 0;                           // [8]
    virtual bool SetGroup(std::uint32_t group) = 0;                         // [9]
    virtual bool SetInstance(std::uint32_t instance) = 0;                   // [10]
    virtual std::uint32_t GetRefCount() const = 0;                          // [11]
    virtual bool Read(cIGZIStream& stream, std::uint32_t flags, cIGZUnknown* userData) = 0;    // [12]
    virtual bool Write(cIGZOStream& stream, std::uint32_t flags, cIGZUnknown* userData) = 0;   // [13]
    // [14] Makes an independent copy and returns the interface `iid` of it in *out.
    virtual bool CreatePrivateCopy(std::uint32_t iid, void** out) = 0;
};

constexpr std::uint32_t kGZIID_cIHitPersistResource = 0x3b93c5e7;
