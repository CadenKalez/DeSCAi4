// cSC4App::PostFrameWorkInit and FindTestScript (0x10000997e, 0x10000a2be): what the application does once every
// system service is up — the scripted-UI command systems, the plug-in folder watchers, the alpha blitter and the
// -LuaScript:"file" test scripts of the command line.
//  * FindTestScript tries the path as given, then the test script directory plus the path, then (quirk) the test script
//    directory plus the *parent* part SplitDirectory answers second.
//  * -LuaScript: values may use single quotes; the script name is the first word (or the quoted part), the rest is
//    handed to the script. Each handled switch is erased from the working copy of the command line.
#include "gz/GZCOMDirector.h"
#include "gz/cIGZFrameWork.h"
#include "gz/GZFramework.h"
#include "rz/cRZCmdLine.h"
#include "rz/cRZDirectory.h"
#include "rz/cRZFile.h"
#include "sc4/App.h"

namespace {
constexpr const char* kNetSwitch = "NetCommandGenerator";
constexpr const char* kLuaSwitch = "LuaScript";

void StartWatcher(FileChangeNotification& watcher, const cRZString& directory, cSC4App* target) {
    if (directory.Strlen() == 0) return;
    if (!cRZDirectory::DoesDirectoryExist(directory)) cRZDirectory::CreateDirectoryA(directory);
    if (!cRZDirectory::DoesDirectoryExist(directory)) return;
    watcher.SetWatchedDirectory(directory);
    watcher.SetChangeTypeFlags(0x13);
    watcher.SetOptionFlags(1);
    watcher.SetNotificationTarget(static_cast<cIGZMessageTarget*>(target));
    watcher.Init();
    watcher.Start();
}
}  // namespace

bool cSC4App::FindTestScript(cRZString& path) {
    if (cRZFile::FileExists(path)) return true;
    const cRZString original(path.ToChar());
    cRZString directory;
    GetTestScriptDirectory(directory);
    path.AssignFromCString((std::string(directory.ToChar()) + original.ToChar()).c_str());
    if (cRZFile::FileExists(path)) return true;
    cRZString parts[2];
    int count = 2;
    SplitDirectory(original, parts, count);
    path.AssignFromCString((std::string(directory.ToChar()) + parts[1].ToChar()).c_str());
    return cRZFile::FileExists(path);
}

bool cSC4App::PostFrameWorkInit() {
    cIGZFrameWork* framework = framework_ ? framework_ : RZGetFramework();
    auto support = [&](cIGZUnknown*& slot, const char* kind, const char* className) {
        if (slot) return;
        if (startupHooks_.createCommandSupport) slot = startupHooks_.createCommandSupport(kind);
        if (slot) {
            slot->AddRef();
            if (startupHooks_.initializeManager) startupHooks_.initializeManager(className, slot);
        }
    };
    support(winCommandSupport_, "win", "cGZWinCommandSystemSupport");
    support(toolCommandSupport_, "tool", "cGZToolCommandSystemSupport");
    support(miscCommandSupport_, "misc", "cGZMiscCommandSystemSupport");

    // -NetCommandGenerator:on|enabled
    bool netGenerator = false;
    if (framework && framework->CommandLine()) {
        cRZString value;
        if (framework->CommandLine()->IsSwitchPresent(cRZString(kNetSwitch), value, true)) {
            value.MakeLower();
            netGenerator = value.IsEqualToChars("on", 2, true) || value.IsEqualToChars("enabled", 7, true);
        }
    }
    if (netGenerator && !field788_ && startupHooks_.createNetCommandGenerator) {
        field788_ = startupHooks_.createNetCommandGenerator();
        if (field788_) field788_->AddRef();
    }
    if (framework && startupHooks_.graphicsWantsQuietLog && startupHooks_.graphicsWantsQuietLog() && framework->GetDebugLevel() == 2)
        framework->SetDebugLevel(1);

    cRZString directory;
    GetPluginDirectory(directory);
    StartWatcher(pluginWatcher_, directory, this);
    directory = cRZString();
    GetUserPluginDirectory(directory);
    StartWatcher(dataWatcher_, directory, this);

    if (startupHooks_.initializeAlphaSystem) startupHooks_.initializeAlphaSystem();

    // -LuaScript:"file" arguments
    if (framework && framework->CommandLine()) {
        cRZCmdLine line(*framework->CommandLine());
        cRZString value;
        while (line.IsSwitchPresent(cRZString(kLuaSwitch), value, true) && value.Strlen() != 0) {
            std::string text = value.ToChar();
            for (char& c : text) if (c == '\'') c = '"';
            auto trim = [](std::string& s) {
                const auto first = s.find_first_not_of(" \t");
                s = first == std::string::npos ? std::string() : s.substr(first, s.find_last_not_of(" \t") - first + 1);
            };
            std::string script;
            if (text[0] == '"' && text.size() > 1) {
                const std::size_t close = text.find('"', 1);
                if (close != 0 && close < text.size()) {
                    script = text.substr(1, close - 1);
                    text.erase(0, close + 1);
                    trim(text);
                }
            }
            if (script.empty()) {
                const std::size_t space = text.find(' ');
                script = text.substr(0, space);
                text.erase(0, space == std::string::npos ? text.size() : space);
                trim(text);
            }
            cRZString scriptPath(script.c_str());
            FindTestScript(scriptPath);
            if (cRZFile::FileExists(scriptPath) && startupHooks_.runTestScript) startupHooks_.runTestScript(scriptPath.ToChar(), text);
            for (int i = 0; i < line.argc(); ++i) {
                if (line[i].FindChars("-LuaScript:", 0, false) == 0) { line.EraseArgument(i); break; }
            }
        }
    }
    return true;
}
