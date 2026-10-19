// RZ:: service accessors: lookup by (service id, interface id), caching, null without a framework.
#include <cstdio>
#include <cstring>

#include "gz/GZFramework.h"
#include "gz/RZServices.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

namespace {
struct Files final : cRZSystemService {
    Files() : cRZSystemService(kGZSID_cGZFileSystem, 100) {}
    // answers every interface with a cIGZFileSystem
    struct View final : cIGZFileSystem {
        const char* AppDrive() override { return ""; }
        const char* AppDirectory() override { return "/app/"; }
        const char* AppName() override { return "sc4"; }
        const char* AppExtension() override { return ""; }
        const char* DataDirectory() override { return "/data/"; }
        const char* PlugInDirectory() override { return "/data/Plugins/"; }
        const char* CDAppDrive() override { return ""; }
        const char* CDAppDirectory() override { return "/cd/"; }
        const char* CDDataDirectory() override { return "/cd/data/"; }
        const char* CDPlugInDirectory() override { return ""; }
        bool FindCDDrive() override { return false; }
        void UseCDIniFile(bool) override {}
        void AllowRecursiveCDSearch(bool) override {}
        void SetExpectedCDName(const cIGZString&) override {}
        void SetExpectedCDAppPath(const cIGZString&) override {}
        void SetDataDirectory(const cIGZString&) override {}
        void SetPluginDirectory(const cIGZString&) override {}
        void GetPluginDirectoryName(cIGZString&) override {}
        void SetPluginDirectoryName(const cIGZString&) override {}
        bool QueryInterface(std::uint32_t, void**) override { return false; }
        std::uint32_t AddRef() override { return 1; }
        std::uint32_t Release() override { return 1; }
    } view;
    bool QueryInterface(std::uint32_t iid, void** out) override {
        if (iid == kGZIID_cIGZFileSystem) { *out = static_cast<cIGZFileSystem*>(&view); return true; }
        return cRZSystemService::QueryInterface(iid, out);
    }
    std::uint32_t AddRef() override { return cRZSystemService::AddRef(); }
    std::uint32_t Release() override { return cRZSystemService::Release(); }
};
}  // namespace

int main() {
    RZ::ResetCaches();
    CHECK(RZ::FS() == nullptr && RZ::WM() == nullptr && RZ::LM() == nullptr);          // no framework yet
    auto* f = new cGZFramework();
    cGZFramework::sSetFrameWork(f);
    CHECK(RZ::FS() == nullptr);                                                         // not registered
    auto* files = new Files();
    f->AddSystemService(files);
    cIGZFileSystem* fs = RZ::FS();
    CHECK(fs == static_cast<cIGZFileSystem*>(&files->view));
    CHECK(RZ::FS() == fs);
    cRZString dir;
    RZ::ADir(dir); CHECK(std::strcmp(dir.ToChar(), "/app/") == 0);
    RZ::DDir(dir); CHECK(std::strcmp(dir.ToChar(), "/data/") == 0);
    f->RemoveSystemService(files);
    CHECK(RZ::FS() == fs);                                                              // quirk: a removed service stays cached
    RZ::ResetCaches();
    CHECK(RZ::FS() == nullptr);
    std::printf(failures ? "FAILED\n" : "OK\n");
    return failures ? 1 : 0;
}
