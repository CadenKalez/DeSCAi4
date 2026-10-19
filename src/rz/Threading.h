// cRZMutex (0x100044412..0x1000444b4), cRZThreadSignal (0x1001fbc4e..0x1001fbe1a) and FileChangeNotification: the
// framework's POSIX threading primitives and the (inert on macOS) file-change watcher. The mutex and the signal
// are plain objects whose vtable starts with Release (they are not COM objects); both hand their native handle
// out as a raw byte copy.
#pragma once
#include <pthread.h>

#include <cstdint>
#include <string>

#include "gz/cIGZString.h"
#include "rz/cRZString.h"

// cRZThread (only the static the application uses)
class cRZThread {
public:
    // @ 0x100038c5e: microseconds, passed on as milliseconds (MacTools::Sleep(us / 1000))
    static void SleepCurrentThread(std::uint32_t microseconds);
};

class cRZMutex {
public:
    explicit cRZMutex(const cIGZString& name);
    virtual ~cRZMutex();
    cRZMutex(const cRZMutex&) = delete;
    cRZMutex& operator=(const cRZMutex&) = delete;

    bool Release();                         // destroys the mutex; always reports false
    bool Lock();
    bool Unlock();
    bool TryLock();
    bool IsValid() const { return valid_; }
    bool GetMutexHandle(void* destination) const;   // copies the pthread_mutex_t

private:
    pthread_mutex_t mutex_{};               // 0x40 bytes
    bool valid_ = false;                    // +0x40 (pthread_mutex_init succeeded)
    std::string name_;
};

class cRZThreadSignal {
public:
    explicit cRZThreadSignal(int manualReset);
    virtual ~cRZThreadSignal();
    cRZThreadSignal(const cRZThreadSignal&) = delete;
    cRZThreadSignal& operator=(const cRZThreadSignal&) = delete;

    bool Release();
    bool Signal();
    bool Broadcast();
    bool Unsignal() { return true; }
    // 0 error, 1 signalled, 2 timed out (0xffffffff waits forever).
    int TimedWait(std::uint32_t milliseconds);
    bool IsValid() const { return true; }
    bool ManualReset() const { return manualReset_ != 0; }
    bool GetEventHandle(void* destination) const;   // copies the pthread_cond_t

private:
    pthread_cond_t cond_{};                 // 0x30 bytes
    int manualReset_ = 0;                   // +0x30
    pthread_mutex_t guard_{};               // the internal critical section (+0x40..)
};

// The file-change watcher is not implemented on this platform: everything succeeds and nothing is reported.
class FileChangeNotification {
public:
    bool Init() { return true; }
    bool Shutdown() { return true; }
    bool Start() { return true; }
    bool IsStarted() const { return false; }
    bool Stop() { return true; }
    bool GetWatchedDirectory(cIGZString& out) const;
    bool SetWatchedDirectory(const cIGZString&) { return true; }
    bool SetChangeTypeFlags(std::uint32_t) { return true; }
    bool SetOptionFlags(std::uint32_t) { return true; }
    void SetNotificationTarget(void*) {}
    cRZString& Directory() { return directory_; }    // the object is itself the watched-directory string

private:
    cRZString directory_;
};
