#include "sc4/App.h"

#include "rz/LanguageManager.h"
#include "rz/cRZFile.h"
#include "rz/cRZMessage2Standard.h"
#include "rz/cRZSysServPtr.h"

namespace {
using LanguageManagerPtr = cRZSysServPtr<cIGZLanguageManager, kGZSID_cRZLanguageManager, kGZIID_cIGZLanguageManager>;

// "<directory>/": the language folder of the current language (language 0) followed by a slash
void AppendLanguageFolder(cIGZString& out) {
    cRZString folder("English");
    LanguageManagerPtr languages;
    if (languages) languages->GetLanguageDirectoryName(folder, 0);
    out.AppendString(folder);
    out.AppendChars("/", 1);
}

// The directories that are derived on first use: fill `target` from the user data directory plus "<name>/".
template <class Fill>
void DeriveDirectory(cRZString& target, Fill&& fillBase, const char* name) {
    if (target.Strlen() == 0) {
        fillBase(target);
        target.AppendChars(name, static_cast<std::uint32_t>(std::strlen(name)));
        target.AppendChars("/", 1);
    }
}
}  // namespace

cSC4App::cSC4App(const char* moduleName, IGZAppFramework* framework) : cGZApp(moduleName, framework) {
    timer3d0_.Start();
    timer3f0_.Start();
    timer410_.Start();
    timer428_.Start();
    timer440_.Start();
    // (the original also calls SL::OnAppCreate(cISC4App*) here and builds the tutorial registry)
}

cSC4App::~cSC4App() {
    instanceMutex_.Unlock();
    for (cIGZUnknown* object : {luaInterpreter_, field788_, miscCommandSupport_, toolCommandSupport_, winCommandSupport_,
                                 renderProperties_, field668_}) {
        if (object) object->Release();
    }
}

bool cSC4App::QueryInterface(std::uint32_t iid, void** out) {
    switch (iid) {
        case 0xca263481:
        case 0xe98b2f57: *out = static_cast<cIGZMessageTarget*>(this); break;
        case 0x452294aa: *out = static_cast<cIGZMessageTarget2*>(this); break;
        case 0xc6ae7085: *out = static_cast<cIGZWinMessageFilter*>(this); break;
        case kGZIID_cISC4App: *out = static_cast<cISC4App*>(this); break;
        default: return cGZApp::QueryInterface(iid, out);
    }
    AddRef();
    return true;
}

std::uint32_t cSC4App::AddRef() { return cGZApp::AddRef(); }
std::uint32_t cSC4App::Release() { return cRZUnknown::Release(); }

void cSC4App::SetDirectories(const char* appDirectory, const char* dataDirectory, const char* userDataDirectory) {
    appDirectory_ = appDirectory;
    dataDirectory_ = dataDirectory;
    userDataDirectory_ = userDataDirectory;
}

void cSC4App::GetAppName(cIGZString& out) const { out.AssignFromCString(appName_.ToChar()); }

void cSC4App::GetDataDirectory(cIGZString& out, int language) {
    out.AssignFromCString(dataDirectory_.ToChar());
    if (language >= 0) AppendLanguageFolder(out);   // quirk: always the current language, whatever the argument
}

void cSC4App::GetCDDataDirectory(cIGZString& out, int language) {
    out.AssignFromCString(cdDataDirectory_.ToChar());
    if (language >= 0) AppendLanguageFolder(out);
}

void cSC4App::GetUserPluginDirectory(cIGZString& out) {
    DeriveDirectory(userPluginDirectory_, [this](cRZString& s) { GetUserDataDirectory(s); }, "Plugins");
    out.AssignFromCString(userPluginDirectory_.ToChar());
}

void cSC4App::GetRegionsDirectory(cIGZString& out) {
    DeriveDirectory(regionsDirectory_, [this](cRZString& s) { GetUserDataDirectory(s); }, "Regions");
    out.AssignFromCString(regionsDirectory_.ToChar());
}

void cSC4App::GetMySimDirectory(cIGZString& out) {
    DeriveDirectory(mySimDirectory_, [this](cRZString& s) { GetUserDataDirectory(s); }, "MySim");
    out.AssignFromCString(mySimDirectory_.ToChar());
}

void cSC4App::GetAlbumDirectory(cIGZString& out) {
    DeriveDirectory(albumDirectory_, [this](cRZString& s) { GetUserDataDirectory(s); }, "Albums");
    out.AssignFromCString(albumDirectory_.ToChar());
}

void cSC4App::GetHTTPCacheDirectory(cIGZString& out) {
    DeriveDirectory(httpCacheDirectory_, [this](cRZString& s) { GetUserDataDirectory(s); }, "HTTPCache");
    out.AssignFromCString(httpCacheDirectory_.ToChar());
}

void cSC4App::GetTempDirectory(cIGZString& out) {
    DeriveDirectory(tempDirectory_, [this](cRZString& s) { GetUserDataDirectory(s); }, "Temp");
    out.AssignFromCString(tempDirectory_.ToChar());
}

void cSC4App::GetExceptionReportsDirectory(cIGZString& out) {
    DeriveDirectory(exceptionReportsDirectory_, [this](cRZString& s) { GetUserDataDirectory(s); }, "Exception Reports");
    out.AssignFromCString(exceptionReportsDirectory_.ToChar());
}

// "<data directory>/Scripts/TestScripts/" (the data directory without a language folder)
void cSC4App::GetTestScriptDirectory(cIGZString& out) {
    if (testScriptDirectory_.Strlen() == 0) {
        GetDataDirectory(testScriptDirectory_, -1);
        testScriptDirectory_.AppendChars("Scripts", 7);
        testScriptDirectory_.AppendChars("/", 1);
        testScriptDirectory_.AppendChars("TestScripts", 11);
        testScriptDirectory_.AppendChars("/", 1);
    }
    out.AssignFromCString(testScriptDirectory_.ToChar());
}

void cSC4App::GetAppIniFileName(cIGZString& out) const {
    if (appName_.Strlen() != 0) {
        out.AssignFromCString(appName_.ToChar());
        out.AppendChars(".ini", 4);
    } else {
        out.AssignFromCString("SimCity 4.ini");
    }
}

// The .ini next to the executable when it exists, else the one in the user data directory.
void cSC4App::GetAppIniFilePath(cIGZString& out) const {
    cRZString name;
    GetAppIniFileName(name);
    cRZString beside(appDirectory_);
    beside.AppendString(name);
    if (cRZFile::FileExists(beside)) {
        out.Copy(beside);
    } else {
        out.AssignFromChars(userDataDirectory_.ToChar(), userDataDirectory_.Strlen());
        out.AppendChars(name.ToChar(), name.Strlen());
    }
}

void cSC4App::GetAppPreferencesFileName(cIGZString& out) const {
    if (appName_.Strlen() != 0) {
        out.AssignFromCString(appName_.ToChar());
        out.AppendChars(".cfg", 4);
    } else {
        out.AssignFromCString("SimCity 4.cfg");
    }
}

void cSC4App::GetAppPreferencesFilePath(cIGZString& out) const {
    cRZString name;
    GetAppPreferencesFileName(name);
    out.AssignFromChars(userDataDirectory_.ToChar(), userDataDirectory_.Strlen());
    out.AppendChars(name.ToChar(), name.Strlen());
}

bool cSC4App::RegisterShutdownCallbackFunction(ShutdownCallback callback, void* context) {
    shutdownCallbacks_.push_back({callback, context});
    return true;
}

bool cSC4App::UnregisterShutdownCallbackFunction(ShutdownCallback callback, void* context) {
    for (auto it = shutdownCallbacks_.begin(); it != shutdownCallbacks_.end(); ++it) {
        if (it->callback == callback && it->context == context) {
            shutdownCallbacks_.erase(it);
            return true;
        }
    }
    return false;
}

// option number -> field of SC4VideoPreferences (the same map serves Apply and GetAuto)
namespace {
struct VideoOptionField { int option; int byteOffset; };
constexpr VideoOptionField kVideoOptionFields[] = {
    {0, 1}, {1, 2}, {2, 3}, {3, 4}, {6, 7}, {4, 5}, {5, 6}, {8, 9}, {7, 8}, {9, 10}, {10, 0xc}, {0xf, 0x1c},
};
}  // namespace

void cSC4App::ApplyVideoPreferences(const SC4VideoPreferences& preferences) {
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(&preferences);
    for (const VideoOptionField& field : kVideoOptionFields) videoOptions_->SetOption(field.option, bytes[field.byteOffset]);
    // tell everyone: message 0xc9f775bb through the message server 2
    void* server = nullptr;
    if (cRZServiceResolver resolve = GetRZServiceResolver()) {
        if (resolve(kGZSID_MessageServer2, 0x652294c7, &server) && server) {
            auto* messages = static_cast<cIGZMessageServer2*>(server);
            cRZAutoRefCount<cRZMessage2Standard> message(new cRZMessage2Standard());
            message->SetType(0xc9f775bb);
            messages->MessagePost(message.get(), false);
            messages->Release();
        }
    }
}

void cSC4App::GetAutoVideoPreferences(SC4VideoPreferences* preferences) {
    auto* bytes = reinterpret_cast<std::uint8_t*>(preferences);
    for (const VideoOptionField& field : kVideoOptionFields) bytes[field.byteOffset] = static_cast<std::uint8_t>(videoOptions_->GetOption(field.option));
    bytes[0xb] = 1;
    int width = 0, height = 0;
    if (display_) display_->GetNthNativeResolution(videoOptions_->GetOption(0xb), width, height);
    preferences->screenWidth = width;
    preferences->screenHeight = height;
    switch (videoOptions_->GetOption(0xc)) {          // colour depth option: 0 = 16 bit, 1 = 32 bit, otherwise untouched
        case 0: preferences->colorDepth = 16; break;
        case 1: preferences->colorDepth = 32; break;
        default: break;
    }
}

cISC4Region* cSC4App::GetRegion() { return nation_ && regionOf_ ? regionOf_(nation_) : nullptr; }

cISC4RegionalCity* cSC4App::GetRegionalCity() {
    if (!regionalCity_) return nullptr;
    return regionalCity_->get();   // (the original AddRefs and Releases once, a no-op)
}
