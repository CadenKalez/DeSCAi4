// cSC4App — the game application (223 functions, 0x1000039e2..0x1000b2d1e; 0x7d8 bytes). It is the cGZApp that the
// framework starts: PreFrameWorkInit registers the COM directors and system services, Init brings up the game's
// managers, the window manager and the main window, and the city lifecycle (new / load / save / close) lives here.
//
// Interfaces (vtable groups): +0 the class's own virtuals and cIGZMessageTarget (iids 0xca263481 / 0xe98b2f57),
// +8 cIGZMessageTarget2 (0x452294aa), +0x10 the window message filter (0xc6ae7085), +0x18 cISC4App (0x26ce01c0),
// +0x20 cGZApp (system service id 0x66, priority -2000000, iid 0x66) with its cIGZApp view at +0x48.
//
// The data directories are cached strings; the ones that are empty are derived from another directory on first use.
#pragma once
#include <cstdint>
#include <list>
#include <memory>

#include "gz/GZApp.h"
#include "gz/WinInterfaces.h"
#include "gz/cGZMessageServer.h"
#include "gz/cIGZMessage2.h"
#include "rz/Threading.h"
#include "rz/cRZTimer.h"
#include "rz/cRZCmdLine.h"
#include "sc4/AppPreferences.h"
#include "sc4/RenderProperties.h"
#include "sc4/ISC4App.h"

class cIGZFrameWork;

namespace sc4 {

// The configuration option store the video preferences go through (object 0xa1d8285 = cSC4ConfigRuleManager, iid
// 0xa1d8284): option number -> value.
class IVideoOptionStore {
public:
    virtual ~IVideoOptionStore() = default;
    virtual void SetOption(int option, int value) = 0;        // vslot 0x30
    virtual int GetOption(int option) = 0;                    // vslot 0x38
};

// The native resolutions of the attached display (MacDisplay::GetNthNativeResolution).
class IDisplayInfo {
public:
    virtual ~IDisplayInfo() = default;
    virtual void GetNthNativeResolution(int index, int& width, int& height) = 0;
};

}  // namespace sc4

class cSC4App final : public cIGZMessageTarget, public cIGZMessageTarget2, public cIGZWinMessageFilter,
                      public cISC4App, public cGZApp {
public:
    using SC4VideoPreferences = sc4::SC4VideoPreferences;

    explicit cSC4App(const char* moduleName, IGZAppFramework* framework = nullptr);   // @ 0x1000039e2
    ~cSC4App();                                        // @ 0x10000447c

    // --- identity / reference counting
    bool QueryInterface(std::uint32_t iid, void** out) override;   // @ 0x10000afb8
    std::uint32_t AddRef() override;                               // @ 0x10000b06c
    std::uint32_t Release() override;                              // @ 0x10000b0ca

    // --- cISC4App accessors
    void GetAppName(cIGZString& out) const override;               // @ 0x1000122b2
    cISC4City* GetCity() const override { return city_; }          // @ 0x10001231a
    std::uint8_t* GetPreferences() const override { return const_cast<sc4::SC4Preferences*>(&preferences_)->data(); }   // @ 0x100012336
    cSC4NewCitySpecification* GetNewCitySpecification() override { return reinterpret_cast<cSC4NewCitySpecification*>(newCitySpecification_); }   // @ 0x100012352
    void GetAppDirectory(cIGZString& out) override { out.AssignFromCString(appDirectory_.ToChar()); }               // @ 0x10001236e
    void GetCDAppDirectory(cIGZString& out) override { out.AssignFromCString(cdAppDirectory_.ToChar()); }          // @ 0x1000123d6
    void GetDataDirectory(cIGZString& out, int language) override;       // @ 0x10001243e
    void GetCDDataDirectory(cIGZString& out, int language) override;     // @ 0x10001253e
    void GetPluginDirectory(cIGZString& out) override { out.AssignFromCString(pluginDirectory_.ToChar()); }         // @ 0x10001263e
    void GetCDPluginDirectory(cIGZString& out) override { out.AssignFromCString(cdPluginDirectory_.ToChar()); }    // @ 0x1000126a6
    void GetSkuSpecificDirectory(cIGZString& out) override { out.AssignFromCString(skuSpecificDirectory_.ToChar()); }   // @ 0x10001270e
    void GetUserDataDirectory(cIGZString& out) override { out.AssignFromCString(userDataDirectory_.ToChar()); }     // @ 0x100012776
    void GetUserPluginDirectory(cIGZString& out) override;         // @ 0x1000127de
    void GetRegionsDirectory(cIGZString& out) override;            // @ 0x10001288e
    void GetMySimDirectory(cIGZString& out) override;              // @ 0x10001293e
    void GetAlbumDirectory(cIGZString& out) override;              // @ 0x1000129ee
    void GetHTTPCacheDirectory(cIGZString& out) override;          // @ 0x100012a9e
    void GetTempDirectory(cIGZString& out) override;               // @ 0x100012b4e
    void GetExceptionReportsDirectory(cIGZString& out) override;   // @ 0x100012bfe
    void GetTestScriptDirectory(cIGZString& out) override;         // @ 0x100012cae
    void GetAppIniFileName(cIGZString& out) const override;        // @ 0x100012d8a
    void GetAppIniFilePath(cIGZString& out) const override;        // @ 0x100012e1e
    void GetAppPreferencesFileName(cIGZString& out) const override;   // @ 0x100012fa4
    void GetAppPreferencesFilePath(cIGZString& out) const override;   // @ 0x100013038
    bool RegisterShutdownCallbackFunction(ShutdownCallback callback, void* context) override;     // @ 0x10001311e
    bool UnregisterShutdownCallbackFunction(ShutdownCallback callback, void* context) override;   // @ 0x1000131ce
    void ApplyVideoPreferences(const SC4VideoPreferences& preferences) override;   // @ 0x10001323c
    void GetAutoVideoPreferences(SC4VideoPreferences* preferences) override;       // @ 0x100013430
    bool GetDebugFunctionalityEnabled() const override { return debugFunctionality_; }    // @ 0x1000135cc
    void SetDebugFunctionalityEnabled(bool enabled) override { debugFunctionality_ = enabled; }   // @ 0x1000135e4
    bool GetPopupDialogsEnabled() const override { return popupDialogs_; }                // @ 0x100013600
    void SetPopupDialogsEnabled(bool enabled) override { popupDialogs_ = enabled; }       // @ 0x100013618
    std::uint32_t GetAppState() const override { return appState_; }                      // @ 0x100013634
    cIGZWin* GetMainWindow() const override { return mainWindow_; }                       // @ 0x10001363e
    cIGZUnknown* GetFeatureManager() const override { return featureManager_; }           // @ 0x10001364c
    cIGZUnknown* GetCheatCodeManager() const override { return cheatCodeManager_; }       // @ 0x10001365a
    cISC4Nation* GetNation() override { return nation_; }                                 // @ 0x100013668
    cISC4Region* GetRegion() override;                                                    // @ 0x100013676
    cISC4RegionalCity* GetRegionalCity() override;                                        // @ 0x100013688
    cIGZUnknown* GetDebugConsole() const override { return debugConsole_; }               // @ 0x1000136c2
    cIGZUnknown* GetGimexFactory() const override { return gimexFactory_; }               // @ 0x1000136d0
    cIGZUnknown* GetStringDetokenizer() const override { return stringDetokenizer_; }     // @ 0x1000136de
    cIGZUnknown* GetWinLocationSaver() const override { return winLocationSaver_; }       // @ 0x1000136ec
    cIGZUnknown* GetRenderProperties() const override { return renderProperties_; }       // @ 0x1000136fa
    cIGZUnknown* GetGlyphTextureManager() const override { return glyphTextureManager_; } // @ 0x100013708
    cIGZUnknown* GetLuaInterpreter() const override { return luaInterpreter_; }           // @ 0x100013716
    cSC4TutorialRegistry* GetTutorialRegistry() override { return tutorialRegistry_; }    // @ 0x100013724
    bool IsRunFirstTimeAfterInstall() override { return runFirstTimeAfterInstall_; }      // @ 0x100013732

    // --- command line and start-up resolution
    void ReadCommandLine();                                        // @ 0x10000f814
    void FixStartupResolutionValuesIfNeeded(std::uint32_t& width, std::uint32_t& height);   // @ 0x10000acba
    bool IsStartupResolutionOK(std::uint32_t width, std::uint32_t height);                  // @ 0x10000ad7a

    // --- host seams (the original reached these through RZ:: / MacDisplay::)
    void SetCommandLineSource(cIGZCmdLine* commandLine) { commandLine_ = commandLine; }   // cGZFramework::FrameWork()->CommandLine()
    void SetRenderProperties(sc4::cSC4RenderProperties* properties);
    void SetVideoOptionStore(sc4::IVideoOptionStore* store) { videoOptions_ = store; }
    void SetDisplayInfo(sc4::IDisplayInfo* display) { display_ = display; }
    // The directories the framework fills in at start-up (PreFrameWorkInit); exposed for the host and tests.
    void SetDirectories(const char* appDirectory, const char* dataDirectory, const char* userDataDirectory);
    cRZString& AppNameString() { return appName_; }
    sc4::SC4Preferences& Preferences() { return preferences_; }

    // --- state shared with the not-yet-recovered parts
    void SetAppState(std::uint32_t state) { appState_ = state; }
    void SetMainWindow(cIGZWin* window) { mainWindow_ = window; }
    void SetCity(cISC4City* city) { city_ = city; }
    void SetNation(cISC4Nation* nation) { nation_ = nation; }
    void SetRegionalCityHolder(cRZAutoRefCount<cISC4RegionalCity>* holder) { regionalCity_ = holder; }
    void SetNationRegionSource(cISC4Region* (*regionOf)(cISC4Nation*)) { regionOf_ = regionOf; }

    // --- cIGZMessageTarget / cIGZMessageTarget2 / window filter
    bool DoMessage(cGZMessage& message) override;                  // @ 0x100011a14
    bool DoMessage(cIGZMessage2* message) override;                // @ 0x100011df8
    bool DoMessage(cIGZWin* window, cGZMessage& message) override; // @ 0x100046e70

    // --- remaining virtuals of cISC4App / cGZApp (see AppPending.cpp until they are recovered)
    bool OnIdle(std::uint32_t elapsed) override;
    void RunMessageServerPump(std::uint32_t a, std::uint32_t b, std::uint32_t c) override;
    void RunMessageServer2Pump(std::uint32_t a, std::uint32_t b, std::uint32_t c) override;
    bool RequestNewCity(cRZAutoRefCount<cISC4RegionalCity>* city) override;
    bool RequestLoadCity() override;
    bool RequestCloseCity(bool a) override;
    bool RequestSaveCity(bool a, bool b) override;
    bool RequestQuit(bool a, bool b) override;
    bool RequestQuitFromRegion(bool a) override;
    bool RequestGoToRegionView(bool a) override;
    bool LoadCity(const cRZString& path, cRZAutoRefCount<cISC4RegionalCity>* city) override;
    bool CloseCity() override;
    bool SaveCity(bool a) override;
    bool SaveCity(const cRZString& path, bool a) override;
    void SavePreferences() override;                               // @ 0x10000f670
    void ReadPreferences();                                        // @ 0x10000f35c
    void EnableFullGamePauseOnAppFocusLoss(bool enable) override;
    bool AddDynamicLibraryByName(const cIGZString& name, cRZString* resolvedPath, bool required) override;
    bool AddDynamicLibraryByPath(const cIGZString& path, bool required) override;
    bool Init() override;
    bool Shutdown() override;
    bool PreFrameWorkInit() override;
    bool PostFrameWorkInit() override;
    bool PreFrameWorkShutdown() override;
    bool LoadRegistry() override;
    bool OnTick(std::uint32_t elapsed) override;

private:
    struct ShutdownCallbackData { ShutdownCallback callback; void* context; };   // cSC4App::ShutdownCallbackData

    // +0x78 ...
    std::uint32_t appState_ = 0;                         // +0x78
    bool field7c_ = false;                               // +0x7c
    cIGZWin* mainWindow_ = nullptr;                      // +0x80
    void* field88_ = nullptr;                            // +0x88
    cISC4Nation* nation_ = nullptr;                      // +0x90
    cRZAutoRefCount<cISC4RegionalCity>* regionalCity_ = nullptr;   // +0x98 (set by RequestNewCity / LoadCity)
    cISC4City* city_ = nullptr;                          // +0xa0
    bool fieldA8_ = false;                               // +0xa8
    cRZString appName_;                                  // +0xb0 (the executable's file name)
    cRZString appDirectory_;                             // +0xd8
    cRZString cdAppDirectory_;                           // +0x100
    cRZString dataDirectory_;                            // +0x128
    cRZString cdDataDirectory_;                          // +0x150
    cRZString pluginDirectory_;                          // +0x178
    cRZString cdPluginDirectory_;                        // +0x1a0
    cRZString string1c8_;                                // +0x1c8
    cRZString skuSpecificDirectory_;                     // +0x1f0
    cRZString userDataDirectory_;                        // +0x218
    cRZString userPluginDirectory_;                      // +0x240
    cRZString regionsDirectory_;                         // +0x268
    cRZString mySimDirectory_;                           // +0x290
    cRZString albumDirectory_;                           // +0x2b8
    cRZString httpCacheDirectory_;                       // +0x2e0
    cRZString tempDirectory_;                            // +0x308
    cRZString exceptionReportsDirectory_;                // +0x330
    cRZString testScriptDirectory_;                      // +0x358
    cSC4TutorialRegistry* tutorialRegistry_ = nullptr;   // +0x380 (embedded in the original)
    cIGZUnknown* featureManager_ = nullptr;              // +0x3b8
    cIGZUnknown* cheatCodeManager_ = nullptr;            // +0x3c0
    cRZTimer timer3d0_{cRZTimer::kMinutes};              // +0x3d0
    int interval3e8_ = 30;                               // +0x3e8
    cRZTimer timer3f0_{cRZTimer::kMinutes};              // +0x3f0
    int interval408_ = 5;                                // +0x408
    cRZTimer timer410_{cRZTimer::kMilliseconds};         // +0x410
    cRZTimer timer428_{cRZTimer::kSeconds};              // +0x428
    cRZTimer timer440_{cRZTimer::kSeconds};              // +0x440
    int interval458_ = 30, interval45c_ = 5;             // +0x458 / +0x45c
    sc4::SC4Preferences preferences_;                    // +0x460 (a heap block in the original)
    std::uint8_t newCitySpecification_[0x200] = {};      // +0x468
    cIGZUnknown* field668_ = nullptr;                    // +0x668
    cIGZUnknown* debugConsole_ = nullptr;                // +0x678
    cIGZUnknown* gimexFactory_ = nullptr;                // +0x680
    cIGZUnknown* stringDetokenizer_ = nullptr;           // +0x688
    cIGZUnknown* winLocationSaver_ = nullptr;            // +0x690
    std::list<ShutdownCallbackData> shutdownCallbacks_;  // +0x698
    FileChangeNotification pluginWatcher_;               // +0x6b8
    FileChangeNotification dataWatcher_;                 // +0x708
    sc4::cSC4RenderProperties* renderProperties_ = nullptr;   // +0x760
    sc4::IVideoOptionStore* videoOptions_ = nullptr;     // +0x768
    cIGZUnknown* winCommandSupport_ = nullptr;           // +0x770
    cIGZUnknown* toolCommandSupport_ = nullptr;          // +0x778
    cIGZUnknown* miscCommandSupport_ = nullptr;          // +0x780
    cIGZUnknown* field788_ = nullptr;                    // +0x788
    cRZMutex instanceMutex_{cRZString("SimCity 4 Mutex")};   // +0x790
    bool debugFunctionality_ = false;                    // +0x7a0
    bool popupDialogs_ = true;                           // +0x7a1
    cIGZUnknown* glyphTextureManager_ = nullptr;         // +0x7c0
    cIGZUnknown* luaInterpreter_ = nullptr;              // +0x7c8
    bool runFirstTimeAfterInstall_ = true;               // +0x7d0

    sc4::IDisplayInfo* display_ = nullptr;
    cIGZCmdLine* commandLine_ = nullptr;
    std::uint32_t requestedGraphicsDriver_ = 0;          // +0x7b8 (set by the -d switch)
    cISC4Region* (*regionOf_)(cISC4Nation*) = nullptr;
};
