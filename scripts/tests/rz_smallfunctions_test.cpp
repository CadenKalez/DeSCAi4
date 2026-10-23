// Small RZ / platform entry points that no other test calls: the string-to-uint conversion, the critical section's
// native handle and destruction, the thread sleep, the CPU-usage setter and the framework accessor.
#include <chrono>
#include <cstdio>
#include <mutex>

#include "gz/GZCOMDirector.h"
#include "rz/RZConvert.h"
#include "rz/Threading.h"
#include "rz/cRZCriticalSection.h"
#include "sc4/AppPlatform.h"

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

int main() {
    // RZConvertToUintPtrA: decimal, "0x" hex, and hex selected by an A-F digit before the first non-digit
    CHECK(RZ::ConvertToUint("123") == 123u);
    CHECK(RZ::ConvertToUint("0x1F") == 0x1fu && RZ::ConvertToUint("0X10") == 16u);
    CHECK(RZ::ConvertToUint("1f") == 0x1fu && RZ::ConvertToUint("42ab") == 0x42abu);
    CHECK(RZ::ConvertToUint("") == 0u);

    // cRZCriticalSection: recursive locking through the interface and the native handle, destroyed via delete
    auto* cs = new cRZCriticalSection();
    cs->Lock();
    cs->Lock();
    CHECK(cs->IsLocked() == 2);
    {
        std::lock_guard<std::recursive_mutex> guard(cs->NativeHandle());   // GetCriticalSectionHandle: same mutex, recursive
    }
    CHECK(cs->Unlock() >= 0 && cs->Unlock() >= 0 && cs->IsLocked() == 0);
    delete cs;

    // cRZThread::SleepCurrentThread takes microseconds
    const auto t0 = std::chrono::steady_clock::now();
    cRZThread::SleepCurrentThread(5000);
    CHECK(std::chrono::steady_clock::now() - t0 >= std::chrono::milliseconds(4));

    CHECK(SC4SetCPUUsageCount(4) == 0);           // the macOS build ignores the CPU count
    CHECK(RZGetFrameWork() == nullptr);           // no framework has been registered in this process

    std::printf(g_fail ? "FAILED %d\n" : "rz_smallfunctions OK\n", g_fail);
    return g_fail ? 1 : 0;
}
