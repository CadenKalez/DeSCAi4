// boot_probe — a development tool, not part of the game: brings the recovered application object up headless against the
// real game data and reports how far it gets. Each step prints OK / FAIL; a function that is not recovered yet aborts the
// run with its name, which is the next thing to recover.
//   build/boot_probe [path-to-city.sc4]
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "gz/GZFramework.h"
#include "gz/cGZPersistResourceManager.h"
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
    gd::cIGZGDriver* GetGDriver() override { return nullptr; }
    int GetGDriverCount() override { return 0; }
    gd::cIGZGDriver* GetGDriverByIndex(int) override { return nullptr; }
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
void Step(const char* name, bool ok) { std::printf("[%s] %s\n", ok ? "OK" : "FAIL", name); std::fflush(stdout); }
}  // namespace

int main(int argc, char** argv) {
    const std::string bundle = std::string(std::getenv("HOME")) +
        "/Library/Application Support/Steam/steamapps/common/SimCity 4 Deluxe/Sim City 4 Deluxe Edition.app/Contents";
    const std::string docs = "/tmp/sc4boot_docs";
    std::filesystem::create_directories(docs);
    SetDocumentsFolderProvider([&](std::string& out) { out = docs; return true; });

    auto* framework = new cGZFramework();
    cGZFramework::sSetFrameWork(framework);
    framework->COM().RealInit();
    auto* app = new cSC4App("SimCity 4", framework->AppView());
    app->AddRef();
    app->SetFramework(framework);
    auto* languages = new cRZLanguageManager();
    framework->AddSystemService(languages);
    auto* resources = new cGZPersistResourceManager();
    framework->AddSystemService(resources);

    Graphics graphics;
    Options options;
    app->StartupHooks().graphicSystem = [&] { return &graphics; };
    app->StartupHooks().createVideoOptionStore = [&] { return &options; };
    app->StartupHooks().messageBox = [](const std::string& text, const std::string&) { std::printf("  alert: %s\n", text.c_str()); };
    app->StartupHooks().initializeManager = [](const char* name, cIGZUnknown*) { std::printf("  init %s\n", name); };
    app->StartupHooks().fileSystemDirectories = [&](sc4::FileSystemDirectories& d) {
        d.appDirectory = bundle + "/MacOS/"; d.cdAppDirectory = bundle + "/MacOS/"; d.dataDirectory = bundle + "/GameData/"; d.cdDataDirectory = bundle + "/GameData/";
        return true;
    };
    app->StartupHooks().bundleGameDataDirectory = [&] { return bundle + "/GameData"; };
    for (const char* name : {"cSC4GlyphTextureManager", "cSCStringDetokenizer", "cSLWinLocationSaver", "cSC4FeatureManager", "cSC4ToolTipManager", "cSC4MissingPluginDataReporter", "cSC4Nation"})
        app->SetServiceFactory(name, []() -> cIGZUnknown* { return new Object(); });
    cRZCmdLine line("sc4");
    framework->SetCommandLine(line);

    Step("PreFrameWorkInit", app->PreFrameWorkInit());
    Step("Init", app->Init());
    Step("PostFrameWorkInit", app->PostFrameWorkInit());
    std::printf("  resource segments: %u\n", static_cast<unsigned>(resources->GetSegmentCount()));
    if (argc > 1) std::printf("  city file: %s (LoadCity needs the city hooks, see AppCityHooks)\n", argv[1]);
    Step("PreFrameWorkShutdown", app->PreFrameWorkShutdown());
    Step("Shutdown", app->Shutdown());
    return 0;
}
