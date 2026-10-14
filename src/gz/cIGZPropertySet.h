// cIGZPropertySetT<cKeyStandardTraitsT<unsigned int, 0xebb10cdd, 0x6bb22f3c>> — a map from
// uint32 property ids to typed cIGZAny values. Slots 3..10 (+0x18..+0x50), taken from the
// vtable of cGZPropertySetT @ 0x100b0a6a8. Interface id 0xebb10cdd.
#pragma once
#include <cstdint>

#include "gz/cIGZAny.h"
#include "gz/cIGZUnknown.h"

class cIGZPropertySet : public cIGZUnknown {
public:
    virtual bool SetValue(const std::uint32_t& key, const cIGZAny& value) = 0;       // [3] replaces any previous value
    virtual bool ClearValue(const std::uint32_t& key) = 0;                           // [4] false if absent
    virtual void ClearAll() = 0;                                                     // [5]
    virtual bool FirstKeyAndValue(std::uint32_t* key, cIGZAny** value) = 0;          // [6] starts an iteration
    virtual bool NextKeyAndValue(std::uint32_t* key, cIGZAny** value) = 0;           // [7]
    // [8] Reads the value into `out`: false if the key is absent *or* the stored type differs
    // from the type of `out` (no conversion is performed).
    virtual bool Value(const std::uint32_t& key, cIGZAny* out) const = 0;
    virtual bool HasValue(const std::uint32_t& key) const = 0;                       // [9]
    virtual std::uint32_t NumValues() const = 0;                                     // [10]
};

constexpr std::uint32_t kGZIID_cIGZPropertySet = 0xebb10cdd;
