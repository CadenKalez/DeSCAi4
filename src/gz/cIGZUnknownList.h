// cIGZUnknownList / cIGZUnknownEnumerator — reference-counted object list. Slots 3..17 from the
// vtable of cRZUnknownList (0x100a687b8); iid 0x12e.
#pragma once
#include <cstdint>

#include "gz/cIGZUnknown.h"

class cIGZUnknownEnumerator : public cIGZUnknown {
public:
    // [3] called once per element; `context`/`flags` are the values passed to EnumElements.
    virtual void Enumerate(cIGZUnknown* element, cIGZUnknown* context, std::uint32_t flags) = 0;
};

class cIGZUnknownList : public cIGZUnknown {
public:
    virtual std::uint32_t Size() const = 0;                                                     // [3]
    virtual bool GetElement(std::uint32_t index, std::uint32_t iid, void** out) const = 0;     // [4]
    virtual bool EnumElements(cIGZUnknownEnumerator& e, cIGZUnknown* context, std::uint32_t flags) = 0;  // [5]
    virtual bool PushFront(cIGZUnknown* element) = 0;                                           // [6]
    virtual bool PushFront(const cIGZUnknownList& other) = 0;                                   // [7]
    virtual bool PushBack(cIGZUnknown* element) = 0;                                            // [8]
    virtual bool PushBack(const cIGZUnknownList& other) = 0;                                    // [9]
    virtual bool PeekFront(std::uint32_t iid, void** out) const = 0;                            // [10]
    virtual bool PeekBack(std::uint32_t iid, void** out) const = 0;                             // [11]
    virtual bool PopFront(std::uint32_t iid, void** out) = 0;                                   // [12]
    virtual bool PopBack(std::uint32_t iid, void** out) = 0;                                    // [13]
    virtual bool Clear() = 0;                                                                   // [14]
    virtual void CopyFrom(const cIGZUnknownList& other) = 0;                                    // [15]
    virtual void CreateClone(cIGZUnknownList** out) const = 0;                                  // [16]
    virtual bool ConstEnumElements(cIGZUnknownEnumerator& e, cIGZUnknown* context, std::uint32_t flags) const = 0;  // [17]
    virtual ~cIGZUnknownList() = default;                                                       // [18],[19]
};
constexpr std::uint32_t kGZIID_cIGZUnknownList = 0x0000012e;
