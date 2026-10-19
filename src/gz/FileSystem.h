// cGZFileSystem — the file system service (31 + thunks, 0x1003f8c10..0x1003fae54): where the application, its data, its
// plugins and (formerly) the install CD live. Service id 0x2073215d, priority 2,000,000, class iid 0x287fb697,
// interface cIGZFileSystem (iid 0xe0203660) as the third vtable group (+0x28).
// Paths are worked out lazily on first use and cached:
//   application drive/directory/name/extension: the bundle path split with Splitpath;
//   data directory: <app directory>"SimCity 4 Data/", replaced by <bundle>/Contents/GameData/ on a bundle;
//   plugin directory: <data directory>"Plugins/" (the folder name is "Plugins" by default and can be changed).
// The CD members look for a volume called "SimCity 4 Rush Hour" and validate it against the expected name/app path or a
// Maxis.ini on the disc; none of that matters to the Steam build but it is kept.
// Quirks kept: SetDataDirectory / SetPluginDirectory store the text as given (no separator is added) and only mark the
// path as resolved when the directory exists.
#pragma once
#include <cstdint>
#include <string>

#include "gz/cIGZFileSystem.h"
#include "rz/cRZString.h"
#include "rz/cRZSystemService.h"

class IFileSystemHost {
public:
    virtual ~IFileSystemHost() = default;
    virtual std::string CurrentAppPath() = 0;                          // RZGetCurrentAppPath: the executable's path
    virtual bool BundleGameDataPath(std::string& path) = 0;            // <main bundle>/Contents/GameData (no trailing '/')
    virtual std::string FindCDROM(const std::string& volumeName) = 0;  // MacTools::FindCDROM: the mount point, "" if none
    virtual std::string AppObjectName() = 0;                           // the application's name (ini section on the CD)
};
IFileSystemHost* FileSystemHost();
void SetFileSystemHost(IFileSystemHost* host);

constexpr std::uint32_t kGZCLSID_cGZFileSystem = 0x287fb697;

class cGZFileSystem : public cRZSystemService, public cIGZFileSystem {
public:
    cGZFileSystem();
    ~cGZFileSystem() override;

    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return cRZSystemService::AddRef(); }
    std::uint32_t Release() override { return cRZSystemService::Release(); }
    bool Init() override { return true; }
    bool Shutdown() override { return true; }

    const char* AppDrive() override;
    const char* AppDirectory() override;
    const char* AppName() override;
    const char* AppExtension() override;
    const char* DataDirectory() override;
    const char* PlugInDirectory() override;
    const char* CDAppDrive() override;
    const char* CDAppDirectory() override;
    const char* CDDataDirectory() override;
    const char* CDPlugInDirectory() override;
    bool FindCDDrive() override;
    void UseCDIniFile(bool use) override { useCDIni_ = use; }
    void AllowRecursiveCDSearch(bool allow) override { allowRecursiveCDSearch_ = allow; }
    void SetExpectedCDName(const cIGZString& name) override { expectedCDName_.AssignFromIGZString(name); }
    void SetExpectedCDAppPath(const cIGZString& path) override { expectedCDAppPath_.AssignFromIGZString(path); }
    void SetDataDirectory(const cIGZString& directory) override;
    void SetPluginDirectory(const cIGZString& directory) override;
    void GetPluginDirectoryName(cIGZString& out) override;
    void SetPluginDirectoryName(const cIGZString& name) override { pluginDirectoryName_.AssignFromIGZString(name); }
    virtual void GetAndSplitApplicationPath();
    virtual bool ValidateCDPath();

private:
    bool dataResolved_ = false;           // +0x30
    bool pluginResolved_ = false;         // +0x31
    bool appResolved_ = false;            // +0x32
    std::string appDrive_, appDirectory_, appName_, appExtension_;       // +0x40 +0x68 +0x90 +0xb8
    std::string dataDirectory_, pluginDirectory_;                        // +0xe0, +0x108
    cRZString pluginDirectoryName_;       // +0x128 ("Plugins")
    bool cdFound_ = false;                // +0x150
    cRZString cdDrive_;                   // +0x158
    cRZString cdAppDirectory_;            // +0x180
    cRZString cdDataDirectory_;           // +0x1a8
    cRZString cdPlugInDirectory_;         // +0x1d0
    cRZString expectedCDName_;            // +0x1f8
    cRZString expectedCDAppPath_;         // +0x220
    bool useCDIni_ = false;               // +0x248
    bool allowRecursiveCDSearch_ = true;  // +0x249
};
