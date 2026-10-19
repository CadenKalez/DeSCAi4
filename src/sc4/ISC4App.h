// cISC4App — the application interface every game system reaches through (iid 0x26ce01c0, the third vtable group of
// cSC4App @ 0x100a67360, slots 3..68). The managers it hands out are interfaces recovered elsewhere; the ones that are
// not yet recovered stay opaque here.
#pragma once
#include <cstdint>

#include "gz/cIGZString.h"
#include "gz/cIGZUnknown.h"
#include "rz/cRZAutoRefCount.h"
#include "rz/cRZString.h"

class cIGZWin;
class cISC4City;
class cISC4Nation;
class cISC4Region;
class cISC4RegionalCity;
class cSC4NewCitySpecification;
class cSC4TutorialRegistry;

constexpr std::uint32_t kGZIID_cISC4App = 0x26ce01c0;

namespace sc4 {

// The video options as the video preferences dialog edits them (SC4VideoPreferences, 0x20 bytes). The byte options at
// +1..+0xc and +0x1c mirror options of the configuration rule manager (see cSC4App::ApplyVideoPreferences for the map).
struct SC4VideoPreferences {
    std::uint8_t reserved0 = 0;
    std::uint8_t option[0x0f] = {};          // +1 .. +0xf (only the indices used by Apply / GetAuto are meaningful)
    std::int32_t screenWidth = 0;            // +0x10
    std::int32_t screenHeight = 0;           // +0x14
    std::int32_t colorDepth = 0;             // +0x18
    std::uint8_t option1c = 0;               // +0x1c
    std::uint8_t pad[3] = {};
};
static_assert(sizeof(SC4VideoPreferences) == 0x20);

}  // namespace sc4

class cISC4App : public cIGZUnknown {
public:
    using SC4VideoPreferences = sc4::SC4VideoPreferences;
    using ShutdownCallback = bool (*)(void*);

    virtual bool OnIdle(std::uint32_t elapsed) = 0;                                             // [3]
    virtual void RunMessageServerPump(std::uint32_t a, std::uint32_t b, std::uint32_t c) = 0;   // [4]
    virtual void RunMessageServer2Pump(std::uint32_t a, std::uint32_t b, std::uint32_t c) = 0;  // [5]
    virtual bool RequestNewCity(cRZAutoRefCount<cISC4RegionalCity>* city) = 0;                  // [6]
    virtual bool RequestLoadCity() = 0;                                                         // [7]
    virtual bool RequestCloseCity(bool a) = 0;                                                  // [8]
    virtual bool RequestSaveCity(bool a, bool b) = 0;                                           // [9]
    virtual bool RequestQuit(bool a, bool b) = 0;                                               // [10]
    virtual bool RequestQuitFromRegion(bool a) = 0;                                             // [11]
    virtual bool RequestGoToRegionView(bool a) = 0;                                             // [12]
    virtual bool LoadCity(const cRZString& path, cRZAutoRefCount<cISC4RegionalCity>* city) = 0; // [13]
    virtual bool CloseCity() = 0;                                                               // [14]
    virtual bool SaveCity(bool a) = 0;                                                          // [15]
    virtual bool SaveCity(const cRZString& path, bool a) = 0;                                   // [16]
    virtual void SavePreferences() = 0;                                                         // [17]
    virtual void EnableFullGamePauseOnAppFocusLoss(bool enable) = 0;                            // [18]
    virtual void ApplyVideoPreferences(const SC4VideoPreferences& preferences) = 0;             // [19]
    virtual void GetAutoVideoPreferences(SC4VideoPreferences* preferences) = 0;                 // [20]
    virtual bool GetDebugFunctionalityEnabled() const = 0;                                      // [21]
    virtual void SetDebugFunctionalityEnabled(bool enabled) = 0;                                // [22]
    virtual bool GetPopupDialogsEnabled() const = 0;                                            // [23]
    virtual void SetPopupDialogsEnabled(bool enabled) = 0;                                      // [24]
    virtual std::uint32_t GetAppState() const = 0;                                              // [25]
    virtual cIGZWin* GetMainWindow() const = 0;                                                 // [26]
    virtual void GetAppName(cIGZString& out) const = 0;                                         // [27]
    virtual void GetAppIniFileName(cIGZString& out) const = 0;                                  // [28]
    virtual void GetAppIniFilePath(cIGZString& out) const = 0;                                  // [29]
    virtual void GetAppPreferencesFileName(cIGZString& out) const = 0;                          // [30]
    virtual void GetAppPreferencesFilePath(cIGZString& out) const = 0;                          // [31]
    virtual cIGZUnknown* GetFeatureManager() const = 0;                                         // [32]
    virtual cIGZUnknown* GetCheatCodeManager() const = 0;                                       // [33]
    virtual cISC4Nation* GetNation() = 0;                                                       // [34]
    virtual cISC4Region* GetRegion() = 0;                                                       // [35]
    virtual cISC4RegionalCity* GetRegionalCity() = 0;                                           // [36]
    virtual cISC4City* GetCity() const = 0;                                                     // [37]
    virtual std::uint8_t* GetPreferences() const = 0;                                           // [38]
    virtual cSC4NewCitySpecification* GetNewCitySpecification() = 0;                            // [39]
    virtual cIGZUnknown* GetDebugConsole() const = 0;                                           // [40]
    virtual cIGZUnknown* GetGimexFactory() const = 0;                                           // [41]
    virtual cIGZUnknown* GetStringDetokenizer() const = 0;                                      // [42]
    virtual cIGZUnknown* GetWinLocationSaver() const = 0;                                       // [43]
    virtual cIGZUnknown* GetRenderProperties() const = 0;                                       // [44]
    virtual cIGZUnknown* GetGlyphTextureManager() const = 0;                                    // [45]
    virtual cIGZUnknown* GetLuaInterpreter() const = 0;                                         // [46]
    virtual cSC4TutorialRegistry* GetTutorialRegistry() = 0;                                    // [47]
    virtual bool IsRunFirstTimeAfterInstall() = 0;                                              // [48]
    virtual void GetAppDirectory(cIGZString& out) = 0;                                          // [49]
    virtual void GetCDAppDirectory(cIGZString& out) = 0;                                        // [50]
    virtual void GetDataDirectory(cIGZString& out, int language) = 0;                           // [51]
    virtual void GetCDDataDirectory(cIGZString& out, int language) = 0;                         // [52]
    virtual void GetPluginDirectory(cIGZString& out) = 0;                                       // [53]
    virtual void GetCDPluginDirectory(cIGZString& out) = 0;                                     // [54]
    virtual void GetSkuSpecificDirectory(cIGZString& out) = 0;                                  // [55]
    virtual void GetUserDataDirectory(cIGZString& out) = 0;                                     // [56]
    virtual void GetUserPluginDirectory(cIGZString& out) = 0;                                   // [57]
    virtual void GetRegionsDirectory(cIGZString& out) = 0;                                      // [58]
    virtual void GetMySimDirectory(cIGZString& out) = 0;                                        // [59]
    virtual void GetAlbumDirectory(cIGZString& out) = 0;                                        // [60]
    virtual void GetHTTPCacheDirectory(cIGZString& out) = 0;                                    // [61]
    virtual void GetTempDirectory(cIGZString& out) = 0;                                         // [62]
    virtual void GetExceptionReportsDirectory(cIGZString& out) = 0;                             // [63]
    virtual void GetTestScriptDirectory(cIGZString& out) = 0;                                   // [64]
    virtual bool AddDynamicLibraryByName(const cIGZString& name, cRZString* resolvedPath, bool required) = 0;   // [65]
    virtual bool AddDynamicLibraryByPath(const cIGZString& path, bool required) = 0;            // [66]
    virtual bool RegisterShutdownCallbackFunction(ShutdownCallback callback, void* context) = 0;   // [67]
    virtual bool UnregisterShutdownCallbackFunction(ShutdownCallback callback, void* context) = 0; // [68]
};
