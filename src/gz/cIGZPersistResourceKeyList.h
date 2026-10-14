// Key list / key filter interfaces of the persistence layer.
// Slots recovered from the vtables of cGZPersistResourceKeyList (0x100a98740) and the filter classes.
#pragma once
#include <cstdint>

#include "gz/cGZPersistResourceKey.h"
#include "gz/cIGZUnknown.h"

// iid 0x8865cd89. [3] IsKeyIncluded, [4]/[5] destructors (declared by the interface).
class cIGZPersistResourceKeyFilter : public cIGZUnknown {
public:
    virtual bool IsKeyIncluded(const cGZPersistResourceKey& key) = 0;  // [3]
    virtual ~cIGZPersistResourceKeyFilter() = default;                 // [4],[5]
};
constexpr std::uint32_t kGZIID_cIGZPersistResourceKeyFilter = 0x8865cd89;

class cIGZPersistResourceKeyList;
using ResourceKeyEnumFn = void (*)(const cGZPersistResourceKey&, void*);

// iid 0x656b8f12
class cIGZPersistResourceKeyList : public cIGZUnknown {
public:
    virtual bool Insert(const cGZPersistResourceKey& key) = 0;               // [3] appends (duplicates allowed)
    virtual bool Insert(const cIGZPersistResourceKeyList& other) = 0;        // [4]
    virtual bool Erase(const cGZPersistResourceKey& key) = 0;                // [5] removes the first match
    virtual bool EraseAll() = 0;                                             // [6]
    virtual void EnumKeys(ResourceKeyEnumFn callback, void* context) const = 0;  // [7]
    virtual bool IsPresent(const cGZPersistResourceKey& key) = 0;            // [8]
    virtual std::uint32_t Size() const = 0;                                  // [9]
    virtual const cGZPersistResourceKey* GetKey(std::uint32_t index) const = 0;  // [10] unchecked
    virtual ~cIGZPersistResourceKeyList() = default;                         // [11],[12]
};
constexpr std::uint32_t kGZIID_cIGZPersistResourceKeyList = 0x656b8f12;
