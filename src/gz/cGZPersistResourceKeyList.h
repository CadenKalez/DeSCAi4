// cGZPersistResourceKeyList and the stock key filters (type / group / type+group).
// Recovered from Sim City 4 Deluxe Editionsub 1.2.1 (vtable 0x100a98740 and the filter vtables).
#pragma once
#include <cstdint>
#include <deque>

#include "gz/cIGZPersistResourceKeyList.h"

class cGZPersistResourceKeyList : public cIGZPersistResourceKeyList {
public:
    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return ++refCount_; }
    std::uint32_t Release() override;

    static void InsertKeyCallback(const cGZPersistResourceKey& key, void* list);   // @ 0x10032b5e8: appends to the list in `list`
    bool Insert(const cGZPersistResourceKey& key) override;
    bool Insert(const cIGZPersistResourceKeyList& other) override;
    bool Erase(const cGZPersistResourceKey& key) override;
    bool EraseAll() override;
    void EnumKeys(ResourceKeyEnumFn callback, void* context) const override;
    bool IsPresent(const cGZPersistResourceKey& key) override;
    std::uint32_t Size() const override { return static_cast<std::uint32_t>(keys_.size()); }
    const cGZPersistResourceKey* GetKey(std::uint32_t index) const override { return &keys_[index]; }

private:
    std::uint32_t refCount_ = 0;
    std::deque<cGZPersistResourceKey> keys_;
};

// Common refcounted base of the stock filters (refcount at +8, iid 0x8865cd89).
class cGZPersistResourceKeyFilter : public cIGZPersistResourceKeyFilter {
public:
    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return ++refCount_; }
    std::uint32_t Release() override;

private:
    std::uint32_t refCount_ = 0;
};

class cGZPersistResourceKeyFilterByType final : public cGZPersistResourceKeyFilter {
public:
    explicit cGZPersistResourceKeyFilterByType(std::uint32_t type) : type_(type) {}
    bool IsKeyIncluded(const cGZPersistResourceKey& key) override { return key.type == type_; }
private:
    std::uint32_t type_;
};

class cGZPersistResourceKeyFilterByGroup final : public cGZPersistResourceKeyFilter {
public:
    explicit cGZPersistResourceKeyFilterByGroup(std::uint32_t group) : group_(group) {}
    bool IsKeyIncluded(const cGZPersistResourceKey& key) override { return key.group == group_; }
private:
    std::uint32_t group_;
};

class cGZPersistResourceKeyFilterByTypeAndGroup final : public cGZPersistResourceKeyFilter {
public:
    cGZPersistResourceKeyFilterByTypeAndGroup(std::uint32_t type, std::uint32_t group)
        : type_(type), group_(group) {}
    bool IsKeyIncluded(const cGZPersistResourceKey& key) override {
        return key.type == type_ && key.group == group_;
    }
private:
    std::uint32_t type_, group_;
};
