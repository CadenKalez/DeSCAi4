#include "rz/cRZTimer.h"
#include <SDL.h>
#include <cassert>
#include <cstdio>
int main() {
    cRZTimer t(cRZTimer::kMilliseconds);
    assert(!t.IsTimerRunning());
    g_ticks = 1000; t.Start(); assert(t.IsTimerRunning());
    g_ticks = 4500;
    assert(t.GetElapsedTime() == 3500);
    t.SetUnits(cRZTimer::kSeconds); assert(!t.IsTimerRunning() && t.GetElapsedTime() == 0);
    g_ticks = 1000; t.Start(); g_ticks = 7000;
    assert(t.GetElapsedTime() == 6);
    float f = -1; t.GetElapsedTime(f); assert(f == 6.0f);
    std::uint64_t u = 0; t.GetElapsedTime(u); assert(u == 6);
    t.Stop(); assert(!t.IsTimerRunning()); g_ticks = 99999; assert(t.GetElapsedTime() == 6);
    t.SetElapsedTime(120.0f); assert(t.GetElapsedTime() == 120);          // seconds
    cRZTimer m(cRZTimer::kMinutes); m.SetElapsedTime(std::uint64_t(2)); assert(m.GetElapsedTime() == 2);
    cRZTimer us(cRZTimer::kMicroseconds); us.SetElapsedTime(std::uint64_t(5000)); assert(us.GetElapsedTime() == 5000);
    cRZTimer ns(cRZTimer::kNanoseconds); ns.SetElapsedTime(std::uint64_t(3'000'000)); assert(ns.GetElapsedTime() == 0 /*unsupported on read*/);
    // tick wrap-around no longer explodes
    cRZTimer w(cRZTimer::kMilliseconds); g_ticks = 0xFFFFFFF0u; w.Start(); g_ticks = 0x10; assert(w.GetElapsedTime() == 0x20);
    std::puts("cRZTimer ok");
}
