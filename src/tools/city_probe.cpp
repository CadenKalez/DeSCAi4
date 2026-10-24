// city_probe - a development tool, not part of the game: boots the recovered application object against the real game
// data, then loads a real city file through cSC4App::LoadCity and reports whether the city came up.
//
// Every step prints OK / FAIL. Unlike boot_probe this one supplies the city hooks of AppCityHooks, so the load path is
// the game's own code the whole way: the segment is a recovered cSC4DBSegment, the version record and the application
// record are read with the recovered SaveGame helpers, and the city object is a recovered cSC4City.
//
//   build/city_probe <city.sc4> [--verbose]
// Without an argument it loads the first city of the retail install's Timbuktu region.
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <string>

#include "gz/GZFramework.h"
#include "gz/cGZPersistResourceManager.h"
#include "rz/LanguageManager.h"
#include "rz/cRZCmdLine.h"
#include "gz/GZCOM.h"
#include "sc4/App.h"
#include "sc4/AppPlatform.h"
#include "sc4/AutoHelpers.h"
#include "sc4/ISC4City.h"
#include "sc4/cSC4City.h"
#include "sc4/DBSegment.h"
#include "sc4/ISC4City.h"
#include "sc4/SaveGame.h"

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

// The city's host services. The game implements this with the framework, the message server and GZCOM; the probe only
// needs the serial number and the language, and everything optional stays a no-op so the load path itself runs.
struct CityHost final : sc4::ICityHost {
    std::uint32_t CreateCitySerialNumber() override { return sc4::SC4CreateCitySerialNumber(); }
    void GetCurrentLanguageAndCountry(int& language, int& country) override { language = 0; country = 0; }
    cIGZUnknown* CreateComponent(std::uint32_t, std::uint32_t) override { return nullptr; }
    void PostMessage(cIGZMessage2*) override {}
    void SendMessage(cIGZMessage2*) override {}
};

// Every serialized object of a city file is created from its class id, as the game's serializer does through GZCOM
// (cSC4COMSerializer::OnGetGZSerializable -> GZCOM()->CreateInstance(classId, cIGZSerializable)). The probe goes
// through the generated GZCOM creator table (gz/GZCOM.h); classes whose creator is not linked or not recovered come
// back null and are listed, so the output names the next class LoadCity needs.
struct Factory final : sc4::ISerializableFactory {
    cIGZSerializable* CreateSerializable(std::uint32_t classId) override {
        if (classId == 0x8990c372) {                        // cSC4City: the probe's own host
            auto* city = new sc4::cSC4City(&host);
            city->AddRef();
            return city;
        }
        const gzcom::ClassFactoryInfo* info = gzcom::FindClassFactory(classId);
        void* object = info && info->create ? info->create() : nullptr;
        cIGZSerializable* serializable = nullptr;
        if (object) {
            auto* unknown = static_cast<cIGZUnknown*>(object);   // the creators hand out the primary (cIGZUnknown) pointer
            unknown->AddRef();
            if (!unknown->QueryInterface(sc4::kGZIID_cIGZSerializable, reinterpret_cast<void**>(&serializable))) serializable = nullptr;
            unknown->Release();
        }
        if (!serializable && missing.insert(classId).second)
            std::printf("  class %08x %s: %s\n", classId, info ? info->className : "(not in the GZCOM table)",
                        !info ? "unknown id" : object ? "no cIGZSerializable" : "creator returns null");
        return serializable;
    }
    CityHost host;
    std::set<std::uint32_t> missing;
};

void Step(const char* name, bool ok) { std::printf("[%s] %s\n", ok ? "OK" : "FAIL", name); std::fflush(stdout); }

}  // namespace

void InstallCityProbeBindings();   // city_probe_bindings.cpp

int main(int argc, char** argv) {
    const std::string bundle = std::string(std::getenv("HOME")) +
        "/Library/Application Support/Steam/steamapps/common/SimCity 4 Deluxe/Sim City 4 Deluxe Edition.app/Contents";
    const std::string docs = "/tmp/sc4city_docs";
    std::filesystem::create_directories(docs);
    SetDocumentsFolderProvider([&](std::string& out) { out = docs; return true; });

    std::string cityPath;
    for (int i = 1; i < argc; ++i)
        if (argv[i][0] != '-') cityPath = argv[i];
    if (cityPath.empty())
        cityPath = bundle + "/GameData/Regions/Timbuktu/City - New City (25).sc4";

    auto* framework = new cGZFramework();
    cGZFramework::sSetFrameWork(framework);
    framework->COM().RealInit();
    auto* app = new cSC4App("SimCity 4", framework->AppView());
    app->AddRef();
    app->SetFramework(framework);
    framework->AddSystemService(new cRZLanguageManager());
    auto* resources = new cGZPersistResourceManager();
    framework->AddSystemService(resources);

    Graphics graphics;
    Options options;
    app->StartupHooks().graphicSystem = [&] { return &graphics; };
    app->StartupHooks().createVideoOptionStore = [&] { return &options; };
    app->StartupHooks().messageBox = [](const std::string& text, const std::string&) { std::printf("  alert: %s\n", text.c_str()); };
    app->StartupHooks().initializeManager = [](const char* name, cIGZUnknown*) { std::printf("  init %s\n", name); };
    app->StartupHooks().fileSystemDirectories = [&](sc4::FileSystemDirectories& d) {
        d.appDirectory = bundle + "/MacOS/"; d.cdAppDirectory = bundle + "/MacOS/";
        d.dataDirectory = bundle + "/GameData/"; d.cdDataDirectory = bundle + "/GameData/";
        return true;
    };
    app->StartupHooks().bundleGameDataDirectory = [&] { return bundle + "/GameData"; };
    for (const char* name : {"cSC4GlyphTextureManager", "cSCStringDetokenizer", "cSLWinLocationSaver", "cSC4FeatureManager",
                             "cSC4ToolTipManager", "cSC4MissingPluginDataReporter", "cSC4Nation"})
        app->SetServiceFactory(name, []() -> cIGZUnknown* { return new Object(); });
    framework->SetCommandLine(cRZCmdLine("sc4"));

    // The city hooks LoadCity goes through: recovered classes and helpers all the way down.
    auto& hooks = app->CityHooks();
    static CityHost cityHost;
    static Factory factory;
    sc4::SetSerializableFactory(&factory);
    hooks.createCity = []() -> cISC4City* { auto* city = new sc4::cSC4City(&cityHost); city->AddRef(); return city; };
    hooks.createCitySegment = []() -> cIGZPersistDBSegment* {
        auto* segment = new sc4::cSC4DBSegment();
        segment->AddRef();
        std::printf("  city segment: created\n");
        return segment->AsIGZPersistDBSegment();
    };
    hooks.readLoadSaveVersion = [](cIGZPersistDBSegment* segment) {
        const std::uint32_t v = sc4::SC4ReadAppLoadSaveVersion(segment);
        std::printf("  save version: %u (loadable range %u..%u)\n", v, sc4::kLoadSaveVersionOldest, sc4::kLoadSaveVersionDefault);
        return v;
    };
    // loadClassObjects / readAppRecord stay unset: LoadCity runs the game's own cSC4DBSegment::LoadClassObjects and
    // reads the app record with cSC4AutoLoadStream.
    hooks.notify = [](const std::string& title, const std::string& text) { std::printf("  notify: %s - %s\n", title.c_str(), text.c_str()); };
    hooks.loadText = [](std::uint32_t, std::uint32_t) { return std::string(); };
    hooks.busy = [](bool) {};
    hooks.flushGraphics = [] {};
    hooks.destroyCurrentScreen = [] {};
    hooks.createCityScreen = [] {};
    hooks.createRegionScreen = [] {};

    Step("PreFrameWorkInit", app->PreFrameWorkInit());
    Step("Init", app->Init());
    Step("PostFrameWorkInit", app->PostFrameWorkInit());
    std::printf("  resource segments: %u\n", static_cast<unsigned>(resources->GetSegmentCount()));

    InstallCityProbeBindings();
    std::printf("  city file: %s\n", cityPath.c_str());
    std::fflush(stdout);
    const bool loaded = app->LoadCity(cRZString(cityPath.c_str()), nullptr);
    Step("LoadCity", loaded);
    std::printf("  classes the file needs that could not be created: %zu\n", factory.missing.size());
    if (loaded) {
        cISC4City* city = app->GetCity();
        if (city) {
            cRZString name;
            city->GetCityName(name);
            std::printf("  city loaded: \"%s\"\n", name.ToChar());
        } else {
            std::printf("  city: app reports a load but City() is null\n");
        }
    }

    Step("PreFrameWorkShutdown", app->PreFrameWorkShutdown());
    Step("Shutdown", app->Shutdown());
    return loaded ? 0 : 1;
}
