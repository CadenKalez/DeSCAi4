// cIGZSndPropertyCache — per-instance and global sound properties kept in persisted property-set
// resources. Slots 3..15 (+0x18..+0x78) of cGZSndPropertyCache (vtable 0x100ab4f40); interface id
// 0x5d4c6b90, class id 0x9d4c6b95.
#pragma once
#include <cstdint>

#include "gz/cIGZAny.h"
#include "gz/cIGZUnknown.h"

class cIGZSndPropertyCache : public cIGZUnknown {
public:
    virtual bool AddInstance(std::uint32_t instance) = 0;                                              // [3]
    virtual bool RemoveInstance(std::uint32_t instance) = 0;                                           // [4]
    virtual bool HasInstance(std::uint32_t instance) = 0;                                              // [5]
    virtual bool SetInstanceProperty(std::uint32_t instance, std::uint32_t property, const cIGZAny& value) = 0;   // [6]
    virtual bool GetInstanceProperty(std::uint32_t instance, std::uint32_t property, cIGZAny* value) = 0;         // [7]
    virtual bool HasInstanceProperty(std::uint32_t instance, std::uint32_t property) = 0;              // [8]
    virtual bool RemoveInstanceProperty(std::uint32_t instance, std::uint32_t property) = 0;           // [9]
    virtual bool SetGlobalProperty(std::uint32_t property, const cIGZAny& value) = 0;                  // [10]
    virtual bool GetGlobalProperty(std::uint32_t property, cIGZAny* value) = 0;                        // [11]
    virtual bool HasGlobalProperty(std::uint32_t property) = 0;                                        // [12]
    virtual bool RemoveGlobalProperty(std::uint32_t property) = 0;                                     // [13]
    virtual bool SetInstancePropertyUint32(std::uint32_t instance, std::uint32_t property, std::uint32_t value) = 0;   // [14]
    virtual bool SetGlobalPropertyUint32(std::uint32_t property, std::uint32_t value) = 0;             // [15]
};
constexpr std::uint32_t kGZIID_cIGZSndPropertyCache = 0x5d4c6b90;
constexpr std::uint32_t kGZCLSID_cGZSndPropertyCache = 0x9d4c6b95;
