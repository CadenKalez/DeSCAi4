// Platform glue of the native Metal application (no recovered counterpart on macOS); derived from the early host.
// The boot_probe sequence (src/tools/boot_probe.cpp) run inside the host: the recovered cSC4App spine against the real
// game data, with the host's window manager and graphics behind the application's hooks.
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "sc4/DBSegment.h"
#include "platform/GameDataPath.h"
#include "app/NativeBoot.h"
#include "gz/GZFramework.h"
#include "gz/cGZPersistResourceManager.h"
#include "sc4/ServiceLocator.h"
#include "rz/LanguageManager.h"
#include "rz/cRZCmdLine.h"
#include "sc4/App.h"
#include "sc4/AppPlatform.h"

namespace {
struct Object final : cIGZUnknown {
    bool QueryInterface(std::uint32_t, void** out) override { *out = this; return true; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    int refs = 0;
};
struct Graphics final : IGraphicSystemControl {
    gd::cIGZGDriver* driver = nullptr;   // the native Metal driver the application window made
    DisplayMetrics GetScreenMetrics() override { return {1440, 900, 32}; }
    void PreInitSetDesiredGameResolution(const DisplayMetrics&) override {}
    void PreInitSetWindowedMode(bool) override {}
    void DisableScreenBuffer() override {}
    void EnableCursorEmulation(bool) override {}
    void EnablePageFlipping(bool) override {}
    bool IsPageFlippingEnabled() override { return false; }
    void SetDefaultDriverClassID(std::uint32_t) override {}
    bool CanvasReady() override { return true; }
    DisplayMetrics GetGameMetrics() override { return {1024, 768, 32}; }
    gd::cIGZGDriver* GetGDriver() override { return driver; }
    int GetGDriverCount() override { return driver ? 1 : 0; }
    gd::cIGZGDriver* GetGDriverByIndex(int) override { return driver; }
    void SetGDriver(gd::cIGZGDriver*) override {}
    void SetForceNoTranslucency(bool, float) override {}
    cIGZBuffer* CreateBuffer() override { return nullptr; }
    void RenderBufferToScreen(cIGZBuffer*, const cRZRect&, const cRZRect&) override {}
    void LazyFlush() override {}
    cRZRect CanvasRect() override { return cRZRect{0, 0, 640, 480}; }
    bool IsFullScreenMode() override { return false; }
    std::uint32_t GetScreenPixelFormatBits() override { return 32; }
};
struct Options final : sc4::IVideoOptionStore {
    void SetOption(int, int) override {}
    int GetOption(int option) override { return option == 0xd ? 1 : 0; }
};
}  // namespace


namespace native {
static cGZFramework* g_framework = nullptr;
static cSC4App* g_app = nullptr;
cGZFramework* BootFramework() { return g_framework; }
cSC4App* BootApplication() { return g_app; }

bool RunBoot(bool verbose, bool keepAlive, gd::cIGZGDriver* driver, const std::function<void(cGZFramework&, cSC4App&)>& beforeInit) {
    int failures = 0;
    auto Step = [&](const char* name, bool ok) { std::printf("[%s] %s\n", ok ? "OK" : "FAIL", name); std::fflush(stdout); if (!ok) ++failures; };

    const std::string bundle = platform::GameContentsDir();
    const std::string docs = "/tmp/sc4boot_docs";
    std::filesystem::create_directories(docs);
    SetDocumentsFolderProvider([&](std::string& out) { out = docs; return true; });

    auto* framework = new cGZFramework();
    g_framework = framework;
    cGZFramework::sSetFrameWork(framework);
    framework->COM().RealInit();
    auto* app = new cSC4App("SimCity 4", framework->AppView());
    g_app = app;
    app->AddRef();
    app->SetFramework(framework);
    auto* languages = new cRZLanguageManager();
    framework->AddSystemService(languages);
    auto* resources = new cGZPersistResourceManager();
    framework->AddSystemService(resources);
    SL::spRM = resources;   // the service locator's own lookup (ServiceLocator.cpp) does not find the manager under its id yet
    sc4::SetPersistResourceManagerService(resources);   // the occupants' exemplar holders resolve their exemplars through it

    // static: the start-up hooks outlive this function when the application is kept alive
    static Graphics graphics;
    static Options options;
    graphics.driver = driver;
    app->StartupHooks().graphicSystem = [&] { return &graphics; };
    app->StartupHooks().createVideoOptionStore = [&] { return &options; };
    app->StartupHooks().messageBox = [](const std::string& text, const std::string&) { std::printf("  alert: %s\n", text.c_str()); };
    app->StartupHooks().initializeManager = [](const char* name, cIGZUnknown*) { std::printf("  init %s\n", name); };
    app->StartupHooks().fileSystemDirectories = [&](sc4::FileSystemDirectories& d) {
        d.appDirectory = bundle + "/MacOS/"; d.cdAppDirectory = bundle + "/MacOS/"; d.dataDirectory = bundle + "/GameData/"; d.cdDataDirectory = bundle + "/GameData/";
        return true;
    };
    app->StartupHooks().bundleGameDataDirectory = [&] { return bundle + "/GameData"; };
    for (const char* name : {"cSC4GlyphTextureManager", "cSCStringDetokenizer", "cSLWinLocationSaver", "cSC4FeatureManager", "cSC4ToolTipManager", "cSC4MissingPluginDataReporter"})   // the nation is created by the application (NativeRegion)
        app->SetServiceFactory(name, []() -> cIGZUnknown* { return new Object(); });
    cRZCmdLine line("sc4");
    framework->SetCommandLine(line);

    if (beforeInit) beforeInit(*framework, *app);
    Step("PreFrameWorkInit", app->PreFrameWorkInit());
    Step("Init", app->Init());
    Step("PostFrameWorkInit", app->PostFrameWorkInit());
    std::printf("  resource segments: %u\n", static_cast<unsigned>(resources->GetSegmentCount()));
    if (!keepAlive) {   // the host keeps the application alive when it goes on to build UI from the game data
        Step("PreFrameWorkShutdown", app->PreFrameWorkShutdown());
        Step("Shutdown", app->Shutdown());
    }
    return failures == 0;
}
}  // namespace native
