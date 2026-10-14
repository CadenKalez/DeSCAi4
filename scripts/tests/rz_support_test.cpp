#include "rz/cRZCallbackTimer.h"
#include "rz/cRZCriticalSection.h"
#include "rz/cRZRandom.h"

#include <SDL.h>

#include <cassert>
#include <cmath>
#include <cstdio>
#include <set>
#include <thread>

namespace {
int g_fired = 0;
void* g_firedWith = nullptr;
void Fire(void* p) { ++g_fired; g_firedWith = p; }
struct Counted final : cIGZUnknown {
    int refs = 0;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
};
}  // namespace

int main() {
    // --- cRZRandom: LCG matches the classic ANSI rand() stream (low 15 bits) for seed 1
    {
        cRZRandom r(1);
        const unsigned expect[] = {16838, 5758, 10113, 17515, 31051};
        for (unsigned e : expect) assert((r.RandomUint32Uniform() & 0x7fff) == e);
        cRZRandom a(1234), b(1234), c(99);
        for (int i = 0; i < 100; ++i) assert(a.RandomUint32Uniform(1000) == b.RandomUint32Uniform(1000));
        bool differs = false; for (int i = 0; i < 20; ++i) differs |= (a.RandomUint32Uniform(1u << 30) != c.RandomUint32Uniform(1u << 30));
        assert(differs);
        cRZRandom z(5); assert(z.RandomUint32Uniform(0u) == 0 && z.RandomSint32RangeUniform(7, 7) == 7);
        cRZRandom u(42);
        for (int i = 0; i < 5000; ++i) {
            assert(u.RandomUint32Uniform(10) < 10);
            const int v = u.RandomSint32RangeUniform(-5, 5); assert(v >= -5 && v < 5);
            const int g = u.RandomSint32RangeGaussianFast(0, 100); assert(g >= 0 && g < 100);
            const double d = u.RandomDoubleUniform(); assert(d >= 0.0 && d < 1.0);
            const double rg = u.RandomDoubleRangeGaussian(10.0, 20.0); assert(rg >= 10.0 && rg <= 20.0);
            const double fg = u.RandomDoubleRangeGaussianFast(1.0, 2.0); assert(fg >= 1.0 && fg <= 2.0);
        }
        cRZRandom gs(7); double sum = 0, sq = 0; const int n = 20000;
        for (int i = 0; i < n; ++i) { const double x = gs.RandomDoubleGaussian(); sum += x; sq += x * x; }
        assert(std::fabs(sum / n) < 0.05 && std::fabs(sq / n - 1.0) < 0.1);   // ~N(0,1)
        // every outcome of a small range is reachable
        std::set<unsigned> seen; cRZRandom s(3); for (int i = 0; i < 200; ++i) seen.insert(s.RandomUint32Uniform(6));
        assert(seen.size() == 6);
    }
    // --- cRZCriticalSection: recursion depth bookkeeping and cross-thread exclusion
    {
        auto* cs = new cRZCriticalSection;
        assert(cs->IsValid() && cs->IsLocked() == 0);
        cs->Lock(); cs->Lock();
        assert(cs->IsLocked() == 2 && cs->TryLock() == 3);
        bool otherGotIt = true;
        std::thread t([&] { otherGotIt = cs->NativeHandle().try_lock(); if (otherGotIt) cs->NativeHandle().unlock(); });
        t.join();
        assert(!otherGotIt);
        assert(cs->Unlock() == 2 && cs->Unlock() == 1 && cs->Unlock() == 0);
        cs->Release();
    }
    // --- cRZCallbackTimer (against the SDL stub)
    {
        cRZCallbackTimer t;
        assert(!t.StartPeriodic(0, nullptr));                     // no callback yet
        t.SetCallbackFunction(Fire, &g_fired);
        std::uint32_t v = 0; t.GetPeriod(v); assert(v == 1000); t.GetResolution(v); assert(v == 100);
        Counted holder;
        assert(t.StartPeriodic(250, &holder) && t.IsRunning() && g_timerInterval == 250 && holder.refs == 1);
        assert(!t.StartOneShot(1, nullptr));                      // already running
        // periodic: the SDL callback returns the interval and the timer keeps running
        assert(g_timerCallback(g_timerInterval, g_timerParam) == 250 && g_fired == 1 && g_firedWith == &g_fired);
        assert(t.IsRunning() && holder.refs == 1);
        // changing the period re-arms with the new value, holder preserved
        t.SetPeriod(400);
        assert(t.IsRunning() && g_timerInterval == 400);
        t.Stop(); assert(!t.IsRunning() && holder.refs == 0);
        // one-shot: callback returns 0 and the holder is released before the call
        assert(t.StartOneShot(50, &holder) && holder.refs == 1);
        assert(g_timerCallback(g_timerInterval, g_timerParam) == 0 && g_fired == 2);
        assert(!t.IsRunning() && holder.refs == 0);
        // creation failure leaves the timer stopped and balances refs
        g_timerCreationFails = true;
        assert(!t.StartPeriodic(10, &holder) && !t.IsRunning() && holder.refs == 0);
        g_timerCreationFails = false;
        assert(cRZCallbackTimer::GetCurrentSystemMillisecondTime() >= 0);
    }
    std::puts("rz support ok");
}
