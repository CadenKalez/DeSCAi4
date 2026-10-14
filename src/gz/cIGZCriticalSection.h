// cIGZCriticalSection — recursive lock interface. Slots 0..5 (note: slot 0 is Release; this
// interface does not derive from cIGZUnknown). Recovered from cRZCriticalSection's vtable.
#pragma once

class cIGZCriticalSection {
public:
    virtual void Release() = 0;       // [0] deletes the lock
    virtual void Lock() = 0;          // [1]
    virtual int Unlock() = 0;         // [2] new depth, or 0 if the OS call failed
    virtual int TryLock() = 0;        // [3] depth after the attempt (unchanged when it failed)
    virtual bool IsValid() const = 0;  // [4] always true
    virtual int IsLocked() const = 0;  // [5] current recursion depth
    virtual ~cIGZCriticalSection() = default;
};
