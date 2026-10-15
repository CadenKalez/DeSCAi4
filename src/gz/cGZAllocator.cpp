#include "gz/cGZAllocator.h"

namespace gz {

bool cGZDefaultAllocator::QueryInterface(std::uint32_t iid, void** out) {
    if (iid != kGZIID_cIGZUnknown && iid != kGZIID_cIGZAllocator) return false;
    *out = this;
    AddRef();
    return true;
}

std::uint32_t cGZDefaultAllocator::Release() {
    if (refCount_ > 1) return --refCount_;
    delete this;
    return 0;
}

bool cGZAllocatorService::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZAllocatorService) {
        *out = static_cast<cIGZAllocator*>(this);
        AddRef();
        return true;
    }
    return cRZSystemService::QueryInterface(iid, out);
}

bool cRZExceptionNotification::QueryInterface(std::uint32_t iid, void** out) {
    if (iid != kGZIID_cIGZUnknown && iid != kGZIID_cIRZExceptionNotification) return false;
    *out = this;
    AddRef();
    return true;
}

std::uint32_t cRZExceptionNotification::Release() {
    if (refCount_ == 1) {
        delete this;
        return 0;
    }
    return --refCount_;
}

}  // namespace gz
