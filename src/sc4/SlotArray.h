// cSC4SlotArray<T> — the original's slot pool (an index-stable, free-list-backed std::vector<T>). The overlay manager
// keeps three of them (cOverlay, cDecal, cRingDecal) and the same template is used elsewhere in the engine, so the
// four instantiations the port needs live here.
//
// Original layout, recovered from the three AddSlot instantiations (0x1007285fc / 0x100728e24 / 0x100729170) and
// cSC4SlotArray<cOverlay>::Clear (0x100727cac):
//   +0x00 std::vector<T> begin      +0x08 end       +0x10 capacity end
//   +0x18 u32 freeListHead (-1 = none)              +0x1c u32 freeListTail
// A slot's first 4 bytes are the free-list link: -1 while the slot is in use, 0 for the last free slot, else the
// index of the next free slot. AddSlot either pops the head of the free list or appends a new element; the new
// element keeps T's first word untouched until AddSlot overwrites it with -1.
//
// Quirks kept:
//  * AddSlot writes -1 into the slot's link *after* copying, and, when reusing the tail of the chain, resets both
//    free-list words to -1 before copying (so the chain is rebuilt one slot at a time).
//  * Both Clear and the destructor leave the free list reset to -1 but keep the vector's capacity.
//  * The free-list link lives inside T (offset 0), so T must be layout-compatible with a leading int32.
#pragma once
#include <cstdint>
#include <vector>

namespace sc4 {

template <class T>
class cSC4SlotArray {
public:
    static_assert(sizeof(T) >= 4, "T must carry the free-list link in its first word");

    std::uint32_t AddSlot(const T& value);   // 0x1007285fc / 0x100728e24 / 0x100729170 (cOverlay / cDecal / cRingDecal)
    void Clear();                            // 0x100727cac (cOverlay; the POD instantiations use the vector itself)
    T& operator[](std::uint32_t index) { return slots_[index]; }
    const T& operator[](std::uint32_t index) const { return slots_[index]; }
    std::size_t size() const { return slots_.size(); }
    bool empty() const { return slots_.empty(); }
    const std::vector<T>& slots() const { return slots_; }

    std::vector<T> slots_;                                  // +0x00 / +0x08 / +0x10
    std::uint32_t freeListHead_ = 0xffffffffu;             // +0x18
    std::uint32_t freeListTail_ = 0xffffffffu;             // +0x1c
};

template <class T>
std::uint32_t cSC4SlotArray<T>::AddSlot(const T& value) {
    std::uint32_t slot = freeListHead_;
    if (slot == 0xffffffffu) {
        slots_.push_back(value);
        slots_.back().freeLink = -1;
        return static_cast<std::uint32_t>(slots_.size() - 1);
    }
    if (freeListHead_ == freeListTail_) {
        // last slot of the chain: the next AddSlot starts a fresh chain
        freeListHead_ = 0xffffffffu;
        freeListTail_ = 0xffffffffu;
    } else {
        freeListHead_ = static_cast<std::uint32_t>(slots_[slot].freeLink);
    }
    slots_[slot] = value;
    slots_[slot].freeLink = -1;
    return slot;
}

template <class T>
void cSC4SlotArray<T>::Clear() {
    slots_.clear();
    freeListHead_ = 0xffffffffu;
    freeListTail_ = 0xffffffffu;
}

}  // namespace sc4
