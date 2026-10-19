// cIGZFileSystem — the file system service's interface (iid 0xe0203660; service id 0x2073215d; the third vtable group of
// cGZFileSystem @ 0x100aac870, slots 3..21): where the application, its data, the plugins and the install CD live.
#pragma once
#include <cstdint>

#include "gz/cIGZString.h"
#include "gz/cIGZUnknown.h"

constexpr std::uint32_t kGZIID_cIGZFileSystem = 0xe0203660;
constexpr std::uint32_t kGZSID_cGZFileSystem = 0x2073215d;

class cIGZFileSystem : public cIGZUnknown {
public:
    virtual const char* AppDrive() = 0;                                  // [3]
    virtual const char* AppDirectory() = 0;                              // [4]
    virtual const char* AppName() = 0;                                   // [5]
    virtual const char* AppExtension() = 0;                              // [6]
    virtual const char* DataDirectory() = 0;                             // [7]
    virtual const char* PlugInDirectory() = 0;                           // [8]
    virtual const char* CDAppDrive() = 0;                                // [9]
    virtual const char* CDAppDirectory() = 0;                            // [10]
    virtual const char* CDDataDirectory() = 0;                           // [11]
    virtual const char* CDPlugInDirectory() = 0;                         // [12]
    virtual bool FindCDDrive() = 0;                                      // [13]
    virtual void UseCDIniFile(bool use) = 0;                             // [14]
    virtual void AllowRecursiveCDSearch(bool allow) = 0;                 // [15]
    virtual void SetExpectedCDName(const cIGZString& name) = 0;          // [16]
    virtual void SetExpectedCDAppPath(const cIGZString& path) = 0;       // [17]
    virtual void SetDataDirectory(const cIGZString& directory) = 0;      // [18]
    virtual void SetPluginDirectory(const cIGZString& directory) = 0;    // [19]
    virtual void GetPluginDirectoryName(cIGZString& out) = 0;            // [20]
    virtual void SetPluginDirectoryName(const cIGZString& name) = 0;     // [21]
};
