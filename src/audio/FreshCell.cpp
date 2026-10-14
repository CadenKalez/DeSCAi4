#include "audio/FreshCell.h"

// cFreshCell::QueryInterface(unsigned int, void**) @ 0x1004ca0e2
bool cFreshCell::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIFreshCell || iid == kGZIID_cIGZUnknown) {
        *out = this;
        AddRef();
        return true;
    }
    return false;
}

// cFreshCell::Release() @ 0x1004ca094 — the original has no virtual destructor; Release tears
// down the connection list and filename itself when the count is <= 1.
std::uint32_t cFreshCell::Release() {
    if (refCount_ < 2) {
        delete this;
        return 0;
    }
    return static_cast<std::uint32_t>(--refCount_);
}
