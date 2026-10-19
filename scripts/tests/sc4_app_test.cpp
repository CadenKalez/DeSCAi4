// cSC4App: accessors, derived directories, ini / preferences file names, shutdown callbacks, video option mapping,
// and the preferences blob defaults.
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <string>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <vector>
#include <unistd.h>

#include "rz/cRZCmdLine.h"
#include "gz/GZFramework.h"
#include "rz/LanguageManager.h"
#include "sc4/App.h"
#include "sc4/AppPlatform.h"
#include "sc4/AppPreferences.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

namespace {
struct Options final : sc4::IVideoOptionStore {
    std::map<int, int> values;
    void SetOption(int option, int value) override { values[option] = value; }
    int GetOption(int option) override { auto it = values.find(option); return it == values.end() ? 0 : it->second; }
};
struct Display final : sc4::IDisplayInfo {
    void GetNthNativeResolution(int index, int& w, int& h) override { w = 1000 + index; h = 700 + index; }
};
std::string Get(void (cSC4App::*getter)(cIGZString&), cSC4App& app) { cRZString s; (app.*getter)(s); return s.ToChar(); }
std::string Get(void (cSC4App::*getter)(cIGZString&) const, cSC4App& app) { cRZString s; (app.*getter)(s); return s.ToChar(); }
bool Shut(void* flag) { ++*static_cast<int*>(flag); return true; }
}  // namespace

int main() {
    auto* app = new cSC4App("SimCity 4");
    app->AddRef();
    CHECK(app->GetServiceID() == 0x66 && app->GetServicePriority() == -2000000);
    CHECK(app->GetAppState() == 0 && !app->GetDebugFunctionalityEnabled() && app->GetPopupDialogsEnabled());
    CHECK(app->IsRunFirstTimeAfterInstall());
    app->SetDebugFunctionalityEnabled(true); CHECK(app->GetDebugFunctionalityEnabled());
    app->SetPopupDialogsEnabled(false); CHECK(!app->GetPopupDialogsEnabled());

    // directories: cached ones return what was set, derived ones come from the user data directory
    app->SetDirectories("/Games/SC4/", "/Games/SC4/Data/", "/Users/me/SC4/");
    CHECK(Get(&cSC4App::GetAppDirectory, *app) == "/Games/SC4/");
    cRZString out;
    app->GetDataDirectory(out, -1); CHECK(std::strcmp(out.ToChar(), "/Games/SC4/Data/") == 0);
    CHECK(Get(&cSC4App::GetUserPluginDirectory, *app) == "/Users/me/SC4/Plugins/");
    CHECK(Get(&cSC4App::GetRegionsDirectory, *app) == "/Users/me/SC4/Regions/");
    CHECK(Get(&cSC4App::GetMySimDirectory, *app) == "/Users/me/SC4/MySim/");
    CHECK(Get(&cSC4App::GetAlbumDirectory, *app) == "/Users/me/SC4/Albums/");
    CHECK(Get(&cSC4App::GetHTTPCacheDirectory, *app) == "/Users/me/SC4/HTTPCache/");
    CHECK(Get(&cSC4App::GetTempDirectory, *app) == "/Users/me/SC4/Temp/");
    CHECK(Get(&cSC4App::GetExceptionReportsDirectory, *app) == "/Users/me/SC4/Exception Reports/");
    CHECK(Get(&cSC4App::GetTestScriptDirectory, *app) == "/Games/SC4/Data/Scripts/TestScripts/");

    // ini / cfg names: default names while the executable name is not known
    CHECK(Get(&cSC4App::GetAppIniFileName, *app) == "SimCity 4.ini");
    app->AppNameString() = "SC4Mac";
    CHECK(Get(&cSC4App::GetAppIniFileName, *app) == "SC4Mac.ini");
    CHECK(Get(&cSC4App::GetAppPreferencesFileName, *app) == "SC4Mac.cfg");
    CHECK(Get(&cSC4App::GetAppPreferencesFilePath, *app) == "/Users/me/SC4/SC4Mac.cfg");
    CHECK(Get(&cSC4App::GetAppIniFilePath, *app) == "/Users/me/SC4/SC4Mac.ini");   // no .ini beside the (fake) app directory

    // shutdown callbacks
    int calls = 0;
    CHECK(app->RegisterShutdownCallbackFunction(Shut, &calls));
    CHECK(app->UnregisterShutdownCallbackFunction(Shut, &calls));
    CHECK(!app->UnregisterShutdownCallbackFunction(Shut, &calls));

    // video preferences <-> option store
    Options options; Display display;
    app->SetVideoOptionStore(&options); app->SetDisplayInfo(&display);
    cISC4App::SC4VideoPreferences prefs;
    auto* b = reinterpret_cast<std::uint8_t*>(&prefs);
    for (int i = 1; i <= 0xc; ++i) b[i] = static_cast<std::uint8_t>(i);
    b[0x1c] = 9;
    app->ApplyVideoPreferences(prefs);
    CHECK(options.values[0] == 1 && options.values[1] == 2 && options.values[2] == 3 && options.values[3] == 4);
    CHECK(options.values[4] == 5 && options.values[5] == 6 && options.values[6] == 7 && options.values[7] == 8);
    CHECK(options.values[8] == 9 && options.values[9] == 10 && options.values[10] == 0xc && options.values[0xf] == 9);
    options.values[0xb] = 2; options.values[0xc] = 1;
    cISC4App::SC4VideoPreferences got;
    app->GetAutoVideoPreferences(&got);
    CHECK(reinterpret_cast<std::uint8_t*>(&got)[1] == 1 && reinterpret_cast<std::uint8_t*>(&got)[0xb] == 1);
    CHECK(got.screenWidth == 1002 && got.screenHeight == 702 && got.colorDepth == 32);
    options.values[0xc] = 0; app->GetAutoVideoPreferences(&got); CHECK(got.colorDepth == 16);
    options.values[0xc] = 7; got.colorDepth = 24; app->GetAutoVideoPreferences(&got); CHECK(got.colorDepth == 24);

    // interface lookup
    void* iface = nullptr;
    CHECK(app->QueryInterface(kGZIID_cISC4App, &iface) && iface == static_cast<cISC4App*>(app));
    CHECK(app->QueryInterface(0x452294aa, &iface) && iface == static_cast<cIGZMessageTarget2*>(app));
    CHECK(app->QueryInterface(0xc6ae7085, &iface) && iface == static_cast<cIGZWinMessageFilter*>(app));
    CHECK(app->QueryInterface(0xca263481, &iface) && iface == static_cast<cIGZMessageTarget*>(app));
    CHECK(app->QueryInterface(kGZIID_cIGZApp, &iface) && iface == static_cast<cIGZApp*>(app));
    CHECK(!app->QueryInterface(0x1234, &iface));


    // ---- start-up resolution
    {
        auto* a = new cSC4App("T"); a->AddRef();
        CHECK(a->IsStartupResolutionOK(1024, 768) && a->IsStartupResolutionOK(1280, 1024) && a->IsStartupResolutionOK(1400, 1050 - 0 + 0) == false);
        CHECK(a->IsStartupResolutionOK(1680, 1050) && a->IsStartupResolutionOK(1920, 1200) && !a->IsStartupResolutionOK(1000, 768));
        CHECK(!a->IsStartupResolutionOK(800, 480));
        std::uint32_t w = 1000, h = 700;                    // area 700000 -> 1024x768
        a->FixStartupResolutionValuesIfNeeded(w, h); CHECK(w == 0x400 && h == 0x300);
        w = 640; h = 480; a->FixStartupResolutionValuesIfNeeded(w, h); CHECK(w == 800 && h == 600);
        w = 3000; h = 2000; a->FixStartupResolutionValuesIfNeeded(w, h); CHECK(w == 0x400 && h == 0x300);   // quirk: huge areas fall back to 1024x768
        w = 1800; h = 1000; a->FixStartupResolutionValuesIfNeeded(w, h); CHECK(w == 0x640 && h == 0x4b0);
        w = 2000; h = 1400; a->FixStartupResolutionValuesIfNeeded(w, h); CHECK(w == 0x800 && h == 0x600);
        w = 1024; h = 768; a->FixStartupResolutionValuesIfNeeded(w, h); CHECK(w == 1024 && h == 768);
        a->Release();
    }

    // ---- command line
    {
        auto* a = new cSC4App("T"); a->AddRef();
        auto* props = new sc4::cSC4RenderProperties(); a->SetRenderProperties(props);
        a->SetDirectories("/g/", "/g/Data/", "/u/");
        cRZCmdLine line("sc4 -IME:off -r1280x1024x32 -w -d:OpenGL -l:\"Mayor Bob\" -audio:off -radio:enabled -sim:75.5 -XDD:on -CPUCount:2 -AutoZoomChange:off -UserDir:off");
        a->SetCommandLineSource(&line);
        a->ReadCommandLine();
        auto& p = a->Preferences();
        CHECK(p.Get<std::uint8_t>(sc4::prefs::kImeEnabled) == 0);
        CHECK(p.Get<std::uint32_t>(sc4::prefs::kVideoWidth) == 1280 && p.Get<std::uint32_t>(sc4::prefs::kVideoHeight) == 1024 && p.Get<std::uint32_t>(sc4::prefs::kVideoColorDepth) == 32);
        CHECK(p.Get<std::uint8_t>(sc4::prefs::kFullScreen) == 0);
        CHECK(std::strcmp(reinterpret_cast<const char*>(p.data() + sc4::prefs::kLanguageName), "Mayor Bob") == 0);
        CHECK(p.Get<std::uint8_t>(sc4::prefs::kAudioEnabled) == 0 && p.Get<std::uint8_t>(sc4::prefs::kAudioEnabledMirror) == 0);
        CHECK(p.Get<std::uint32_t>(sc4::prefs::kMusicEnabled) == 1);
        CHECK(p.Get<float>(sc4::prefs::kSimulatorParameter) == 75.5f && p.Get<std::uint8_t>(sc4::prefs::kSwitchXDD) == 1);
        CHECK(!props->BoolValue(0x25));
        CHECK(Get(&cSC4App::GetUserDataDirectory, *a) == "/g/Data/");
        a->Release();            // the application owns (and frees) the render properties
    }

    // ---- preferences file round trip
    {
        char dir[] = "/tmp/sc4appXXXXXX"; CHECK(mkdtemp(dir));
        std::string user = std::string(dir) + "/";
        auto* a = new cSC4App("Tst"); a->AddRef();
        a->SetDirectories("/g/", "/g/Data/", user.c_str());
        a->AppNameString() = "Tst";
        a->Preferences().Set<std::uint32_t>(sc4::prefs::kVideoWidth, 1600);
        a->Preferences().Set<float>(sc4::prefs::kSimulatorParameter, 12.5f);
        std::memcpy(a->Preferences().data() + sc4::prefs::kLanguageName, "temp", 5);
        a->SavePreferences();
        CHECK(a->Preferences().Get<std::uint32_t>(sc4::prefs::kAppResetFlag) == 1 && a->Preferences().data()[sc4::prefs::kLanguageName] == 0);
        auto* b = new cSC4App("Tst"); b->AddRef();
        b->SetDirectories("/g/", "/g/Data/", user.c_str());
        b->AppNameString() = "Tst";
        CHECK(b->IsRunFirstTimeAfterInstall());
        b->ReadPreferences();
        CHECK(!b->IsRunFirstTimeAfterInstall());
        CHECK(b->Preferences().Get<std::uint32_t>(sc4::prefs::kVideoWidth) == 1600 && b->Preferences().Get<float>(sc4::prefs::kSimulatorParameter) == 12.5f);
        CHECK(std::memcmp(a->Preferences().data(), b->Preferences().data(), sc4::kPreferencesSize) == 0);
        a->Release(); b->Release();
        std::string cleanup = "rm -rf " + std::string(dir); std::system(cleanup.c_str());
    }


    // ---- AddCOMDirectors / AddSystemServices against the real framework
    {
        struct Fake final : cRZSystemService {
            Fake(std::uint32_t id) : cRZSystemService(id, 1000) {}
            bool QueryInterface(std::uint32_t iid, void** out) override { (void)iid; *out = static_cast<cIGZSystemService*>(this); AddRef(); return true; }
            std::uint32_t AddRef() override { return cRZSystemService::AddRef(); }
            std::uint32_t Release() override { return cRZSystemService::Release(); }
        };
        auto* f = new cGZFramework();
        cGZFramework::sSetFrameWork(f);
        f->COM().RealInit();
        auto* a = new cSC4App("T", f->AppView()); a->AddRef();
        a->SetFramework(f);
        a->SetDirectories("/g/", "/g/Data/", "/u/");
        struct { const char* name; std::uint32_t id; } fakes[] = {{"cGZAllocatorService", 0x3a}, {"cGZFileSystem", 0x3b}, {"nGZLog::cService", 0x3e}, {"cGZRegistry", 0x3f},
            {"cRZLanguageManager", 0x40}, {"cGZPersistResourceManager", 0x41}, {"cSC4AudioAEMSEventSystem", 0x42}};
        for (auto& fake : fakes) { std::uint32_t id = fake.id; a->SetServiceFactory(fake.name, [id]() -> cIGZUnknown* { return static_cast<cIGZUnknown*>(static_cast<cIGZSystemService*>(new Fake(id))); }); }
        a->StartupHooks().showFpsPreference = [] { return true; };
        a->AddCOMDirectors();
        a->AddSystemServices();
        void* out = nullptr;
        for (auto& fake : fakes) CHECK(f->GetSystemService(fake.id, kGZIID_cIGZSystemService, &out));
        CHECK(!f->GetSystemService(0x99, kGZIID_cIGZSystemService, &out));
    }


    // ---- PreFrameWorkInit end to end (real framework, real language manager, fake graphics system)
    {
        struct Graphics final : sc4::IGraphicSystemControl {
            DisplayMetrics screen{1440, 900, 32};
            std::vector<std::string> log; DisplayMetrics desired; bool windowed = true; std::uint32_t driver = 0;
            DisplayMetrics GetScreenMetrics() override { return screen; }
            void PreInitSetDesiredGameResolution(const DisplayMetrics& m) override { desired = m; log.push_back("resolution"); }
            void PreInitSetWindowedMode(bool w) override { windowed = w; log.push_back("windowed"); }
            void DisableScreenBuffer() override { log.push_back("disable"); }
            void EnableCursorEmulation(bool e) override { log.push_back(e ? "cursor-on" : "cursor-off"); }
            void EnablePageFlipping(bool) override { log.push_back("flip"); }
            bool IsPageFlippingEnabled() override { return true; }
            void SetDefaultDriverClassID(std::uint32_t d) override { driver = d; log.push_back("driver"); }
        };
        char dir[] = "/tmp/sc4pfiXXXXXX"; CHECK(mkdtemp(dir));
        std::string root = dir;
        std::filesystem::create_directories(root + "/Data/English");
        std::ofstream(root + "/Data/English/text.str") << "x";
        std::filesystem::create_directories(root + "/Data/German");
        std::ofstream(root + "/Data/German/data.dat") << "x";
        std::filesystem::create_directories(root + "/Data/French");        // no string tables: not available
        SetDocumentsFolderProvider([&](std::string& out) { out = root + "/Documents"; return true; });
        std::filesystem::create_directories(root + "/Documents");

        auto* f = new cGZFramework();
        cGZFramework::sSetFrameWork(f);
        f->COM().RealInit();
        auto* a = new cSC4App("T", f->AppView()); a->AddRef();
        a->SetFramework(f);
        auto* languages = new cRZLanguageManager();
        a->SetServiceFactory("cRZLanguageManager", [languages]() -> cIGZUnknown* { return static_cast<cIGZUnknown*>(static_cast<cIGZSystemService*>(languages)); });
        Graphics graphics;
        a->StartupHooks().graphicSystem = [&] { return &graphics; };
        a->StartupHooks().fileSystemDirectories = [&](sc4::FileSystemDirectories& d) { d.appDirectory = root + "/App/"; d.cdAppDirectory = root + "/CD/"; d.dataDirectory = root + "/Data/"; d.cdDataDirectory = root + "/CDData/"; return true; };
        a->StartupHooks().bundleGameDataDirectory = [&] { return root + "/Data"; };
        cRZCmdLine line("sc4 -r1280x1024x32 -d:opengl -cursormode:blit");
        f->SetCommandLine(line);
        CHECK(a->PreFrameWorkInit());
        CHECK(a->Preferences().Get<std::uint32_t>(sc4::prefs::kVideoWidth) == 1280);
        CHECK(graphics.log.size() >= 6 && graphics.log[0] == "disable" && graphics.log[1] == "cursor-on" && graphics.log[2] == "flip");
        // the data directories, the derived ones, the user data directory
        CHECK(Get(&cSC4App::GetPluginDirectory, *a) == root + "/Data/Plugins/");
        CHECK(Get(&cSC4App::GetUserDataDirectory, *a) == root + "/Documents/SimCity 4/");
        CHECK(std::filesystem::is_directory(root + "/Documents/SimCity 4"));
        CHECK(Get(&cSC4App::GetAppDirectory, *a) == root + "/App/");
        // languages: English and German are available (string tables / data), French is not
        for (std::uint32_t id = 1; id <= 0x2c; ++id) {
            cRZString folder;
            if (!languages->GetLanguageDirectoryName(folder, id)) continue;
            std::string name = folder.ToChar();
            if (name == "English" || name == "German") CHECK(languages->CanSwitchToLanguage(id));
            if (name == "French") CHECK(!languages->CanSwitchToLanguage(id));
        }
        CHECK(languages->GetCurrentLanguage() == a->Preferences().Get<std::uint32_t>(sc4::prefs::kLanguageId));
        CHECK(graphics.driver == 0xc4554841u);                           // -d:opengl
        CHECK(a->Preferences().data()[sc4::prefs::kVideoHardwareDriver] == 0xff);   // not the DirectX id: flagged "other"
        CHECK(!graphics.windowed);                                        // 1280x1024 does not fit the 1440x900 desktop -> full screen
        CHECK(a->GetRenderProperties() != nullptr);
        a->Release();
        std::string cleanup = "rm -rf " + root; std::system(cleanup.c_str());
    }

    // preferences blob defaults
    sc4::SC4Preferences p;
    CHECK(sc4::SC4Preferences::size() == 0x1734);
    CHECK(p.Get<std::uint8_t>(0x004) == 1 && p.Get<std::uint32_t>(0x008) == 0x400 && p.Get<std::uint32_t>(0x00c) == 0x320);
    CHECK(p.Get<std::uint8_t>(0x04 + 0x28) == 1 && p.Get<std::uint8_t>(0x04 + 0x24) == 0 && p.Get<std::uint8_t>(0x04 + 0x63) == 1);
    CHECK(p.Get<std::uint32_t>(0x470) == 0x42480000 && p.Get<std::uint8_t>(0x49c) == 1);
    CHECK(p.Get<std::uint32_t>(0x4a8) == 1 && p.Get<std::uint32_t>(0x4a4 + 0x48) == 0x1000101);
    CHECK(p.Get<std::uint8_t>(sc4::prefs::kVideoAutoConfigure) == 1 && p.Get<std::uint8_t>(sc4::prefs::kVideoHardwareDriver) == 1);
    CHECK(p.Get<std::uint32_t>(sc4::prefs::kVideoWidth) == 1024 && p.Get<std::uint32_t>(sc4::prefs::kVideoHeight) == 768);
    CHECK(p.Get<std::uint8_t>(0xf28) == 3 && p.Get<std::uint16_t>(0xf16) == 0x103);

    app->Release(); app->Release();
    std::printf(failures ? "FAILED\n" : "OK\n");
    return failures ? 1 : 0;
}
