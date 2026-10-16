// cRZCOMDllDirector — the base of every COM director (0x10003f2e8..0x10003fc36, iids 0xa21ee941 / 1 for the
// director view, 0x03fa40bf for the framework-hook view at +8). A director owns a table clsid -> factory
// (two kinds: a creator returning an unknown, and a "make interface" function), a list of child directors whose
// classes it also serves, the COM object and the library path it was initialised with.
//
// Layout (0x90 bytes): +0 director view, +8 hook view, +0x10 refcount, +0x18 library path (cRZString), +0x40 COM,
// +0x48 framework, +0x50 child directors, +0x68 class table (unordered_map; max load factor +0x88 = 1.0).
// Note the original CanUnloadNow answers "true" without asking the children when the refcount is not zero.
#pragma once
#include <cstdint>
#include <initializer_list>
#include <unordered_map>
#include <vector>

#include "gz/cIGZString.h"
#include "gz/cIGZSystemService.h"
#include "gz/cIGZUnknown.h"
#include "rz/cRZString.h"

constexpr std::uint32_t kGZIID_cIRZCOMDllDirector = 0xa21ee941, kGZIID_cIGZFrameWorkHooks = 0x03fa40bf;

class cIGZFrameWork;

class cIGZCOM : public cIGZUnknown {
public:
    virtual bool CreateInstance(std::uint32_t clsid, std::uint32_t iid, void** out) = 0;   // [3]
    virtual cIGZFrameWork* GetFrameWork() = 0;                                              // [4]
};

class cIGZCOMDirector : public cIGZUnknown {
public:
    virtual bool InitializeCOM(cIGZCOM* com, const cIGZString& libraryPath) = 0;                              // [3]
    virtual bool OnStart(cIGZCOM* com) = 0;                                                                   // [4]
    virtual void EnumClassObjects(void (*fn)(std::uint32_t clsid, std::uint32_t, void*), void* context) = 0;  // [5]
    virtual bool GetClassObject(std::uint32_t clsid, std::uint32_t iid, void** out) = 0;                      // [6]
    virtual bool CanUnloadNow() = 0;                                                                          // [7]
    virtual bool OnUnload() = 0;                                                                              // [8]
    virtual std::uint32_t RefCount() = 0;                                                                     // [9]
    virtual std::uint32_t RemoveRef() = 0;                                                                    // [10]
    virtual cIGZFrameWork* FrameWork() = 0;                                                                   // [11]
    virtual cIGZCOM* GZCOM() = 0;                                                                             // [12]
    virtual std::uint32_t GetDirectorID() const = 0;                                                          // [13]
    virtual bool GetLibraryPath(cIGZString& out) = 0;                                                         // [14]
    virtual std::size_t GetHeapAllocatedSize() = 0;                                                           // [15]
};

class cIGZFrameWorkHooks : public cIGZUnknown {
public:
    virtual bool PreFrameWorkInit() = 0;
    virtual bool PreAppInit() = 0;
    virtual bool PostAppInit() = 0;
    virtual bool PreAppShutdown() = 0;
    virtual bool PostAppShutdown() = 0;
    virtual bool PostSystemServiceShutdown() = 0;
    virtual bool AbortiveQuit() = 0;
    virtual bool OnInstall() = 0;
};

// RZGetHeapAllocatedSize(): the process-wide allocation counter.
using RZHeapSizeFn = std::size_t (*)();
void SetRZHeapSizeFn(RZHeapSizeFn fn);

// The framework services the directors' OnStart handlers use (RZGetFramework(): vslot 0x18 registers a system
// service, vslot 0x38 registers the framework hooks).
class IDirectorFramework {
public:
    virtual ~IDirectorFramework() = default;
    virtual void AddSystemService(cIGZUnknown* service) = 0;
    virtual void AddHooks(cIGZFrameWorkHooks* hooks) = 0;
    virtual void RegisterSystemService(cIGZUnknown* service) = 0;      // vslot 0x48 (a service's Init)
    virtual void UnregisterSystemService(cIGZUnknown* service) = 0;    // vslot 0x50 (a service's Shutdown)
};
IDirectorFramework* DirectorFramework();
void SetDirectorFramework(IDirectorFramework* framework);

class cRZCOMDllDirector : public cIGZCOMDirector, public cIGZFrameWorkHooks {
public:
    using CreateFn = cIGZUnknown* (*)();
    using MakeFn = bool (*)(std::uint32_t iid, void** out);

    cRZCOMDllDirector() = default;
    virtual ~cRZCOMDllDirector() = default;

    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return ++refCount_; }
    std::uint32_t Release() override { return RemoveRef(); }
    bool InitializeCOM(cIGZCOM* com, const cIGZString& libraryPath) override;
    bool OnStart(cIGZCOM*) override { return true; }
    void EnumClassObjects(void (*fn)(std::uint32_t, std::uint32_t, void*), void* context) override;
    bool GetClassObject(std::uint32_t clsid, std::uint32_t iid, void** out) override;
    bool CanUnloadNow() override;
    bool OnUnload() override { return true; }
    std::uint32_t RefCount() override { return refCount_; }
    std::uint32_t RemoveRef() override;
    cIGZFrameWork* FrameWork() override { return framework_; }
    cIGZCOM* GZCOM() override { return com_; }
    bool GetLibraryPath(cIGZString& out) override;
    std::size_t GetHeapAllocatedSize() override;

    bool PreFrameWorkInit() override { return true; }
    bool PreAppInit() override { return true; }
    bool PostAppInit() override { return true; }
    bool PreAppShutdown() override { return true; }
    bool PostAppShutdown() override { return true; }
    bool PostSystemServiceShutdown() override { return true; }
    bool AbortiveQuit() override { return true; }
    bool OnInstall() override { return true; }

    void AddDirector(cRZCOMDllDirector* child);
    void AddCls(std::uint32_t clsid, CreateFn create);
    void AddCls(std::uint32_t clsid, MakeFn make);

protected:
    // OnStart of the plain directors: register as framework hooks, then create each service class through COM
    // and register it with the framework.
    bool StartServices(cIGZCOM* com, std::initializer_list<std::uint32_t> serviceClasses);
    struct Entry { void* fn; int kind; };   // kind 1: CreateFn, kind 2: MakeFn
    std::uint32_t refCount_ = 0;                          // +0x10
    cRZString libraryPath_;                               // +0x18
    cIGZCOM* com_ = nullptr;                              // +0x40
    cIGZFrameWork* framework_ = nullptr;                  // +0x48
    std::vector<cRZCOMDllDirector*> children_;            // +0x50
    std::unordered_map<std::uint32_t, Entry> classes_;    // +0x68
};
