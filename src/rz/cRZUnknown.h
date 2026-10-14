// cRZUnknown — reference-counting mix-in used by cRZSystemService. Recovered from vtable
// 0x100a68770: QueryInterface/AddRef/Release, virtual destructors (slots 3-4), RemoveRef, RefCount.
// Layout: +0 vptr, +8 int refcount.
#pragma once
#include <cstdint>

#include "gz/cIGZUnknown.h"

class cRZUnknown : public cIGZUnknown {
public:
    cRZUnknown() = default;
    virtual ~cRZUnknown() = default;

    bool QueryInterface(std::uint32_t iid, void** out) override {  // @ 0x10003c8f8
        if (iid != kGZIID_cIGZUnknown) return false;
        *out = static_cast<cIGZUnknown*>(this);
        AddRef();
        return true;
    }
    std::uint32_t AddRef() override { return ++refCount_; }        // @ 0x10003c912
    // Deletes the object when the count is already 0 or 1 (never reports a negative count).
    std::uint32_t Release() override {                             // @ 0x10003c920
        if (refCount_ > 1) return --refCount_;
        delete this;
        return 0;
    }
    // Decrements without ever destroying; stays at 0. Returns the new count.
    virtual std::uint32_t RemoveRef() {                            // @ 0x10003c93c
        if (refCount_ != 0) --refCount_;
        return refCount_;
    }
    virtual std::uint32_t RefCount() const { return refCount_; }   // @ 0x10003c952

private:
    std::uint32_t refCount_ = 0;
};
