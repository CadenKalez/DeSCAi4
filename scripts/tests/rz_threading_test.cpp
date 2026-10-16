#include <cstdio>
#include <thread>

#include "rz/Threading.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

int main() {
    cRZString name("test");
    auto* mutex = new cRZMutex(name);
    CHECK(mutex->IsValid());
    CHECK(mutex->Lock());
    std::thread other([&] { CHECK(!mutex->TryLock()); });
    other.join();
    CHECK(mutex->Unlock());
    CHECK(mutex->TryLock() && mutex->Unlock());
    alignas(8) unsigned char handle[sizeof(pthread_mutex_t)];
    CHECK(mutex->GetMutexHandle(handle));
    CHECK(!mutex->Release());                       // destroys itself, reports false

    auto* signal = new cRZThreadSignal(1);
    CHECK(signal->IsValid() && signal->ManualReset() && signal->Unsignal());
    CHECK(signal->Signal() && signal->Broadcast());
    unsigned char event[sizeof(pthread_cond_t)];
    CHECK(signal->GetEventHandle(event));
    const int waited = signal->TimedWait(10);       // never locked mutex: reports "timed out" / error, not "signalled"
    CHECK(waited != 1);
    CHECK(!signal->Release());

    FileChangeNotification watcher;
    CHECK(watcher.Init() && watcher.Start() && !watcher.IsStarted() && watcher.Stop() && watcher.Shutdown());
    watcher.Directory().AssignFromCString("/tmp/watched");
    cRZString out;
    CHECK(watcher.GetWatchedDirectory(out) && std::string(out.ToChar()) == "/tmp/watched");
    std::printf(failures ? "FAILED\n" : "OK\n");
    return failures != 0;
}
