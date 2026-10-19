#include "gz/FileSystem.h"

#include <cstring>

#include "rz/cRZDirectory.h"
#include "rz/cRZFile.h"
#include "rz/cRZIniFile.h"

namespace {
IFileSystemHost* g_host = nullptr;
}

IFileSystemHost* FileSystemHost() { return g_host; }
void SetFileSystemHost(IFileSystemHost* host) { g_host = host; }

cGZFileSystem::cGZFileSystem() : cRZSystemService(kGZSID_cGZFileSystem, 2000000), pluginDirectoryName_("Plugins") {}

cGZFileSystem::~cGZFileSystem() = default;

bool cGZFileSystem::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZFileSystem) {
        *out = static_cast<cIGZFileSystem*>(this);
        AddRef();
        return true;
    }
    if (iid == kGZCLSID_cGZFileSystem) {
        *out = static_cast<cRZSystemService*>(this);
        AddRef();
        return true;
    }
    return cRZSystemService::QueryInterface(iid, out);
}

void cGZFileSystem::GetAndSplitApplicationPath() {
    const std::string path = g_host ? g_host->CurrentAppPath() : std::string();
    char drive[256] = "", directory[256] = "", name[256] = "", extension[256] = "";
    Splitpath(path.c_str(), drive, directory, name, extension);
    appDrive_ = drive;
    appDirectory_ = directory;
    appName_ = name;
    appExtension_ = extension;
    appResolved_ = true;
}

const char* cGZFileSystem::AppDrive() {
    if (!appResolved_) { GetAndSplitApplicationPath(); appResolved_ = true; }
    return appDrive_.c_str();
}
const char* cGZFileSystem::AppDirectory() {
    if (!appResolved_) { GetAndSplitApplicationPath(); appResolved_ = true; }
    return appDirectory_.c_str();
}
const char* cGZFileSystem::AppName() {
    if (!appResolved_) { GetAndSplitApplicationPath(); appResolved_ = true; }
    return appName_.c_str();
}
const char* cGZFileSystem::AppExtension() {
    if (!appResolved_) { GetAndSplitApplicationPath(); appResolved_ = true; }
    return appExtension_.c_str();
}

const char* cGZFileSystem::DataDirectory() {
    if (!dataResolved_) {
        dataDirectory_ = std::string(AppDirectory()) + "SimCity 4 Data/";
        std::string bundleData;
        if (g_host && g_host->BundleGameDataPath(bundleData)) dataDirectory_ = bundleData + "/";
        dataResolved_ = true;
    }
    return dataDirectory_.c_str();
}

const char* cGZFileSystem::PlugInDirectory() {
    if (!pluginResolved_) {
        pluginDirectory_ = std::string(DataDirectory()) + "Plugins/";
        pluginResolved_ = true;
    }
    return pluginDirectory_.c_str();
}

const char* cGZFileSystem::CDAppDrive() {
    if (!cdFound_) {
        if (!appResolved_) GetAndSplitApplicationPath();
        if (!dataResolved_) DataDirectory();
        if (!pluginResolved_) PlugInDirectory();
        FindCDDrive();
    }
    return cdDrive_.ToChar();
}
const char* cGZFileSystem::CDAppDirectory() {
    if (!cdFound_) CDAppDrive();
    return cdAppDirectory_.ToChar();
}
const char* cGZFileSystem::CDDataDirectory() {
    if (!cdFound_) CDAppDrive();
    return cdDataDirectory_.ToChar();
}
const char* cGZFileSystem::CDPlugInDirectory() {
    if (!cdFound_) CDAppDrive();
    return cdPlugInDirectory_.ToChar();
}

bool cGZFileSystem::FindCDDrive() {
    const std::string mount = g_host ? g_host->FindCDROM("SimCity 4 Rush Hour") : std::string();
    if (mount.empty()) return false;
    cdDrive_ = cRZString(mount.c_str());
    cdFound_ = true;
    return true;
}

bool cGZFileSystem::ValidateCDPath() {
    if (cdDrive_.Strlen() == 0) return false;
    if (useCDIni_) {
        const cRZString iniPath((std::string(cdDrive_.ToChar()) + "Maxis.ini").c_str());
        if (!cRZFile::FileExists(iniPath)) return false;
        const std::string appObject = g_host ? g_host->AppObjectName() : std::string();
        if (appObject.empty()) return false;
        cRZIniFile ini(iniPath);
        cRZString section(appObject.c_str()), value;
        if (ini.ReadEntry(section, cRZString("App"), value)) cdAppDirectory_ = cRZString((std::string(cdDrive_.ToChar()) + value.ToChar()).c_str());
        if (ini.ReadEntry(section, cRZString("Data"), value)) cdDataDirectory_ = cRZString((std::string(cdDrive_.ToChar()) + value.ToChar()).c_str());
        if (ini.ReadEntry(section, cRZString("PlugIn"), value)) cdPlugInDirectory_ = cRZString((std::string(cdDrive_.ToChar()) + value.ToChar()).c_str());
        return true;
    }
    if (!allowRecursiveCDSearch_) return false;
    cRZDirectory directory(cdDrive_, false, false, false);
    const bool haveName = expectedCDName_.Strlen() != 0;
    const bool havePath = expectedCDAppPath_.Strlen() != 0;
    if (!haveName && !havePath) {                      // search the disc for the application itself
        const cRZString target((std::string(AppName()) + AppExtension()).c_str());
        std::list<cRZString> found;
        directory.SetNewDirectoryEntryFilter(target, false);
        const bool read = directory.ReadDirectoryFileEntriesIntoStringListRecursive(cdDrive_, found, false);
        if (!found.empty() && read) {
            cRZString appDirectory;
            cRZFile::GetPathDirectory(found.front(), appDirectory);
            cdAppDirectory_ = appDirectory;
            cdDataDirectory_ = cRZString(DataDirectory());
            cdPlugInDirectory_ = cRZString(PlugInDirectory());
            return true;
        }
        return false;
    }
    if (haveName) {                                    // the volume must carry the expected name
        cRZString volume;
        if (!cRZFile::GetVolumeName(cdDrive_, volume) || strcasecmp(volume.ToChar(), expectedCDName_.ToChar()) != 0) return false;
    }
    if (!havePath) return true;
    const cRZString appPath((std::string(cdDrive_.ToChar()) + expectedCDAppPath_.ToChar()).c_str());
    if (!cRZFile::FileExists(appPath)) return false;
    cRZString appDirectory;
    cRZFile::GetPathDirectory(appPath, appDirectory);
    cdAppDirectory_ = appDirectory;
    cdDataDirectory_ = appDirectory;
    cdPlugInDirectory_ = appDirectory;
    return true;
}

void cGZFileSystem::SetDataDirectory(const cIGZString& directory) {
    dataDirectory_.assign(directory.Data(), directory.Strlen());
    if (cRZDirectory::DoesDirectoryExist(cRZString(dataDirectory_.c_str()))) dataResolved_ = true;
}

void cGZFileSystem::SetPluginDirectory(const cIGZString& directory) {
    pluginDirectory_.assign(directory.Data(), directory.Strlen());
    if (cRZDirectory::DoesDirectoryExist(cRZString(pluginDirectory_.c_str()))) pluginResolved_ = true;
}

void cGZFileSystem::GetPluginDirectoryName(cIGZString& out) {
    out.AssignFromChars(pluginDirectoryName_.ToChar(), pluginDirectoryName_.Strlen());
}
