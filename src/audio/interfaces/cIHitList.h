// cIHitList — an ordered set of integer ids from which Choose() draws without immediate repeats.
// Slots 3..16 (byte offsets +0x18..+0x80); cHitList adds the dtors, GetRefCount and the
// Read/WriteResource pair after them.
#pragma once
#include <cstdint>

#include "gz/cIGZString.h"
#include "gz/cIGZVariant.h"


class cIHitList : public cIGZUnknown {
public:
    virtual bool AddValue(std::int32_t value) = 0;                                  // [3]
    virtual bool AddVariant(const cIGZVariant& value) = 0;                          // [4] unsupported: false
    virtual bool Remove(std::int32_t value) = 0;                                    // [5] all copies
    virtual bool RemoveAll() = 0;                                                   // [6]
    virtual bool SetCriterion(std::int32_t historyLength) = 0;                      // [7]
    virtual std::int32_t Choose() = 0;                                              // [8]
    virtual std::int32_t NumItems() const = 0;                                      // [9]
    virtual bool GetVariantValueByIndex(std::uint32_t index, cIGZVariant** out) const = 0;       // [10] false
    virtual bool SetVariantValueByIndex(std::uint32_t index, const cIGZVariant& value) = 0;      // [11] false
    virtual bool GetIntValueByIndex(std::uint32_t index, std::int32_t* out) const = 0;           // [12]
    virtual bool GetStringValueByIndex(std::uint32_t index, cIGZString& out) const = 0;          // [13] false
    virtual bool SetIntValueByIndex(std::uint32_t index, std::int32_t value) = 0;                // [14]
    virtual bool SetStringValueByIndex(std::uint32_t index, const cIGZString& value) = 0;        // [15] false
    virtual bool RemoveByIndex(std::uint32_t index) = 0;                                         // [16]
};

constexpr std::uint32_t kGZIID_cIHitList = 0xdb1acfb2;
