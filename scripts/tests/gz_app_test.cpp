#include <cstdio>
#include <cstring>

#include "gz/GZApp.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

struct Framework final : IGZAppFramework {
    cIGZSystemService* added = nullptr;
    void AddSystemService(cIGZSystemService* s) override { added = s; }
};

int main() {
    Framework fw;
    auto* app = new cGZApp("TestModule", &fw);
    app->AddRef();
    CHECK(app->GetServiceID() == 0x66 && app->GetServicePriority() == -2000000);
    CHECK(std::strcmp(app->ModuleName(), "TestModule") == 0);
    CHECK(app->AsIGZSystemService() == static_cast<cIGZSystemService*>(app));
    CHECK(app->Shutdown() && app->PreFrameWorkInit() && app->PostFrameWorkInit() && app->PreFrameWorkShutdown() && app->LoadRegistry());
    CHECK(app->GZRun() == 0);
    app->AddDynamicLibrariesHere(); app->AddCOMDirectorsHere(); app->AddApplicationServicesHere();
    app->AddApplicationService(app);
    CHECK(fw.added == static_cast<cIGZSystemService*>(app) && app->FrameWork() == &fw);
    void* iface = nullptr;
    CHECK(app->QueryInterface(kGZIID_cIGZApp, &iface) && iface == static_cast<cIGZApp*>(app));
    CHECK(app->QueryInterface(kGZIID_cIGZSystemService, &iface) && iface == static_cast<cIGZSystemService*>(app));
    CHECK(!app->QueryInterface(0x1234, &iface));
    app->Release(); app->Release(); app->Release();
    std::string long_name(40, 'x');
    cGZApp other(long_name.c_str());
    CHECK(long_name == other.ModuleName());
    std::printf(failures ? "FAILED\n" : "gz_app ok\n");
    return failures ? 1 : 0;
}
