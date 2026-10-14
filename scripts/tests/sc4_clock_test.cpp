#include <cstdio>
#include <vector>

#include "rz/PortMemoryStreams.h"
#include "sc4/cSC424HourClock.h"

using namespace sc4;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

struct Host final : IClockHost {
    struct Msg { std::uint32_t id, a, b; };
    std::vector<Msg> posted;
    bool tunables = false;
    void PostMessage(std::uint32_t m, std::uint32_t a, std::uint32_t b) override { posted.push_back({m, a, b}); }
    bool ReadTunables(Tunables& t) override {
        if (!tunables) return false;
        t.timeMagnification = 3000.0f;   // above the max: clamped
        t.nightStartHour = 21;
        t.nightEndHour = 5;
        return true;
    }
    int count(std::uint32_t id) const { int n = 0; for (auto& m : posted) n += m.id == id; return n; }
};

int main() {
    Host host;
    cSC424HourClock clock(&host);
    std::uint32_t elapsed = 0;
    clock.SetElapsedSource([&] { return elapsed; });

    // defaults: noon, 100x, night 20:00-04:00
    CHECK(clock.GetHour() == 12 && clock.GetMinute() == 0 && clock.GetTimePercentage() == 0.5f);
    float mag = 0; clock.GetTimeMagnification(mag);
    CHECK(mag == 100.0f && !clock.IsNight());
    CHECK(clock.GetEventHour(0) == 20 && clock.GetEventHour(1) == 4 && clock.GetEventHour(7) == 0);

    // setters wrap and notify
    clock.SetHour(25);                          // 25 % 24 = 1
    CHECK(clock.GetHour() == 1 && host.count(kMsgHourChanged) == 1 && host.count(kMsgMinuteChanged) == 1);
    clock.SetHour(1);                           // unchanged: silent
    CHECK(host.posted.size() == 2);
    clock.SetMinute(125);                       // 125 % 60 = 5
    clock.SetSeconds(61);                       // 61 % 60 = 1
    CHECK(clock.GetMinute() == 5 && clock.GetSeconds() == 1);
    clock.SetTimePercentage(0.75f);
    CHECK(clock.GetHour() == 18);
    clock.SetTimePercentage(1.25f);             // wraps by a day
    CHECK(clock.GetHour() == 6);

    // idle advance: 1 real second at 100x = 100 game seconds
    host.posted.clear();
    clock.SetHour(5); clock.SetMinute(0); clock.SetSeconds(0);
    host.posted.clear();
    elapsed = 1000000;
    CHECK(clock.OnIdle(0));
    CHECK(clock.GetMinute() == 1 && clock.GetSeconds() == 40);
    CHECK(host.count(kMsgMinuteChanged) == 0);   // still inside the same 5-minute slot
    elapsed = 10000000;                          // 1000 game seconds: crosses slots
    clock.OnIdle(0);
    CHECK(host.count(kMsgMinuteChanged) == 1 && clock.GetMinute() == 18);
    elapsed = 100000000;                         // would be 10000s: one step is clamped to 1200
    std::uint32_t before = clock.GetHour() * 3600 + clock.GetMinute() * 60 + clock.GetSeconds();
    clock.OnIdle(0);
    std::uint32_t after = clock.GetHour() * 3600 + clock.GetMinute() * 60 + clock.GetSeconds();
    CHECK(after - before == 1200);
    elapsed = 100000;                            // below 61 game seconds: ignored
    std::uint32_t t0 = clock.GetSeconds();
    clock.OnIdle(0);
    CHECK(clock.GetSeconds() == t0);

    // pausing stops the clock and notifies on the 0->1 and 1->0 edges only
    host.posted.clear();
    clock.Pause(true); clock.Pause(true);
    CHECK(clock.IsPaused() && host.count(kMsgPauseChanged) == 1);
    elapsed = 50000000;
    std::uint32_t pausedAt = clock.GetMinute();
    clock.OnIdle(0);
    CHECK(clock.GetMinute() == pausedAt);
    clock.Pause(false);
    CHECK(clock.IsPaused() && host.count(kMsgPauseChanged) == 1);
    clock.Pause(false);
    CHECK(!clock.IsPaused() && host.count(kMsgPauseChanged) == 2);
    clock.Pause(false);                          // underflow is ignored
    CHECK(!clock.IsPaused());
    clock.HiddenPause(true);
    CHECK(clock.IsHiddenPaused() && host.count(kMsgHiddenPauseChanged) == 1);
    clock.HiddenPause(false);

    // night: 20:00-04:00 (visual hour), day/night notification on transitions
    clock.SetHour(19);
    clock.SetVisualTimeMode(cSC424HourClock::kVisualReal);
    host.posted.clear();
    elapsed = 0;
    for (int i = 0; i < 4; ++i) { elapsed = 2400000; clock.OnIdle(0); }  // 4 steps of 240 s (100x) = 16 min? check hours below
    clock.SetHour(20);
    elapsed = 100000000; clock.OnIdle(0);       // visual follows real time
    CHECK(clock.IsNight() == (clock.GetVisualHour() >= 20 || clock.GetVisualHour() < 4));
    clock.SetVisualTimeMode(cSC424HourClock::kVisualMidnight);
    CHECK(clock.GetVisualHour() == 0 && clock.IsNight());
    clock.SetVisualTimeMode(cSC424HourClock::kVisualNoon);
    CHECK(clock.GetVisualHour() == 12 && !clock.IsNight());
    CHECK(host.count(kMsgDayNightChanged) >= 1);

    // magnification clamps to its limits
    clock.SetTimeMagnification(10000.0f);
    clock.GetTimeMagnification(mag);
    CHECK(mag == 2000.0f);
    clock.SetTimeMagnification(0.0f);
    clock.GetTimeMagnification(mag);
    CHECK(mag == 0.05f);
    clock.SetTimeMagnificationMinMax(10.0f, 5.0f);   // max is raised to min
    float lo, hi; clock.GetTimeMagnificationMinMax(lo, hi);
    CHECK(lo == 10.0f && hi == 10.0f);
    clock.SetTimeMagnificationMinMax(0.05f, 2000.0f);

    // cheats
    clock.SetTimeMagnification(100.0f);
    clock.DoCheat(kClockCheatSetTime, "settime 9");
    CHECK(clock.GetHour() == 9);
    clock.DoCheat(kClockCheatControl, "clock PAUSE");
    CHECK(clock.IsPaused());
    clock.DoCheat(kClockCheatControl, "clock resume");
    CHECK(!clock.IsPaused());
    clock.DoCheat(kClockCheatControl, "clock magnify 250");
    clock.GetTimeMagnification(mag);
    CHECK(mag == 250.0f);

    // tunables and serialization
    host.tunables = true;
    cSC424HourClock c2(&host);
    c2.Init();
    c2.GetTimeMagnification(mag);
    CHECK(mag == 2000.0f && c2.GetEventHour(0) == 21 && c2.GetEventHour(1) == 5);
    c2.SetHour(7);
    c2.Pause(true);
    PortMemoryOStream out;
    CHECK(c2.Write(out));
    cSC424HourClock c3(&host);
    PortMemoryIStream in(out.bytes().data(), out.bytes().size());
    CHECK(c3.Read(in));
    CHECK(c3.GetHour() == 7 && c3.IsPaused() && c3.GetEventHour(0) == 21 && c3.GetGZCLSID() == 0xa7e7f929);

    if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::puts("sc4_clock ok");
    return 0;
}
