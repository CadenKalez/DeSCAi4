// Scope helpers: game pause pairing, input flush timing, percentage constructor, streams that cannot open.
#include <cstdio>

#include "sc4/AutoHelpers.h"
#include "sc4/SCPropertyHelp.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace sc4;

int main() {
    int graphics = 0, animation = 0, simulator = 0, view = 0, flushes = 0;
    bool graphicsPaused = false, simPaused = false;
    GamePauseServices& s = GetGamePauseServices();
    s.graphics = [&](bool p) { ++graphics; graphicsPaused = p; };
    s.animation = [&](bool) { ++animation; };
    s.simulator = [&](bool p) { ++simulator; simPaused = p; };
    s.view = [&](bool) { ++view; };
    s.flushInput = [&] { ++flushes; };
    {
        cSC4AutoGamePause pause(true, false, true, false);
        CHECK(graphics == 1 && graphicsPaused && animation == 0 && simulator == 1 && simPaused && view == 0);
    }
    CHECK(graphics == 2 && !graphicsPaused && simulator == 2 && !simPaused);       // released with the same flags

    {
        cSC4AutoFlushInputMessageQueue flush(0);        // an interval of 0 flushes on every call
        flush.Flush();
        CHECK(flushes == 1);
    }
    CHECK(flushes == 2);                                // and once more when it leaves scope
    {
        cSC4AutoFlushInputMessageQueue slow(3600000);
        slow.Flush();
    }
    CHECK(flushes == 2);

    CHECK(SC4Percentage(50.0f, false).value == 0.5f && SC4Percentage(0.25f, true).value == 0.25f && SC4Percentage().value == 0.0f);

    cSC4AutoLoadStream load(nullptr, 1, 2, 3);
    CHECK(!load.IsOpen() && load.GetVersion() == 0xffff);
    cSC4AutoSaveStream save(nullptr, 1, 2, 3, 5);
    CHECK(!save.IsOpen());
    cSC4AutoDisableAppSwitching noSwitch;
    (void)noSwitch;
    if (failures == 0) std::puts("sc4_autohelpers: all passed");
    return failures ? 1 : 0;
}
