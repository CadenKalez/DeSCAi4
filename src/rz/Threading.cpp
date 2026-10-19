#include "rz/Threading.h"

#include "platform/MacTools.h"

#include <cerrno>
#include <cstring>
#include <ctime>

cRZMutex::cRZMutex(const cIGZString& name) : name_(name.ToChar()) {
    valid_ = pthread_mutex_init(&mutex_, nullptr) != -1;
}

cRZMutex::~cRZMutex() = default;

bool cRZMutex::Release() {
    delete this;
    return false;
}

bool cRZMutex::Lock() { return pthread_mutex_lock(&mutex_) == 0; }
bool cRZMutex::TryLock() { return pthread_mutex_trylock(&mutex_) == 0; }
bool cRZMutex::Unlock() { return pthread_mutex_unlock(&mutex_) == 0; }

bool cRZMutex::GetMutexHandle(void* destination) const {
    std::memcpy(destination, &mutex_, sizeof mutex_);
    return true;
}

cRZThreadSignal::cRZThreadSignal(int manualReset) : manualReset_(manualReset) {
    pthread_cond_init(&cond_, nullptr);
    pthread_mutex_init(&guard_, nullptr);
}

cRZThreadSignal::~cRZThreadSignal() {
    pthread_cond_destroy(&cond_);
    pthread_mutex_destroy(&guard_);
}

bool cRZThreadSignal::Release() {
    delete this;
    return false;
}

bool cRZThreadSignal::Signal() { return pthread_cond_signal(&cond_) == 0; }
bool cRZThreadSignal::Broadcast() { return pthread_cond_broadcast(&cond_) == 0; }

// The wait is done against a private, never-locked mutex (as in the original), so on a platform that checks the
// owner the wait reports an error immediately and the result is "timed out".
int cRZThreadSignal::TimedWait(std::uint32_t milliseconds) {
    pthread_mutex_lock(&guard_);
    pthread_mutex_t local;
    pthread_mutex_init(&local, nullptr);
    int result;
    if (milliseconds == 0xffffffffu) {
        result = pthread_cond_wait(&cond_, &local);
    } else {
        timespec deadline{};
        deadline.tv_sec = time(nullptr) + milliseconds / 1000;
        deadline.tv_nsec = static_cast<long>((milliseconds % 1000) * 1000000);
        result = pthread_cond_timedwait(&cond_, &local, &deadline);
        if (result == -1) result = errno == EAGAIN ? 1 : -1;
    }
    pthread_mutex_destroy(&local);
    pthread_mutex_unlock(&guard_);
    return result + 1;
}

bool cRZThreadSignal::GetEventHandle(void* destination) const {
    std::memcpy(destination, &cond_, sizeof cond_);
    return true;
}

bool FileChangeNotification::GetWatchedDirectory(cIGZString& out) const {
    out.Copy(directory_);
    return true;
}

void cRZThread::SleepCurrentThread(std::uint32_t microseconds) { MacTools::Sleep(microseconds / 1000); }
