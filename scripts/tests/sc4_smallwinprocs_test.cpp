#include <cstdio>

#include "sc4/SmallWinProcs.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace sc4;

int main() {
    std::uint32_t last = 0;
    bool view = true;
    PanelServices& s = GetPanelServices();
    s.hasView3D = [&] { return view; };
    s.executeCommand = [&](std::uint32_t c) { last = c; };
    cSC4PanelUIWinProc panel;
    cGZMessage m; m.type = kGZMsgCommand; m.param1 = 0x287259f6; m.param2 = 0x26c10a3e;
    CHECK(panel.DoWinProcMessage(nullptr, m) && last == 0x6a935e3c);
    m.param2 = 0xc98f4f48;
    CHECK(panel.DoWinProcMessage(nullptr, m) && last == 0x6a935e39);
    last = 0; m.param2 = 0x1;
    CHECK(!panel.DoWinProcMessage(nullptr, m) && last == 0);
    view = false; m.param2 = 0x8a1da655;
    CHECK(!panel.DoWinProcMessage(nullptr, m) && last == 0);        // no 3D view: nothing happens
    m.param1 = 0x77; view = true;
    CHECK(!panel.DoWinProcMessage(nullptr, m));
    void* iface = nullptr;
    CHECK(panel.QueryInterface(0x22e85d8e, &iface));
    cSC4RegionSignpostAnimatorWinProc signpost;
    cGZMessage tick; tick.type = kMessageSignpostTick;
    CHECK(!signpost.DoWinProcMessage(nullptr, tick));              // no child: nothing to move
    if (failures == 0) std::puts("sc4_smallwinprocs: all passed");
    return failures ? 1 : 0;
}
