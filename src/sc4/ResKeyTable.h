// cSCResKeyTable / cSCResKeyEnumerator — an explicit list of resource keys (37 functions, 0x100527e6e..0x100528640).
// The table (class id 0x67336285, interface 0x273362a2) is a plain vector of (type, group, instance) keys that
// persists as a u32 count + triples; the enumerator (0xa787d86b) walks a private copy of the list.
// Layout of both: +0 vptr, +8 cRZRefCount, +0x18 vector begin, +0x20 end (+0x30 enumerator cursor).
#pragma once
#include <cstdint>
#include <vector>

#include "gz/cGZPersistResourceKey.h"
#include "gz/cIGZIStream.h"
#include "gz/cIGZOStream.h"
#include "gz/cIGZSerializable.h"
#include "rz/cRZRefCount.h"
#include "sc4/SCProperty.h"

namespace sc4 {

constexpr std::uint32_t kGZCLSID_cSCResKeyTable = 0x67336285, kGZIID_cISCResKeyTable = 0x273362a2;
constexpr std::uint32_t kGZIID_cISCResKeyEnumerator = 0xa787d86b;

class cISCResKeyEnumerator : public cIGZUnknown {
public:
    virtual bool Next(std::uint32_t count, cGZPersistResourceKey** keys, std::uint32_t& got) = 0;
    virtual bool Skip(std::uint32_t count) = 0;
    virtual void Reset() = 0;
    virtual bool Clone(cISCResKeyEnumerator** out) = 0;
};

class cSCResKeyEnumerator final : public cRZRefCount, public cISCResKeyEnumerator {
public:
    explicit cSCResKeyEnumerator(const std::vector<cGZPersistResourceKey>& keys) : keys_(keys) {}
    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { cRZRefCount::AddRef(); return RefCount(); }
    std::uint32_t Release() override { return static_cast<std::uint32_t>(cRZRefCount::Release()); }
    bool Next(std::uint32_t count, cGZPersistResourceKey** keys, std::uint32_t& got) override;
    bool Skip(std::uint32_t count) override;
    void Reset() override { cursor_ = 0; }
    bool Clone(cISCResKeyEnumerator** out) override;

private:
    std::vector<cGZPersistResourceKey> keys_;   // +0x18
    std::size_t cursor_ = 0;                    // +0x30 (the original's clone leaves it unset; here it starts at 0)
};

class cSCResKeyTable final : public cRZRefCount, public cIGZSerializable {
public:
    static cSCResKeyTable* Create() { return new cSCResKeyTable; }

    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { cRZRefCount::AddRef(); return RefCount(); }
    std::uint32_t Release() override { return static_cast<std::uint32_t>(cRZRefCount::Release()); }

    void GetEntry(std::uint32_t index, cGZPersistResourceKey& key) const { key = keys_[index]; }
    void SetEntry(std::uint32_t index, const cGZPersistResourceKey& key);
    void AddEntries(const cISCProperty* property);        // consecutive (group, type, instance) triples
    void AddEntry(const cGZPersistResourceKey& key) { keys_.push_back(key); }
    void RemoveEntry(std::uint32_t index);
    void RemoveEntry(const cGZPersistResourceKey& key);
    void EnumEntries(void (*fn)(const cGZPersistResourceKey&, void*), void* context) const;
    bool CreateEnumerator(cISCResKeyEnumerator** out) const;
    std::uint32_t Size() const { return static_cast<std::uint32_t>(keys_.size()); }
    void Resize(std::uint32_t size) { keys_.resize(size); }
    void Clear() { keys_.clear(); }
    bool IsExplicit() const { return true; }
    bool Write(cIGZOStream& stream) const override;
    bool Read(cIGZIStream& stream) override;
    std::uint32_t GetGZCLSID() const override { return kGZCLSID_cSCResKeyTable; }

private:
    cSCResKeyTable() = default;
    std::vector<cGZPersistResourceKey> keys_;   // +0x18
};

}  // namespace sc4
