// cIGZSndDataSourceCreator — factory of sound data sources. Slots 3..5 (+0x18..+0x28) from
// cGZSndSegmentDataSourceCreator (vtable 0x100ab4390). Interface id 0x9b75709e.
#pragma once
#include <cstdint>

#include "gz/cIGZString.h"
#include "gz/cIGZVariant.h"

class cIGZSndDataSourceCreator : public cIGZUnknown {
public:
    // [3] +0x18: creates the data source for sound `id` and returns interface `iid` (AddRef'd).
    virtual bool CreateSndDataSource(std::uint32_t id, std::uint32_t flags, cIGZVariant* variant, std::uint32_t iid, void** out) = 0;
    virtual bool AddSndDataSourceDef(std::uint32_t id, cIGZString& definition) = 0;   // [4] +0x20
    virtual void RemoveSndDataSourceDef(std::uint32_t id) = 0;                        // [5] +0x28
};
constexpr std::uint32_t kGZIID_cIGZSndDataSourceCreator = 0x9b75709e;
