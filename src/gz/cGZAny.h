// cGZAny<T> and the concrete value classes AnyUint32, AnySint32, AnyFloat32, AnyBoolean (a
// 4-byte payload after the vptr, object size 16) and AnyString (a counted pointer to a
// cRZString). Recovered from the cGZAny<...> template instances (e.g. CopyFrom @ 0x1004c2e04,
// CopyTo @ 0x10047ab50, CompareTo @ 0x100473258).
//
// Comparison quirk kept from the binary: CompareTo walks the 8 bytes after the vptr as signed
// chars (so padding participates; the port keeps the padding zeroed).
#pragma once
#include <cstdint>
#include <cstring>
#include <new>

#include "gz/cIGZAny.h"
#include "rz/cRZString.h"

namespace gz_detail {
inline std::int32_t CompareBytes(const unsigned char* a, const unsigned char* b) {
    for (int i = 0; i < 8; ++i) {
        const auto ca = static_cast<signed char>(a[i]);
        const auto cb = static_cast<signed char>(b[i]);
        if (ca < cb) return -1;
        if (cb < ca) return 1;
    }
    return 0;
}
}  // namespace gz_detail

// CRTP base for the 4-byte value types. `Derived` provides the TypeID constant and a `value`.
template <class Derived, class Value, std::uint32_t kTypeId>
class cGZAnyScalar : public cIGZAny {
public:
    cGZAnyScalar() { std::memset(payload_, 0, sizeof payload_); }
    explicit cGZAnyScalar(Value v) : cGZAnyScalar() { set(v); }

    Value get() const { Value v; std::memcpy(&v, payload_, sizeof v); return v; }
    void set(Value v) { std::memcpy(payload_, &v, sizeof v); }

    bool CopyFrom(const cIGZAny& other) override {  // @ 0x1004c2e04
        if (other.TypeID() != TypeID()) return false;
        std::memcpy(payload_, static_cast<const cGZAnyScalar&>(other).payload_, sizeof payload_);
        return true;
    }
    bool CopyFromObject(const void* object) override {  // @ 0x100481520
        if (object == nullptr) return false;
        const auto* other = static_cast<const cGZAnyScalar*>(object);
        if (other->TypeID() != TypeID()) return false;
        std::memcpy(payload_, other->payload_, sizeof payload_);
        return true;
    }
    void CopyTo(void* destination) const override {  // @ 0x10047ab50
        auto* copy = new (destination) Derived(static_cast<const Derived&>(*this));
        (void)copy;
    }
    std::uint32_t TypeID() const override { return kTypeId; }
    std::uint32_t DataSize() const override { return 0x10; }
    std::int32_t CompareTo(const cIGZAny& other) const override {
        return gz_detail::CompareBytes(payload_, static_cast<const cGZAnyScalar&>(other).payload_);
    }
    std::int32_t CompareToObject(const void* object) const override {
        return gz_detail::CompareBytes(payload_, static_cast<const cGZAnyScalar*>(object)->payload_);
    }

private:
    unsigned char payload_[8];  // value in the first bytes, zero padding after
};

class AnyUint32 final : public cGZAnyScalar<AnyUint32, std::uint32_t, kAnyTypeUint32> {
public:
    AnyUint32() = default;
    explicit AnyUint32(std::uint32_t v) : cGZAnyScalar(v) {}
};
class AnySint32 final : public cGZAnyScalar<AnySint32, std::int32_t, kAnyTypeSint32> {
public:
    AnySint32() = default;
    explicit AnySint32(std::int32_t v) : cGZAnyScalar(v) {}
};
class AnyFloat32 final : public cGZAnyScalar<AnyFloat32, float, kAnyTypeFloat32> {
public:
    AnyFloat32() = default;
    explicit AnyFloat32(float v) : cGZAnyScalar(v) {}
};
class AnyBoolean final : public cGZAnyScalar<AnyBoolean, std::uint32_t, kAnyTypeBoolean> {
public:
    AnyBoolean() = default;
    explicit AnyBoolean(bool v) : cGZAnyScalar(v ? 1u : 0u) {}
    bool value() const { return get() != 0; }
};

// AnyString holds a reference to a shared cRZString (+8). Copies share the string; assignment
// AddRefs the new string before releasing the old one (cGZAny<AnyString>::CopyFrom @ 0x100473418).
class AnyString final : public cIGZAny {
public:
    AnyString() = default;
    explicit AnyString(const char* s) : string_(new cRZString(s)) { string_->AddRef(); }    // @ 0x10047328e
    explicit AnyString(const cIGZString& s) : string_(new cRZString(s)) { string_->AddRef(); }  // @ 0x10047358a
    AnyString(const AnyString& o) : string_(o.string_) { if (string_) string_->AddRef(); }
    AnyString& operator=(const AnyString&) = delete;
    ~AnyString() override { if (string_) string_->Release(); }                              // @ 0x10046fb66

    const cRZString* string() const { return string_; }

    bool CopyFrom(const cIGZAny& other) override {
        if (other.TypeID() != TypeID()) return false;
        Assign(static_cast<const AnyString&>(other).string_);
        return true;
    }
    bool CopyFromObject(const void* object) override {
        if (object == nullptr) return false;
        const auto* other = static_cast<const AnyString*>(object);
        if (other->TypeID() != TypeID()) return false;
        Assign(other->string_);
        return true;
    }
    void CopyTo(void* destination) const override { new (destination) AnyString(*this); }  // @ 0x100473502
    std::uint32_t TypeID() const override { return kAnyTypeString; }
    std::uint32_t DataSize() const override { return 0x10; }
    // Delegates to cRZString::CompareToString(other, caseSensitive = true) (slot +0x68 of the string).
    std::int32_t CompareTo(const cIGZAny& other) const override { return CompareStrings(static_cast<const AnyString&>(other).string_); }
    std::int32_t CompareToObject(const void* object) const override {
        return CompareStrings(static_cast<const AnyString*>(object)->string_);
    }

private:
    void Assign(cRZString* s) {
        if (s == string_) return;
        if (s) s->AddRef();
        cRZString* old = string_;
        string_ = s;
        if (old) old->Release();
    }
    std::int32_t CompareStrings(const cRZString* other) const {
        if (string_ == nullptr || other == nullptr) return string_ == other ? 0 : (string_ ? 1 : -1);
        return string_->CompareToString(*other, true);
    }
    cRZString* string_ = nullptr;
};
