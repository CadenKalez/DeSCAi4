#include <cstdio>

#include "rz/COMDirectors.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace gz;

namespace {
struct Product final : cIGZUnknown {
    std::uint32_t refs = 0;
    bool QueryInterface(std::uint32_t iid, void** out) override { if (iid != 77) return false; *out = this; AddRef(); return true; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { if (--refs == 0) { delete this; return 0; } return refs; }
};
struct Service final : cIGZUnknown {
    static int released;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return 1; }
    std::uint32_t Release() override { ++released; return 0; }
};
int Service::released = 0;
struct Com final : cIGZCOM {
    Service service; std::vector<std::uint32_t> asked;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return 1; }
    std::uint32_t Release() override { return 1; }
    bool CreateInstance(std::uint32_t clsid, std::uint32_t, void** out) override { asked.push_back(clsid); *out = clid_ok(clsid) ? &service : nullptr; return true; }
    static bool clid_ok(std::uint32_t c) { return c != 0xc2c2eb0f; }
    cIGZFrameWork* GetFrameWork() override { return nullptr; }
};
struct Framework final : IDirectorFramework {
    int services = 0, hooks = 0;
    void AddSystemService(cIGZUnknown*) override { ++services; }
    void AddHooks(cIGZFrameWorkHooks*) override { ++hooks; }
    void RegisterSystemService(cIGZUnknown*) override {}
    void UnregisterSystemService(cIGZUnknown*) override {}
};
int count = 0;
void Count(std::uint32_t, std::uint32_t, void*) { ++count; }
cIGZUnknown* MakeFlat() { static Service s; return &s; }
}  // namespace

int main() {
    cGZWin32COMDirector win32;
    CHECK(win32.GetDirectorID() == 0x41740ba);
    count = 0; win32.EnumClassObjects(&Count, nullptr);
    CHECK(count == 1);
    cGZScriptCOMDirector script;
    CHECK(script.GetDirectorID() == 0xfa2e78c2);
    void* out = nullptr;
    CHECK(!script.GetClassObject(0xba2e7953, 1, &out));           // class not linked: nothing created
    RegisterDirectorMaker("cGZScriptTerp", &MakeFlat);
    CHECK(!script.GetClassObject(0xba2e7953, 1, &out));           // the product answers no interface
    cGZWinCOMDirector win;
    CHECK(win.GetDirectorID() == 0xa41740d1);
    count = 0; win.EnumClassObjects(&Count, nullptr);
    CHECK(count > 25);                                             // its own table plus the window-manager director's
    Com com; Framework fw;
    SetDirectorFramework(&fw);
    CHECK(win.OnStart(&com) && fw.hooks == 1 && fw.services == 1 && com.asked.size() == 2 && com.asked[0] == 0x84174448);
    cGZToolsCOMDirector tools;
    CHECK(tools.GetDirectorID() == 0x5c54909c && tools.OnStart(&com) && fw.hooks == 2 && com.asked.back() == 0x414a2b1);
    CHECK(cGZUIScriptCOMDirector().GetDirectorID() == 0x9a3559b0 && cGXCOMDirector().GetDirectorID() == 0x43cac0b9);
    CHECK(cGZResourceCOMDirectorW32().GetDirectorID() == 0xa431a334 && cGZGraphicWin32COMDirector().GetDirectorID() == 0x446b928f);
    CHECK(cFontCOMDirector().GetDirectorID() == 0x5aa40bd3 && cGZSoundCOMDirector().GetDirectorID() == 0xe9d3a40b);
    count = 0; cSC4AutomataCOMDirector().EnumClassObjects(&Count, nullptr);
    CHECK(count >= 20);
    count = 0; cSC4ResourceLibCOMDirector().EnumClassObjects(&Count, nullptr);
    CHECK(count >= 20);
    // accessors hand out one process-wide instance
    CHECK(GetGZWinCOMDllDirector() == GetGZWinCOMDllDirector() && GetGZWinCOMDllDirector()->GetDirectorID() == 0xa41740d1);
    CHECK(GetGZWin32COMDllDirector()->GetDirectorID() == 0x41740ba && GetFontComDirector()->GetDirectorID() == 0x5aa40bd3);
    CHECK(GetSCLuaCOMDirector()->GetDirectorID() == 0xca107f8b && GetGZDrawCOMDllDirector()->GetDirectorID() == 0xb2ea939);
    CHECK(GetGZGraphicCOMDllDirector()->GetDirectorID() == 0x246b917a && GetSC4GDriversCOMDirector()->GetDirectorID() == 0x64b2a903);
    CHECK(GetGZWWWCOMDllDirector()->GetDirectorID() == 0x649b0fbe && GetGZWWWWin32COMDllDirector()->GetDirectorID() == 0xa49b0fd1);
    // the main game director: its own class table, and its children joined the process-wide director
    {
        std::size_t before = 0;
        count = 0; RZGetCOMDllDirector()->EnumClassObjects(&Count, nullptr); before = static_cast<std::size_t>(count);
        cSC4COMDirector sc4;
        CHECK(sc4.GetDirectorID() == 0x63cc0af8);
        count = 0; sc4.EnumClassObjects(&Count, nullptr);
        CHECK(count > 150);
        count = 0; RZGetCOMDllDirector()->EnumClassObjects(&Count, nullptr);
        CHECK(static_cast<std::size_t>(count) > before);              // quirk: the children were added to the process director, not to sc4
        CHECK(SC4GetCOMDirector()->GetDirectorID() == 0x63cc0af8);
    }
    // Lua factories hand out interfaces of whatever the registered maker builds
    RegisterDirectorMaker("cSCLua", +[]() -> cIGZUnknown* { return new Product(); });
    void* lua = nullptr;
    CHECK(GetSCLuaCOMDirector()->GetClassObject(0x4b2811aa, 77, &lua) && lua != nullptr);
    static_cast<cIGZUnknown*>(lua)->Release();
    std::printf(failures ? "FAILED\n" : "rz_comdirectors ok\n");
    return failures != 0;
}
