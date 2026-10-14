// cRZUnknownList — list of AddRef'd objects (vtable 0x100a687b8, 21 functions).
// Quirks kept: Pop*/Peek* succeed only when the element answers the requested interface; the
// list-appending overloads push (front) the other list's elements as a block, preserving order.
#pragma once
#include <cstdint>
#include <list>

#include "gz/cIGZUnknownList.h"
#include "rz/cRZAutoRefCount.h"

class cRZUnknownList final : public cIGZUnknownList {
public:
    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return ++refCount_; }
    std::uint32_t Release() override;

    std::uint32_t Size() const override { return static_cast<std::uint32_t>(items_.size()); }
    bool GetElement(std::uint32_t index, std::uint32_t iid, void** out) const override;
    bool EnumElements(cIGZUnknownEnumerator& e, cIGZUnknown* context, std::uint32_t flags) override;
    bool PushFront(cIGZUnknown* element) override;
    bool PushFront(const cIGZUnknownList& other) override;
    bool PushBack(cIGZUnknown* element) override;
    bool PushBack(const cIGZUnknownList& other) override;
    bool PeekFront(std::uint32_t iid, void** out) const override;
    bool PeekBack(std::uint32_t iid, void** out) const override;
    bool PopFront(std::uint32_t iid, void** out) override;
    bool PopBack(std::uint32_t iid, void** out) override;
    bool Clear() override { items_.clear(); return true; }
    void CopyFrom(const cIGZUnknownList& other) override { Clear(); PushFront(other); }
    void CreateClone(cIGZUnknownList** out) const override;
    bool ConstEnumElements(cIGZUnknownEnumerator& e, cIGZUnknown* context, std::uint32_t flags) const override;

private:
    std::list<cRZAutoRefCount<cIGZUnknown>> items_;
    std::uint32_t refCount_ = 0;
};
