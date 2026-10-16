#include <cstdio>

#include "rz/cRZCOMDllDirector.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

namespace {
struct Product final : cIGZUnknown {
    static int alive;
    std::uint32_t refs = 0;
    Product() { ++alive; }
    ~Product() { --alive; }
    bool QueryInterface(std::uint32_t iid, void** out) override { if (iid != 77) return false; *out = this; AddRef(); return true; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { if (--refs == 0) { delete this; return 0; } return refs; }
};
int Product::alive = 0;
cIGZUnknown* MakeProduct() { return new Product(); }
bool MakeInterface(std::uint32_t iid, void** out) { *out = reinterpret_cast<void*>(std::uintptr_t(iid)); return true; }

struct Com final : cIGZCOM {
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return 1; }
    std::uint32_t Release() override { return 1; }
    bool CreateInstance(std::uint32_t, std::uint32_t, void**) override { return false; }
    cIGZFrameWork* GetFrameWork() override { return reinterpret_cast<cIGZFrameWork*>(0x1234); }
};
struct Dir final : cRZCOMDllDirector {
    std::uint32_t GetDirectorID() const override { return 5; }
};
void CountOne(std::uint32_t, std::uint32_t, void* n) { ++*static_cast<int*>(n); }
std::size_t Heap() { return 4096; }
}  // namespace

int main() {
    Com com; Dir parent, child;
    child.AddCls(100, &MakeProduct);
    child.AddCls(100, &MakeInterface);                 // second registration of an id is ignored
    child.AddCls(200, &MakeInterface);
    cRZString path("game.dll");
    CHECK(!parent.InitializeCOM(nullptr, path));
    CHECK(parent.InitializeCOM(&com, path) && parent.FrameWork() == reinterpret_cast<cIGZFrameWork*>(0x1234) && parent.GZCOM() == &com);
    parent.AddDirector(&child);
    CHECK(child.GZCOM() == &com);
    cRZString out;
    CHECK(child.GetLibraryPath(out) && std::string(out.ToChar()) == "game.dll");

    void* obj = nullptr;
    CHECK(parent.GetClassObject(100, 77, &obj) && obj != nullptr && Product::alive == 1);
    static_cast<cIGZUnknown*>(obj)->Release();
    CHECK(Product::alive == 0);
    CHECK(!parent.GetClassObject(100, 78, &obj) && Product::alive == 0);   // wrong interface: object destroyed again
    CHECK(parent.GetClassObject(200, 9, &obj) && obj == reinterpret_cast<void*>(9));
    CHECK(!parent.GetClassObject(300, 9, &obj));
    int n = 0;
    parent.EnumClassObjects(&CountOne, &n);
    CHECK(n == 2);

    // refcount rules: release never goes below zero; unload check skips children while referenced
    CHECK(parent.AddRef() == 1 && parent.CanUnloadNow());   // quirk: referenced directors answer true
    parent.RemoveRef();
    child.AddRef();
    CHECK(parent.CanUnloadNow());                            // a referenced child answers true as well
    child.RemoveRef();
    CHECK(parent.RemoveRef() == 0 && parent.RemoveRef() == 0);
    SetRZHeapSizeFn(&Heap);
    CHECK(parent.GetHeapAllocatedSize() == 4096);
    void* hooks = nullptr;
    CHECK(parent.QueryInterface(kGZIID_cIGZFrameWorkHooks, &hooks) && hooks == static_cast<cIGZFrameWorkHooks*>(&parent));
    std::printf(failures ? "FAILED\n" : "rz_comdirector ok\n");
    return failures != 0;
}
