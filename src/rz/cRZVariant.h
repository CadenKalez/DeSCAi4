// cRZVariant — the framework's typed value holder (class implementing cIGZVariant).
// Recovered from Sim City 4 Deluxe Editionsub 1.2.1 (vtable 0x100a6a7b0, 163 functions).
//
// Layout of the original (0x18 bytes): +8 value (or the array pointer), +0x10 u16 reference count,
// +0x12 u16 type (array types have 0x8000 set), +0x14 u32 element count.
// Behaviour kept: a getter whose type differs converts through a temporary (numeric conversions via
// 64-bit integer or double, anything non-numeric via the string form); setting a value of another
// type discards the old one; arrays are always copied in; GetValString on a non-string type fills the
// converted text but still returns false.
// Deviations: float/double text uses "%.9g" / "%.17g" shortened (the original calls RZFtoaEnglishA),
// and VoidPtr text is formatted from the low 32 bits like the original's "%016x".
#pragma once
#include <cstdint>
#include <cstring>
#include <string>

#include "gz/cIGZVariant.h"

class cRZVariant final : public cIGZVariant {
public:
    cRZVariant() = default;
    cRZVariant(const cRZVariant& other) { *this = other; }
    explicit cRZVariant(const cIGZVariant& other) { *this = other; }
    explicit cRZVariant(void* pointer) { SetValVoidPtr(pointer); }
    explicit cRZVariant(const cIGZString& text) { SetValString(text); }
    explicit cRZVariant(std::uint8_t v) { SetValUint8(v); }
    explicit cRZVariant(std::uint32_t v) { SetValUint32(v); }
    explicit cRZVariant(float v) { SetValFloat32(v); }
    explicit cRZVariant(double v) { SetValFloat64(v); }
    cRZVariant(const void* data, std::uint32_t size, std::uint32_t extraFlags);   // blob with user flags
    explicit cRZVariant(cIGZUnknown* object) { SetValUnknown(object); }
    ~cRZVariant() override { Destroy(); }

    cRZVariant& operator=(const cIGZVariant& other);
    cRZVariant& operator=(const cRZVariant& other);

    // cIGZUnknown (16-bit reference count, deletes at 1)
    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return ++refCount_; }
    std::uint32_t Release() override;

    std::uint16_t GetType() const override { return type_; }
    std::uint32_t GetCount() const override { return count_; }
    bool Erase() override { Destroy(); return true; }
    bool CopyFrom(const cIGZVariant& other) override { *this = other; return true; }
    bool CreateCopy(cIGZVariant** out) const override;
    bool CanGetVal(std::uint16_t) const override { return true; }

#define RZV_SCALAR(Name, T, Tag) \
    bool GetVal##Name(T& value) const override { return GetScalar<T>(Tag, value); } \
    T GetVal##Name() const override { T v{}; GetScalar<T>(Tag, v); return v; } \
    void SetVal##Name(T value) override { SetScalar<T>(Tag, value); }
    RZV_SCALAR(Bool, bool, kTypeBool)
    RZV_SCALAR(Uint8, std::uint8_t, kTypeUint8)
    RZV_SCALAR(Sint8, std::int8_t, kTypeSint8)
    RZV_SCALAR(Uint16, std::uint16_t, kTypeUint16)
    RZV_SCALAR(Sint16, std::int16_t, kTypeSint16)
    RZV_SCALAR(Uint32, std::uint32_t, kTypeUint32)
    RZV_SCALAR(Sint32, std::int32_t, kTypeSint32)
    RZV_SCALAR(Uint64, std::uint64_t, kTypeUint64)
    RZV_SCALAR(Sint64, std::int64_t, kTypeSint64)
    RZV_SCALAR(UintPtr, std::uintptr_t, kTypeUintPtr)
    RZV_SCALAR(Float32, float, kTypeFloat32)
    RZV_SCALAR(Float64, double, kTypeFloat64)
    RZV_SCALAR(Char, char, kTypeChar)
    RZV_SCALAR(RZUnicodeChar, std::uint16_t, kTypeRZUnicodeChar)
    RZV_SCALAR(RZChar, char, kTypeRZChar)
#undef RZV_SCALAR
    bool GetValVoidPtr(void*& value) const override;
    void* GetValVoidPtr() const override { void* v = nullptr; GetValVoidPtr(v); return v; }
    void SetValVoidPtr(void* value) override;

    bool CreateValString(cIGZString** out) const override;
    bool GetValString(cIGZString& out) const override;
    void SetValString(const cIGZString& value) override { RefRZChar(value.Data(), value.Strlen()); }
    bool GetValVoid(void* destination, std::uint32_t& size) const override;
    void SetValVoid(const void* data, std::uint32_t size) override { RefVoid(data, size); }
    bool GetValUnknown(std::uint32_t iid, void** out) override { return RefIGZUnknown(iid, out); }
    cIGZUnknown* GetValUnknown() override { return type_ == kTypeUnknown ? object_ : nullptr; }
    void SetValUnknown(cIGZUnknown* value) override { RefIGZUnknown(value); }

#define RZV_AS(Name, T, Member) \
    T& As##Name() override { return reinterpret_cast<T&>(value_.Member); } \
    const T& As##Name() const override { return reinterpret_cast<const T&>(value_.Member); }
    RZV_AS(Bool, bool, bytes)
    RZV_AS(Uint8, std::uint8_t, bytes)
    RZV_AS(Sint8, std::int8_t, bytes)
    RZV_AS(Uint16, std::uint16_t, bytes)
    RZV_AS(Sint16, std::int16_t, bytes)
    RZV_AS(Uint32, std::uint32_t, bytes)
    RZV_AS(Sint32, std::int32_t, bytes)
    RZV_AS(Uint64, std::uint64_t, bytes)
    RZV_AS(Sint64, std::int64_t, bytes)
    RZV_AS(UintPtr, std::uintptr_t, bytes)
    RZV_AS(Float32, float, bytes)
    RZV_AS(Float64, double, bytes)
    RZV_AS(Char, char, bytes)
    RZV_AS(RZUnicodeChar, std::uint16_t, bytes)
    RZV_AS(RZChar, char, bytes)
    RZV_AS(VoidPtr, void*, bytes)
#undef RZV_AS

#define RZV_REF(Name, T, Tag) \
    const T* Ref##Name() const override { return static_cast<const T*>(value_.pointer); } \
    void Ref##Name(const T* data, std::uint32_t count) override { SetArray(Tag | kTypeArrayFlag, data, count, sizeof(T)); }
    RZV_REF(Bool, bool, kTypeBool)
    RZV_REF(Uint8, std::uint8_t, kTypeUint8)
    RZV_REF(Sint8, std::int8_t, kTypeSint8)
    RZV_REF(Uint16, std::uint16_t, kTypeUint16)
    RZV_REF(Sint16, std::int16_t, kTypeSint16)
    RZV_REF(Uint32, std::uint32_t, kTypeUint32)
    RZV_REF(Sint32, std::int32_t, kTypeSint32)
    RZV_REF(Uint64, std::uint64_t, kTypeUint64)
    RZV_REF(UintPtr, std::uintptr_t, kTypeUintPtr)
    RZV_REF(Sint64, std::int64_t, kTypeSint64)
    RZV_REF(Float32, float, kTypeFloat32)
    RZV_REF(Float64, double, kTypeFloat64)
    RZV_REF(Char, char, kTypeChar)
    RZV_REF(RZUnicodeChar, std::uint16_t, kTypeRZUnicodeChar)
    RZV_REF(RZChar, char, kTypeRZChar)
#undef RZV_REF
    void* const* RefVoidPtr() const override { return static_cast<void* const*>(value_.pointer); }
    void RefVoidPtr(const void** data, std::uint32_t count) override { SetArray(kTypeVoidPtr | kTypeArrayFlag, data, count, sizeof(void*)); }
    void* RefVoid() const override { return value_.pointer; }
    void RefVoid(const void* data, std::uint32_t size) override { SetArray(kTypeArrayFlag, data, size, 1); }
    cIGZUnknown* RefIGZUnknown() const override { return object_; }
    bool RefIGZUnknown(std::uint32_t iid, void** out) override;
    void RefIGZUnknown(cIGZUnknown* value) override;

private:
    template <class T> bool GetScalar(std::uint16_t tag, T& out) const;
    template <class T> void SetScalar(std::uint16_t tag, T value);
    void SetArray(std::uint16_t type, const void* data, std::uint32_t count, std::size_t elementSize);
    void Destroy();
    static bool ConvertTypes(const cRZVariant& from, cRZVariant& to);   // `to` carries the target type
    std::string ToString() const;
    bool FromString(const std::string& text, cRZVariant& to) const;

    union Value {
        std::uint8_t bytes[8];
        void* pointer;
    } value_{};
    cIGZUnknown* object_ = nullptr;   // mirrors value_.pointer for kTypeUnknown
    std::uint16_t refCount_ = 0;
    std::uint16_t type_ = kTypeNone;
    std::uint32_t count_ = 0;
};

template <class T> inline bool cRZVariant::GetScalar(std::uint16_t tag, T& out) const {
    if (type_ == tag) {
        std::memcpy(&out, value_.bytes, sizeof(T));
        return true;
    }
    cRZVariant target;
    target.type_ = tag;
    if (!ConvertTypes(*this, target)) return false;
    std::memcpy(&out, target.value_.bytes, sizeof(T));
    return true;
}

template <class T> inline void cRZVariant::SetScalar(std::uint16_t tag, T value) {
    if (type_ != kTypeNone && type_ != tag) Destroy();
    type_ = tag;
    std::memset(value_.bytes, 0, sizeof value_.bytes);
    std::memcpy(value_.bytes, &value, sizeof(T));
}

