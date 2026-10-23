// cSC4App: shutting down (PreFrameWorkShutdown before the framework stops its services, Shutdown after) and loading
// plug-in libraries (0x10000a5f4..0x10000ab6c, 0x10001136a..0x100011a14).
//  * AddDynamicLibraryByPath: a library whose "<name>.ini" says [Admin] Enabled=false (or 0) is skipped unless required.
//    The ini is looked up next to the library, but GetPathDirectory yields an empty directory in this port (as in the
//    shipped build), so the ini name is just "<name>.ini" relative to the working directory.
//  * AddDynamicLibraryByName: the library name is lower-cased for the extension test (".so" is appended when missing) and
//    joined to the given directory (the application directory by default).
#include "gz/RZServices.h"
#include "gz/cGZCOM.h"
#include "gz/cIGZFrameWork.h"
#include "gz/CheatCodeManager.h"
#include "gz/GZCOMDirector.h"
#include "rz/cRZFile.h"
#include "rz/cRZIniFile.h"
#include "sc4/App.h"

namespace {
void ReleaseAndClear(cIGZUnknown*& object) {
    if (object) object->Release();
    object = nullptr;
}
}  // namespace

bool cSC4App::PreFrameWorkShutdown() {
    if (pluginWatcher_.IsStarted()) pluginWatcher_.Stop();
    pluginWatcher_.Shutdown();
    if (dataWatcher_.IsStarted()) dataWatcher_.Stop();
    dataWatcher_.Shutdown();
    CloseCity();
    ShutdownWinCtrlMgr();
    SavePreferences();
    for (cIGZUnknown** support : {&field788_, &winCommandSupport_, &toolCommandSupport_, &miscCommandSupport_}) {
        if (!*support) continue;
        if (startupHooks_.shutdownManager) startupHooks_.shutdownManager(support == &field788_ ? "cRZNetCommandGenerator" : "CommandSystemSupport", *support);
        ReleaseAndClear(*support);
    }
    return true;
}

bool cSC4App::Shutdown() {
    cIGZFrameWork* framework = framework_ ? framework_ : RZGetFramework();
    auto stop = [&](const char* name, cIGZUnknown*& object) {
        if (!object) return;
        if (startupHooks_.shutdownManager) startupHooks_.shutdownManager(name, object);
        ReleaseAndClear(object);
    };
    PostSC4AppMessage(0x28e9397c, false);
    stop("cSC4ToolTipManager", toolTipManager_);
    if (framework) framework->RemoveFromOnIdle(static_cast<cIGZSystemService*>(this));
    WindowShutdown();
    stop("cSC4MissingPluginDataReporter", missingPluginReporter_);
    if (nation_) {
        cIGZUnknown* nation = reinterpret_cast<cIGZUnknown*>(nation_);
        if (startupHooks_.shutdownManager) startupHooks_.shutdownManager("cSC4Nation", nation);
        nation->Release();
        nation_ = nullptr;
    }
    regionalCity_ = nullptr;
    if (startupHooks_.shutdownManager && tutorialRegistry_) startupHooks_.shutdownManager("cSC4TutorialRegistry", reinterpret_cast<cIGZUnknown*>(tutorialRegistry_));
    stop("cSC4GlyphTextureManager", glyphTextureManager_);
    if (cheatCodeManager_) {
        auto* cheats = static_cast<cGZCheatCodeManager*>(reinterpret_cast<cRZSystemService*>(cheatCodeManager_));
        cheats->RemoveNotification(static_cast<cIGZMessageTarget*>(this), 0);
        cheats->UnregisterCheatCode(0x2ab5c431);
        cheats->UnregisterCheatCode(0x2b60e45b);
        cheats->UnregisterCheatCode(0x8b6d4f8d);
        cheats->Release();
        cheatCodeManager_ = nullptr;
    }
    for (auto it = shutdownCallbacks_.begin(); it != shutdownCallbacks_.end();) {
        auto current = it++;
        current->callback(current->context);
    }
    shutdownCallbacks_.clear();
    stop("cSC4DebugConsole", debugConsole_);
    ReleaseAndClear(gimexFactory_);
    ReleaseAndClear(stringDetokenizer_);
    stop("cSCLua", luaInterpreter_);
    stop("cSLWinLocationSaver", winLocationSaver_);
    if (cIGZMessageServer2* messages = SL::spMS2) {
        for (std::uint32_t type : {0xfa0c6390u, 0x7a104750u, 0xba44eb51u, 0x49a780b2u, 0xea65224eu, 0xcbb5bb45u, 0x8bb5bb46u, 0xea8ae29au, 0x26d31ec2u})
            messages->RemoveNotification(static_cast<cIGZMessageTarget2*>(this), type);
    }
    if (cIGZMessageServer* messages = RZ::MS()) messages->RemoveNotification(static_cast<cIGZMessageTarget*>(this), 0xc7aa61c6);
    if (cIGZPersistResourceManager* manager = SL::spRM) {
        const std::uint32_t count = manager->GetSegmentCount();
        for (std::uint32_t i = 0; i < count; ++i) {
            cIGZPersistDBSegment* segment = manager->GetSegmentByIndex(i);
            segment->Close();
            segment->Shutdown();
        }
    }
    stop("cSC4FeatureManager", featureManager_);
    PostSC4AppMessage(0x28e9397d, false);
    if (renderProperties_) {
        static_cast<cIGZUnknown*>(renderProperties_)->Release();
        renderProperties_ = nullptr;
    }
    videoOptions_ = nullptr;
    return true;
}

bool cSC4App::AddDynamicLibraryByPath(const cIGZString& path, bool required) {
    cIGZCOM* com = GZCOM();
    if (!com || !cRZFile::FileExists(path)) return false;
    if (!required) {
        cRZString name;
        cRZFile::GetPathName(path, name, false);
        name.AppendChars(".ini", 4);
        cRZString directory;
        cRZFile::GetPathDirectory(path, directory);
        cRZString iniPath((std::string(directory.ToChar()) + name.ToChar()).c_str());
        if (cRZFile::FileExists(iniPath)) {
            cRZIniFile ini(iniPath);
            cRZString value;
            if (ini.ReadEntry(cRZString("Admin"), cRZString("Enabled"), value)) {
                if (value.IsEqualToChars("false", 5, false) || value.IsEqualToChars("0", 1, true)) return false;
            }
        }
    }
    return static_cast<cIGZCOMLibraryManager*>(com)->AddLibrary(path);
}

bool cSC4App::AddDynamicLibraryByName(const cIGZString& name, cRZString* directory, bool required) {
    cRZString base;
    if (directory) base.AssignFromIGZString(*directory);
    else RZ::ADir(base);
    cRZString lowered(name.ToChar());
    lowered.MakeLower();
    const std::string text = lowered.ToChar();
    const bool hasExtension = text.size() >= 3 && text.compare(text.size() - 3, 3, ".so") == 0;
    cRZString file(name.ToChar());
    if (!hasExtension) file.AppendChars(".so", 3);
    cRZString full((std::string(base.ToChar()) + file.ToChar()).c_str());
    return AddDynamicLibraryByPath(full, required);
}

// The original is an empty function: the control manager's resources die with the window system.
void cSC4App::ShutdownWinCtrlMgr() {}

// The "Exception Report Uploader.exe" the crash handler starts: looked for in the user plugin directory, then the plugin
// directory, then the application directory; the first that exists is started and waited for.
// PARTIAL: the program is started through RZSpawnWait (0x1000274df, a spawnv wrapper) which is not recovered; the host
// hook starts it.
void cSC4App::LaunchExceptionReportUploader() {
    static constexpr char kUploader[] = "Exception Report Uploader.exe";
    cRZString path;
    GetUserPluginDirectory(path);
    path.AppendChars(kUploader, sizeof(kUploader) - 1);
    if (!cRZFile::FileExists(path)) {
        GetPluginDirectory(path);
        path.AppendChars(kUploader, sizeof(kUploader) - 1);
        if (!cRZFile::FileExists(path)) {
            GetAppDirectory(path);
            path.AppendChars(kUploader, sizeof(kUploader) - 1);
            if (!cRZFile::FileExists(path)) return;
        }
    }
    if (startupHooks_.spawnWait) startupHooks_.spawnWait(path.ToChar());
}
