// cGZCOM — the framework's COM registry (25 functions, 0x10003b75a..0x10003c4ee, service id 0xa3cd8db3 priority
// 10,000,000, iid 0x4241330d). It keeps the libraries (ordered by path) and a class table clsid -> (version,
// library); GetClassObject finds the library for a class (loading it on first use) and asks its director for the
// object, falling back to the process director (and, for class 1000, the framework itself). It also hands out
// the process-unique 32-bit GUIDs: the top three bits come from a counter within one second, the rest from the
// seconds since 2 Feb 1997 (GetGuidGenSeconds), so a request waits until the next second once a batch used up
// its 8 ids. Layout: +8 / +0x10 recursive lock, +0x20 the system service, +0x41 initialised, +0x48 class table,
// +0x60 libraries, +0x78 library being registered, +0x80 framework, +0x88 director, +0x90 random, +0xa8 next
// GUID second.
// On this platform cGZCOMLibrary::Load always fails (directors are linked in), so AddLibrary reports failure for
// a new path - kept.
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "gz/COMLibrary.h"
#include "rz/cRZCOMDllDirector.h"
#include "rz/cRZRandom.h"
#include "rz/cRZSystemService.h"

constexpr std::uint32_t kGZSID_cGZCOM = 0xa3cd8db3;
constexpr std::int32_t kGZCOMPriority = 10000000;
constexpr std::uint32_t kGZIID_cIGZCOMExtended = 0x4241330d;
constexpr std::uint32_t kGZCOMFrameworkClass = 1000;

// RZGetFramework() / RZGetCOMDllDirector() / RZGetCurrentAppPath()
class IGZCOMHost {
public:
    virtual ~IGZCOMHost() = default;
    virtual cIGZUnknown* Framework() = 0;                              // asked for class 1000
    virtual cIGZCOMDirector* ProcessDirector() = 0;
    virtual void CurrentAppPath(cIGZString& out) = 0;
};

class cIGZCOMLibraryManager : public cIGZCOM {   // the rest of the cGZCOM interface (slots 5..13)
public:
    virtual bool AddLibrary(const cIGZString& path) = 0;
    virtual void FreeUnusedLibraries() = 0;
    virtual bool CreateGuid(std::uint32_t* out) = 0;
    virtual bool CreateGuids(std::uint32_t* out, std::uint32_t count) = 0;
};

int GetGuidGenSeconds();

class cGZCOM final : public cIGZCOMLibraryManager, public cRZSystemService {
public:
    explicit cGZCOM(IGZCOMHost* host) : cRZSystemService(kGZSID_cGZCOM, kGZCOMPriority), host_(host), random_(0xffffffffu) {}
    ~cGZCOM() override = default;

    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return cRZSystemService::AddRef(); }
    std::uint32_t Release() override { return cRZSystemService::Release(); }
    bool Init() override { return true; }
    bool Shutdown() override { return true; }
    void SetServiceRunning(bool running) override { cRZSystemService::SetServiceRunning(running); }

    bool CreateInstance(std::uint32_t clsid, std::uint32_t iid, void** out) override { return GetClassObject(clsid, iid, out); }
    cIGZFrameWork* GetFrameWork() override { return reinterpret_cast<cIGZFrameWork*>(framework_); }
    bool GetClassObject(std::uint32_t clsid, std::uint32_t iid, void** out);
    bool RealInit();
    void RealShutdown();
    bool AddLibrary(const cIGZString& path) override;
    void FreeUnusedLibraries() override;
    bool CreateGuid(std::uint32_t* out) override;
    bool CreateGuids(std::uint32_t* out, std::uint32_t count) override;
    std::uint32_t EnumerateLibraries(cGZCOMLibrary** out, std::uint32_t& count);
    void EnumerateLibraries(bool (*callback)(cGZCOMLibrary*, void*), void* context);

    void FreeAllLibraries();
    bool UpdateClassRegistry(cGZCOMLibrary& library);
    static void AddEntryCallback(std::uint32_t clsid, std::uint32_t version, void* context);
    bool GetLibObject(cGZCOMLibrary& library, std::uint32_t clsid, std::uint32_t iid, void** out);

    // test / engine hooks: register a library whose director is already present
    cGZCOMLibrary* AddLoadedLibrary(const cIGZString& path, cIGZCOMDirector* director);

private:
    struct ClassEntry { std::uint32_t version; cGZCOMLibrary* library; };

    IGZCOMHost* host_;
    std::recursive_mutex lock_;
    bool initialised_ = false;                    // +0x41
    std::map<std::uint32_t, ClassEntry> classes_; // +0x48
    std::map<std::string, std::unique_ptr<cGZCOMLibrary>> libraries_;   // +0x60 (path order)
    cGZCOMLibrary* registering_ = nullptr;        // +0x78
    cIGZUnknown* framework_ = nullptr;            // +0x80
    cIGZCOMDirector* director_ = nullptr;         // +0x88
    cRZRandom random_;                            // +0x90
    std::int32_t nextGuidSecond_ = 0;             // +0xa8
};
