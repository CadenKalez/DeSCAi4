// cRZVersion (14 functions, 0x100041d2e..0x100042622): what the game can ask about the machine and the running bundle.
// The Mac port answers from the bundle's Info.plist (name, short version), sysctl / Mach host calls (CPU clock, memory)
// and the Cocoa user / computer name; the system version string is the hard-coded lie "Windows NT 5.1".
// All platform queries go through IRZVersionPlatform so tests and other platforms can supply their own answers; the
// default platform uses POSIX / Mach calls (no CoreFoundation bundle on non-bundle executables).
#pragma once
#include <cstdint>
#include <string>

#include "rz/cRZString.h"

class IRZVersionPlatform {
public:
    virtual ~IRZVersionPlatform() = default;
    virtual bool BundleName(std::string& out) = 0;            // CFBundleName of the main bundle
    virtual bool BundleShortVersion(std::string& out) = 0;    // CFBundleShortVersionString
    virtual bool AppPath(std::string& out) = 0;               // RZGetCurrentAppPath
    virtual std::uint64_t CPUFrequencyMax() = 0;              // hw.cpufrequency_max in Hz (0 when unknown)
    // Mach host queries in whole megabytes: physical memory, and free + inactive pages (each converted separately, as the
    // original does); false when the host call fails.
    virtual bool MemoryMB(std::uint64_t& physical, std::uint32_t& available) = 0;
    virtual bool UserName(std::string& out) = 0;
    virtual bool ComputerName(std::string& out) = 0;
};

IRZVersionPlatform* GetRZVersionPlatform();
void SetRZVersionPlatform(IRZVersionPlatform* platform);   // nullptr restores the default

class cRZVersion {
public:
    cRZVersion() = default;
    virtual ~cRZVersion() = default;        // [0] [1] @ 0x100041d62 / 0x100041d82

    bool GetModulePath(cRZString& out);                       // @ 0x100041db2
    bool GetModuleName(cRZString& out);                       // @ 0x100041dc6 (empty + false when the bundle has none)
    bool GetModuleVersion(cRZString& out);                    // @ 0x100041f62
    bool GetSystemVersion(cRZString& out);                    // @ 0x100042122
    std::uint32_t GetModuleVersion(std::uint32_t module);     // @ 0x100042142 always 0xffffffff
    std::uint32_t GetCPUSpeed();                              // @ 0x10004214e in MHz
    bool GetCPUName(cRZString&) { return false; }             // @ 0x10004219c
    std::uint32_t GetCPUFlags() { return 0; }                 // @ 0x1000421a4
    // Total and available memory in MB, each rounded up to a multiple of 4; 0x8000000 / 0x1000000 and false when the host fails.
    bool GetSystemMemory(std::uint32_t& totalMB, std::uint32_t& availableMB);   // @ 0x1000421ac
    // Falls back to `fallback` (or "?") when the name is unknown.
    bool GetUserNameA(cRZString& out, const cRZString* fallback);   // @ 0x10004227a
    bool GetHostName(cRZString& out, const cRZString* fallback);    // @ 0x10004244e
};
