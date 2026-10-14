// cIGZAny — a typed value container used by property sets. Vtable (no QueryInterface/AddRef):
// [0] dtor, [1] deleting dtor, [2] CopyFrom(const cIGZAny&), [3] CopyFrom(const void*),
// [4] CopyTo(void*), [5] TypeID, [6] DataSize, [7] MemberDataSize, [8] CompareTo(const cIGZAny&),
// [9] CompareTo(const void*). Recovered from the vtables of AnyUint32 @ 0x100b0a720 and friends.
//
// A property set stores each value as the raw bytes of such an object (vptr + payload), built with
// CopyTo(); `CopyFrom(const void*)` reads a stored object through its own vptr, which is why the
// reader and the stored value must share a TypeID. The port keeps that contract exactly.
#pragma once
#include <cstdint>

class cIGZAny {
public:
    virtual ~cIGZAny() = default;                                         // [0],[1]
    virtual bool CopyFrom(const cIGZAny& other) = 0;                      // [2] false if TypeIDs differ
    virtual bool CopyFromObject(const void* object) = 0;                  // [3] object = another cIGZAny
    virtual void CopyTo(void* destination) const = 0;                     // [4] placement-constructs a copy
    virtual std::uint32_t TypeID() const = 0;                             // [5]
    virtual std::uint32_t DataSize() const = 0;                           // [6] sizeof the whole object (16)
    virtual std::uint32_t MemberDataSize() const { return DataSize() - 8; }  // [7] payload bytes after the vptr
    virtual std::int32_t CompareTo(const cIGZAny& other) const = 0;       // [8] -1 / 0 / 1
    virtual std::int32_t CompareToObject(const void* object) const = 0;   // [9]
};

// Type ids of the value classes (cGZAny<T>::TypeID).
constexpr std::uint32_t kAnyTypeUint32 = 0xeb61e4f7;
constexpr std::uint32_t kAnyTypeFloat32 = 0xabc78708;
constexpr std::uint32_t kAnyTypeBoolean = 0xcba908e1;
constexpr std::uint32_t kAnyTypeSint32 = 0x0c264712;
constexpr std::uint32_t kAnyTypeString = 0x0b8bea18;
