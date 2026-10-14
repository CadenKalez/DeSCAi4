// cIGZSndInstanceCache — maps objects to the instance ids playing for them. Slots 3..11 (+0x18..
// +0x58) of cGZSndInstanceCache (vtable 0x100ab4e40); interface id 0x3df613a1, class id 0xbdf613b4.
#pragma once
#include <cstdint>
#include <list>

#include "gz/cIGZUnknown.h"

class cIGZSndInstanceCache : public cIGZUnknown {
public:
    virtual bool AddObject(std::uint32_t object) = 0;                                      // [3]
    virtual bool RemoveObject(std::uint32_t object) = 0;                                   // [4]
    virtual bool HasObject(std::uint32_t object) = 0;                                      // [5]
    virtual bool AddInstance(std::uint32_t object, std::uint32_t instance) = 0;            // [6]
    virtual bool RemoveInstance(std::uint32_t object, std::uint32_t instance) = 0;         // [7]
    virtual bool HasInstance(std::uint32_t object, std::uint32_t instance) = 0;            // [8]
    // [9]: the object's instance list, or null.
    virtual std::list<std::uint32_t>* GetObjectInstanceIdList(std::uint32_t object) = 0;
    virtual bool RemoveAllInstances(std::uint32_t instance) = 0;                           // [10]
    virtual bool FindFirstObjectFromInstance(std::uint32_t instance, std::uint32_t& object) = 0;   // [11]
};
constexpr std::uint32_t kGZIID_cIGZSndInstanceCache = 0x3df613a1;
constexpr std::uint32_t kGZCLSID_cGZSndInstanceCache = 0xbdf613b4;
