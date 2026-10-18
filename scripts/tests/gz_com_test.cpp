#include <cstdio>
#include <set>

#include "gz/cGZCOM.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

struct Director final : cIGZCOMDirector {
    bool canUnload = true;
    int starts = 0;
    std::set<std::uint32_t> classes;
    cRZString path;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return 1; }
    std::uint32_t Release() override { return 1; }
    bool InitializeCOM(cIGZCOM*, const cIGZString& p) override { path.Copy(p); return true; }
    bool OnStart(cIGZCOM*) override { ++starts; return true; }
    void EnumClassObjects(void (*fn)(std::uint32_t, std::uint32_t, void*), void* context) override {
        for (std::uint32_t c : classes) fn(c, 3, context);
    }
    bool GetClassObject(std::uint32_t clsid, std::uint32_t iid, void** out) override {
        if (!classes.count(clsid)) return false;
        *out = reinterpret_cast<void*>(std::uintptr_t(clsid) * 16 + iid);
        return true;
    }
    bool CanUnloadNow() override { return canUnload; }
    bool OnUnload() override { return true; }
    std::uint32_t RefCount() override { return 1; }
    std::uint32_t RemoveRef() override { return 1; }
    cIGZFrameWork* FrameWork() override { return nullptr; }
    cIGZCOM* GZCOM() override { return nullptr; }
    std::uint32_t GetDirectorID() const override { return 0; }
    bool GetLibraryPath(cIGZString&) override { return true; }
    std::size_t GetHeapAllocatedSize() override { return 0; }
};
struct FrameworkObj final : cIGZUnknown {
    bool QueryInterface(std::uint32_t iid, void** out) override { if (iid != 77) return false; *out = this; return true; }
    std::uint32_t AddRef() override { return 1; }
    std::uint32_t Release() override { return 1; }
};
struct Host final : IGZCOMHost {
    FrameworkObj framework;
    Director processDirector;
    cIGZUnknown* Framework() override { return &framework; }
    cIGZCOMDirector* ProcessDirector() override { return &processDirector; }
    void CurrentAppPath(cIGZString& out) override { out.AssignFromCString("/game"); }
};

static int visited = 0;
static bool Count(cGZCOMLibrary*, void*) { return ++visited < 2; }

int main() {
    Host host;
    host.processDirector.classes = {500};
    cGZCOM com(&host);
    CHECK(com.GetServiceID() == 0xa3cd8db3 && com.GetServicePriority() == 10000000);
    CHECK(com.RealInit() && host.processDirector.path.Data() == std::string("/game") && com.RealInit());

    Director a, b;
    a.classes = {10, 11};
    b.classes = {11, 12};
    com.AddLoadedLibrary(cRZString("b.dll"), &b);
    com.AddLoadedLibrary(cRZString("a.dll"), &a);
    void* out = nullptr;
    CHECK(com.GetClassObject(10, 4, &out) && out == reinterpret_cast<void*>(10 * 16 + 4));
    CHECK(com.GetClassObject(12, 5, &out) && out == reinterpret_cast<void*>(12 * 16 + 5));
    CHECK(com.GetClassObject(11, 6, &out));                      // registered twice with equal versions: the first library keeps it
    CHECK(com.GetClassObject(500, 1, &out) && out == reinterpret_cast<void*>(500 * 16 + 1));   // unknown class: the process director
    CHECK(com.GetClassObject(1000, 77, &out) && out == &host.framework);   // class 1000 is the framework itself
    CHECK(!com.GetClassObject(999, 1, &out));
    CHECK(com.QueryInterface(0x4241330d, &out) && !com.QueryInterface(3, &out));

    // libraries enumerate in path order
    std::uint32_t count = 0;
    CHECK(com.EnumerateLibraries(nullptr, count) == 2 && count == 2);
    cGZCOMLibrary* libs[4];
    count = 4;
    CHECK(com.EnumerateLibraries(libs, count) == 2);
    cRZString first;
    libs[0]->GetPath(first);
    CHECK(first.Data() == std::string("a.dll"));
    visited = 0;
    com.EnumerateLibraries(Count, nullptr);
    CHECK(visited == 2);

    // adding a known library succeeds, an unknown one cannot be loaded on this platform
    CHECK(com.AddLibrary(cRZString("a.dll")) && !com.AddLibrary(cRZString("new.dll")));

    // unloading
    a.canUnload = false;
    com.FreeUnusedLibraries();
    CHECK(libs[0]->IsLoaded() && !libs[1]->IsLoaded());
    com.RealShutdown();
    CHECK(!libs[0]->IsLoaded());

    // GUIDs
    std::uint32_t guids[16];
    CHECK(com.CreateGuids(guids, 16));
    std::set<std::uint32_t> unique(guids, guids + 16);
    CHECK(unique.size() == 16 && (guids[7] >> 29) == 7 && (guids[8] >> 29) == 0);
    std::uint32_t one = 0;
    CHECK(com.CreateGuid(&one));
    CHECK(GetGuidGenSeconds() > 0);
    std::printf(failures ? "FAILED\n" : "OK\n");
    return failures != 0;
}
