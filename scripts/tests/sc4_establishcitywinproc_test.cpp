#include <cstdio>

#include "sc4/EstablishCityWinProc.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace sc4;

int main() {
    auto* proc = new cSC4EstablishCityWinProc(nullptr);
    proc->AddRef();
    void* iface = nullptr;
    CHECK(proc->QueryInterface(0x22e85d8e, &iface) && !proc->QueryInterface(2, &iface));
    CHECK(proc->Difficulty() == 0);
    proc->EndDialog(false);                // no dialog: nothing to end
    proc->Release();
    proc->Release();
    if (failures == 0) std::puts("sc4_establishcitywinproc: all passed");
    return failures ? 1 : 0;
}
