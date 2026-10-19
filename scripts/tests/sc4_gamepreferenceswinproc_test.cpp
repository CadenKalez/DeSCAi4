#include <cstdio>

#include "sc4/GamePreferencesWinProc.h"
#include "sc4/ISC4App.h"
#include "sc4/ServiceLocator.h"
namespace SL { std::uint8_t* spPreferences = nullptr; cISC4App* spSC4 = nullptr; }

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace sc4;

int main() {
    auto* proc = new cSC4GamePreferencesWinProc;
    proc->AddRef();
    void* iface = nullptr;
    CHECK(proc->QueryInterface(0x22e85d8e, &iface) && iface == proc);
    CHECK(!proc->QueryInterface(2, &iface));
    CHECK(!cSC4GamePreferencesWinProc::Displayed());
    proc->EndDialog();                       // nothing displayed: nothing to end
    CHECK(proc->Shutdown());
    proc->Release();
    proc->Release();
    if (failures == 0) std::puts("sc4_gamepreferenceswinproc: all passed");
    return failures ? 1 : 0;
}
