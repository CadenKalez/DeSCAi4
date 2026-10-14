#include "rz/cRZCriticalSection.h"

// cRZCriticalSection::Lock() @ 0x10003b38a
void cRZCriticalSection::Lock() {
    mutex_.lock();
    ++depth_;
}

// cRZCriticalSection::TryLock() @ 0x10003b3b0
int cRZCriticalSection::TryLock() {
    if (mutex_.try_lock()) ++depth_;
    return depth_;
}

// cRZCriticalSection::Unlock() @ 0x10003b3d6
int cRZCriticalSection::Unlock() {
    --depth_;
    const int depth = depth_;
    mutex_.unlock();
    return depth;
}
