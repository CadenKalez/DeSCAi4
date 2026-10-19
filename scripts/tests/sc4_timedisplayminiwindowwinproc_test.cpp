#include <cstdio>

#include "sc4/ISC4App.h"
#include "sc4/ServiceLocator.h"
#include "sc4/TimeDisplayMiniWindowWinProc.h"
namespace SL { std::uint8_t* spPreferences = nullptr; cISC4App* spSC4 = nullptr; }

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace sc4;

int main() {
    std::time_t fake = 86400 * 365;
    GetTimeDisplayServices().now = [&] { return fake; };
    auto* proc = new cSC4TimeDisplayMiniWindowWinProc;
    proc->AddRef();
    void* iface = nullptr;
    CHECK(proc->QueryInterface(0x22e85d8e, &iface));
    proc->Release();
    CHECK(!proc->QueryInterface(2, &iface));
    CHECK(proc->Init());
    const std::uint32_t before = proc->Seconds();
    CHECK(before % 86400 == 0);                           // the constructor stores whole days
    proc->UpdateTimeDisplay();
    const std::uint32_t after = proc->Seconds();
    CHECK(after != before || after % 86400 == 0);         // time of day added (or exactly midnight)
    fake += 10;
    proc->UpdateTimeDisplay();
    CHECK(proc->Seconds() == after + 10);
    CHECK(proc->Shutdown());
    proc->EndDialog();                                    // no dialog attached: nothing happens
    proc->Release();
    if (failures == 0) std::puts("sc4_timedisplayminiwindowwinproc: all passed");
    return failures ? 1 : 0;
}
