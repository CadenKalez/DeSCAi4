// cGZPropertySetT<cKeyStandardTraitsT<unsigned int,...>> — property set implementation.
// Layout of the original (0x30): +0 vptr, +8 refcount, +0x10 std::map<uint32, vector<uint8>>
// (begin/root/size), +0x28 iteration cursor. Each stored value is the raw bytes of a cIGZAny
// object built by CopyTo(); the port keeps that representation.
#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

#include "gz/cIGZPropertySet.h"

class cGZPropertySet : public cIGZPropertySet {
public:
    cGZPropertySet() = default;
    cGZPropertySet(const cGZPropertySet& other);              // operator= @ 0x1004737a4
    cGZPropertySet& operator=(const cGZPropertySet& other);
    virtual ~cGZPropertySet();                                // @ 0x100481c60 / 0x100481be8

    bool QueryInterface(std::uint32_t iid, void** out) override;  // 1 and 0xebb10cdd  @ 0x1004731e0
    std::uint32_t AddRef() override { return ++refCount_; }        // @ 0x1004c2a78
    std::uint32_t Release() override;                              // deletes when count < 2  @ 0x100473210

    bool SetValue(const std::uint32_t& key, const cIGZAny& value) override;  // @ 0x100472b20
    bool ClearValue(const std::uint32_t& key) override;                      // @ 0x100481b00
    void ClearAll() override;                                                // @ 0x100481b60
    bool FirstKeyAndValue(std::uint32_t* key, cIGZAny** value) override;     // @ 0x1004c2a94
    bool NextKeyAndValue(std::uint32_t* key, cIGZAny** value) override;      // @ 0x100484ad0
    bool Value(const std::uint32_t& key, cIGZAny* out) const override;       // @ 0x1004c2ad0
    bool HasValue(const std::uint32_t& key) const override;                  // @ 0x1004c2b20
    std::uint32_t NumValues() const override { return static_cast<std::uint32_t>(values_.size()); }  // @ 0x100472bc0

    // Number of values, usable without the interface.
    std::size_t size() const { return values_.size(); }
    std::int32_t refCount() const { return refCount_; }

private:
    struct Slot {
        std::vector<unsigned char> bytes;  // the stored cIGZAny object (16-byte aligned by the heap)
    };
    static void Destroy(Slot& slot);
    static cIGZAny* Object(Slot& slot) { return reinterpret_cast<cIGZAny*>(slot.bytes.data()); }
    static const cIGZAny* Object(const Slot& slot) { return reinterpret_cast<const cIGZAny*>(slot.bytes.data()); }

    std::int32_t refCount_ = 0;
    std::map<std::uint32_t, Slot> values_;
    std::map<std::uint32_t, Slot>::iterator cursor_ = values_.end();
};
