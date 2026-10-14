// cRZCriticalSection — recursive pthread mutex plus an owner-visible lock depth.
// Original: a malloc'd 0x48-byte block (pthread_mutex_t created with type 2 = recursive, int depth
// at +0x40). The port wraps std::recursive_mutex; the depth is only meaningful to the lock owner,
// exactly as in the original.
#pragma once
#include <mutex>

#include "gz/cIGZCriticalSection.h"

class cRZCriticalSection final : public cIGZCriticalSection {
public:
    cRZCriticalSection() = default;

    void Release() override { delete this; }
    void Lock() override;
    int Unlock() override;
    int TryLock() override;
    bool IsValid() const override { return true; }
    int IsLocked() const override { return depth_; }

    // The original hands out the native mutex handle (GetCriticalSectionHandle).
    std::recursive_mutex& NativeHandle() { return mutex_; }

private:
    std::recursive_mutex mutex_;
    int depth_ = 0;
};
