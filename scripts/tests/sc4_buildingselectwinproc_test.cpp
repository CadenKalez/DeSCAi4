#include <cstdio>

#include "sc4/BuildingSelectWinProc.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace sc4;

namespace {
struct Styles final : ITractStyles {
    std::vector<std::uint32_t> active{0x2000}; bool all = false; int years = 5;
    std::vector<std::uint32_t> GetActiveStyles() override { return active; }
    void SetActiveStyles(const std::vector<std::uint32_t>& s) override { active = s; }
    bool IsUsingAllStylesAtOnce() override { return all; }
    void UseAllStylesAtOnce(bool a) override { all = a; }
    int GetYearsBetweenStyleChanges() override { return years; }
    void SetYearsBetweenStyleChanges(int y) override { years = y; }
};
}  // namespace

int main() {
    Styles styles;
    SetBuildingSelectEnvironment(&styles, nullptr);
    cSC4BuildingSelectWinProc proc(nullptr);               // style bookkeeping needs no window
    proc.AddActiveStyle(0x2001);
    proc.AddActiveStyle(0x2001);                           // already there
    CHECK(styles.active.size() == 2 && proc.IsStyleActive(0x2001) && !proc.IsStyleActive(0x2002));
    proc.RemoveActiveStyle(0x2000);
    CHECK(styles.active.size() == 1 && styles.active[0] == 0x2001);
    proc.RemoveActiveStyle(0x2003);                        // absent: no change
    CHECK(styles.active.size() == 1);
    void* iface = nullptr;
    CHECK(!proc.QueryInterface(1, &iface));                // answers only the window procedure interface
    if (failures == 0) std::puts("sc4_buildingselectwinproc: all passed");
    return failures ? 1 : 0;
}
