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

class cHitList : public cIHitList {
public:
    cHitList();
    ~cHitList() override;

    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return ++refCount_; }
    std::uint32_t Release() override;  // deletes when the count is <= 1
    std::uint32_t GetRefCount() const { return static_cast<std::uint32_t>(refCount_); }

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

    // Resource parsing/serialisation (cHitList::ReadResource/WriteResource). Accepted layouts,
    // tried in this order:
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
