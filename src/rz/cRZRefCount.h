// cRZRefCount — minimal intrusive reference-count base (8 functions, 0x10003c830..0x10003c89a). Layout: +0 vptr,
// +8 count. Release deletes (through the deleting destructor in vslot 1) when the count is 0 or 1; RemoveRef
// decrements without ever destroying and stops at 0.
#pragma once
#include <cstdint>

class cRZRefCount {
public:
    cRZRefCount() = default;
    virtual ~cRZRefCount() = default;
    cRZRefCount(const cRZRefCount&) = delete;
    cRZRefCount& operator=(const cRZRefCount&) = delete;

    std::int32_t Release() {            // @ 0x10003c866
        if (count_ > 1) return --count_;
        delete this;
        return 0;
    }
    std::uint32_t RefCount() const { return count_; }   // @ 0x10003c882
    void AddRef() { ++count_; }                          // @ 0x10003c88c
    std::int32_t RemoveRef() {                           // @ 0x10003c89a
        if (count_ == 0) return 0;
        return --count_;
    }

private:
    std::uint32_t count_ = 0;   // +8
};
