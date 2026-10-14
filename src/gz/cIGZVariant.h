// cIGZVariant — typed value holder (iid 0x60fd4d2b). Slot layout (3..133) recovered from the vtable
// of cRZVariant (0x100a6a7b0). The Ref* members are the array accessors: RefX() returns the stored
// array, RefX(data, count) stores a copy of `count` elements.
#pragma once
#include <cstddef>
#include <cstdint>

#include "gz/cIGZString.h"

class cIGZVariant : public cIGZUnknown {
public:
    // tagRZVariant::Type. Array types are the element type | 0x8000 (0x8000 alone is a byte blob).
    enum Type : std::uint16_t {
        kTypeNone = 0, kTypeBool = 1, kTypeUint8 = 2, kTypeSint8 = 3, kTypeUint16 = 4, kTypeSint16 = 5,
        kTypeUint32 = 6, kTypeSint32 = 7, kTypeUint64 = 8, kTypeSint64 = 9, kTypeFloat32 = 10,
        kTypeFloat64 = 11, kTypeChar = 12, kTypeRZUnicodeChar = 13, kTypeRZChar = 14, kTypeVoidPtr = 15,
        kTypeUnknown = 16, kTypeUintPtr = 17, kTypeArrayFlag = 0x8000,
    };

    virtual std::uint16_t GetType() const = 0;                                  // [3]
    virtual std::uint32_t GetCount() const = 0;                                 // [4]
    virtual bool Erase() = 0;                                                   // [5]
    virtual bool CopyFrom(const cIGZVariant& other) = 0;                        // [6]
    virtual bool CreateCopy(cIGZVariant** out) const = 0;                       // [7]
    virtual bool CanGetVal(std::uint16_t type) const = 0;                       // [8]

#define GZ_VARIANT_SCALAR(Name, T) \
    virtual bool GetVal##Name(T& value) const = 0; \
    virtual T GetVal##Name() const = 0; \
    virtual void SetVal##Name(T value) = 0;
    GZ_VARIANT_SCALAR(Bool, bool)                       // [9..11]
    GZ_VARIANT_SCALAR(Uint8, std::uint8_t)              // [12..14]
    GZ_VARIANT_SCALAR(Sint8, std::int8_t)               // [15..17]
    GZ_VARIANT_SCALAR(Uint16, std::uint16_t)            // [18..20]
    GZ_VARIANT_SCALAR(Sint16, std::int16_t)             // [21..23]
    GZ_VARIANT_SCALAR(Uint32, std::uint32_t)            // [24..26]
    GZ_VARIANT_SCALAR(Sint32, std::int32_t)             // [27..29]
    GZ_VARIANT_SCALAR(Uint64, std::uint64_t)            // [30..32]
    GZ_VARIANT_SCALAR(Sint64, std::int64_t)             // [33..35]
    GZ_VARIANT_SCALAR(UintPtr, std::uintptr_t)          // [36..38]
    GZ_VARIANT_SCALAR(Float32, float)                   // [39..41]
    GZ_VARIANT_SCALAR(Float64, double)                  // [42..44]
    GZ_VARIANT_SCALAR(Char, char)                       // [45..47]
    GZ_VARIANT_SCALAR(RZUnicodeChar, std::uint16_t)     // [48..50]
    GZ_VARIANT_SCALAR(RZChar, char)                     // [51..53]
    GZ_VARIANT_SCALAR(VoidPtr, void*)                   // [54..56]
#undef GZ_VARIANT_SCALAR
    virtual bool CreateValString(cIGZString** out) const = 0;                   // [57]
    virtual bool GetValString(cIGZString& out) const = 0;                       // [58]
    virtual void SetValString(const cIGZString& value) = 0;                     // [59]
    virtual bool GetValVoid(void* destination, std::uint32_t& size) const = 0; // [60]
    virtual void SetValVoid(const void* data, std::uint32_t size) = 0;         // [61]
    virtual bool GetValUnknown(std::uint32_t iid, void** out) = 0;              // [62]
    virtual cIGZUnknown* GetValUnknown() = 0;                                   // [63]
    virtual void SetValUnknown(cIGZUnknown* value) = 0;                         // [64]

    // [65..96] direct references to the stored scalar (no conversion).
#define GZ_VARIANT_AS(Name, T) \
    virtual T& As##Name() = 0; \
    virtual const T& As##Name() const = 0;
    GZ_VARIANT_AS(Bool, bool)
    GZ_VARIANT_AS(Uint8, std::uint8_t)
    GZ_VARIANT_AS(Sint8, std::int8_t)
    GZ_VARIANT_AS(Uint16, std::uint16_t)
    GZ_VARIANT_AS(Sint16, std::int16_t)
    GZ_VARIANT_AS(Uint32, std::uint32_t)
    GZ_VARIANT_AS(Sint32, std::int32_t)
    GZ_VARIANT_AS(Uint64, std::uint64_t)
    GZ_VARIANT_AS(Sint64, std::int64_t)
    GZ_VARIANT_AS(UintPtr, std::uintptr_t)
    GZ_VARIANT_AS(Float32, float)
    GZ_VARIANT_AS(Float64, double)
    GZ_VARIANT_AS(Char, char)
    GZ_VARIANT_AS(RZUnicodeChar, std::uint16_t)
    GZ_VARIANT_AS(RZChar, char)
    GZ_VARIANT_AS(VoidPtr, void*)
#undef GZ_VARIANT_AS

    // [97..133] arrays.
#define GZ_VARIANT_REF(Name, T) \
    virtual const T* Ref##Name() const = 0; \
    virtual void Ref##Name(const T* data, std::uint32_t count) = 0;
    GZ_VARIANT_REF(Bool, bool)
    GZ_VARIANT_REF(Uint8, std::uint8_t)
    GZ_VARIANT_REF(Sint8, std::int8_t)
    GZ_VARIANT_REF(Uint16, std::uint16_t)
    GZ_VARIANT_REF(Sint16, std::int16_t)
    GZ_VARIANT_REF(Uint32, std::uint32_t)
    GZ_VARIANT_REF(Sint32, std::int32_t)
    GZ_VARIANT_REF(Uint64, std::uint64_t)
    GZ_VARIANT_REF(UintPtr, std::uintptr_t)
    GZ_VARIANT_REF(Sint64, std::int64_t)
    GZ_VARIANT_REF(Float32, float)
    GZ_VARIANT_REF(Float64, double)
    GZ_VARIANT_REF(Char, char)
    GZ_VARIANT_REF(RZUnicodeChar, std::uint16_t)
    GZ_VARIANT_REF(RZChar, char)
#undef GZ_VARIANT_REF
    virtual void* const* RefVoidPtr() const = 0;                                // [127]
    virtual void RefVoidPtr(const void** data, std::uint32_t count) = 0;        // [128]
    virtual void* RefVoid() const = 0;                                          // [129]
    virtual void RefVoid(const void* data, std::uint32_t size) = 0;             // [130]
    virtual cIGZUnknown* RefIGZUnknown() const = 0;                             // [131]
    virtual bool RefIGZUnknown(std::uint32_t iid, void** out) = 0;              // [132]
    virtual void RefIGZUnknown(cIGZUnknown* value) = 0;                         // [133]

    virtual ~cIGZVariant() = default;                                           // [134],[135]
};
constexpr std::uint32_t kGZIID_cIGZVariant = 0x60fd4d2b;
