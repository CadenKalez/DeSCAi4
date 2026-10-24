// cHitList — "pick one of these ids at random, but don't repeat recent picks".
// Recovered from Sim City 4 Deluxe Editionsub 1.2.1.
//
// Original layout (0x48): +0x00 vptr, +0x08 refcount, +0x10 std::list<int> available,
// +0x28 std::list<int> recent, +0x40 int criterion, +0x44 bool criterionExplicit.
// Here both lists are contiguous containers; observable behaviour is identical.
//
// Semantics: Choose() removes a uniformly random entry from `available`, appends it to `recent`
// and, while |recent| > criterion, moves the oldest recent entry back to `available`.
// When `available` is empty the oldest recent entry is re-appended to `recent` and returned.
#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

#include "audio/interfaces/cIHitList.h"
#include "gz/cIGZIStream.h"
#include "gz/cIGZOStream.h"

class cHitList : public cIHitList {
public:
    cHitList();
    cHitList(const cHitList& other);  // copies lists, criterion and refcount, like the resource copy path
    virtual ~cHitList();

    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return ++refCount_; }
    std::uint32_t Release() override;  // deletes when the count is <= 1
    // Own virtuals after the destructor pair: [19] GetRefCount, [20] ReadResource, [21] WriteResource.
    virtual std::uint32_t GetRefCount() const { return static_cast<std::uint32_t>(refCount_); }  // @ 0x100482970

    bool AddValue(std::int32_t value) override;
    bool AddVariant(const cIGZVariant&) override { return false; }
    bool Remove(std::int32_t value) override;
    bool RemoveAll() override;
    bool SetCriterion(std::int32_t historyLength) override;
    std::int32_t Choose() override;
    std::int32_t NumItems() const override { return static_cast<std::int32_t>(available_.size() + recent_.size()); }
    bool GetVariantValueByIndex(std::uint32_t, cIGZVariant**) const override { return false; }
    bool SetVariantValueByIndex(std::uint32_t, const cIGZVariant&) override { return false; }
    bool GetIntValueByIndex(std::uint32_t index, std::int32_t* out) const override;
    bool GetStringValueByIndex(std::uint32_t, cIGZString&) const override { return false; }
    bool SetIntValueByIndex(std::uint32_t index, std::int32_t value) override;
    bool SetStringValueByIndex(std::uint32_t, const cIGZString&) override { return false; }
    bool RemoveByIndex(std::uint32_t index) override;

    // cHitList::ReadResource / WriteResource — the persist-resource stream entry points (own vtable
    // slots [20]/[21], after GetRefCount at [19]). The original reads the stream's whole record:
    // it QueryInterfaces the stream for iid 0xc56b8f08 (cIGZPersistDBSerialRecord), takes the size
    // from slot +0x1a0 (GetSize) and copies the record with slot +0xd8 (GetFieldVoid), then parses
    // the bytes and releases the record. Write emits the v1 layout through SetUint32 (slot +0x48).
    // Both ignore the flags and user-data arguments.
    virtual bool ReadResource(cIGZIStream& stream, std::uint32_t flags, cIGZUnknown* userData);   // 0x100482366
    virtual bool WriteResource(cIGZOStream& stream, std::uint32_t flags, cIGZUnknown* userData);  // 0x100482812

    // Resource parsing/serialisation logic without the stream. Accepted layouts, tried in order:
    //   text   : dword n, then n bytes "a,b,lo-hi,..." (ids and inclusive ranges)
    //   v1     : dword 1, dword count, count x int32
    //   v2     : dword count, count x int32
    // On success the criterion becomes count/3 + 1. Returns false for an unrecognised layout.
    bool ReadResourceBytes(const std::uint8_t* data, std::uint32_t size);
    // Appends [1, count, available..., recent...] (the v1 layout) to `out`.
    void WriteResourceDwords(std::vector<std::uint32_t>& out) const;

    static std::int32_t LiveListCount() noexcept;  // g_lNumHitLists

private:
    // Finds the container/slot for a combined index (available first, then recent).
    std::int32_t* ElementAt(std::uint32_t index);
    const std::int32_t* ElementAt(std::uint32_t index) const;
    void RecomputeAutoCriterion() { if (!criterionExplicit_) criterion_ = static_cast<std::int32_t>((available_.size() + recent_.size()) >> 1); }
    std::int32_t PickRandomIndex(std::int32_t range);

    std::int32_t refCount_ = 0;
    std::vector<std::int32_t> available_;
    std::deque<std::int32_t> recent_;
    std::int32_t criterion_ = 10;
    bool criterionExplicit_ = false;
};
