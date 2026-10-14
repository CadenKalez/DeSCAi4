// cIGZSndMultipleSources — a sound with a set of source ids. Slots 3..7 (+0x18..+0x38), taken
// from the sub-vtable at offset +0x30 of cHitTrackPlayer. Interface id 0xdd583e2d.
#pragma once
#include <cstdint>
#include <set>

#include "gz/cIGZUnknown.h"

class cIGZSndMultipleSources : public cIGZUnknown {
public:
    virtual std::set<std::uint32_t> SourceList() = 0;           // [3] returned by value
    virtual void AddSource(std::uint32_t sourceId) = 0;         // [4]
    virtual bool RemoveSource(std::uint32_t sourceId) = 0;      // [5]
    virtual void RemoveAllSources() = 0;                        // [6]
    virtual bool UpdateSources() = 0;                           // [7]
};

constexpr std::uint32_t kGZIID_cIGZSndMultipleSources = 0xdd583e2d;
