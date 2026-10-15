// cGZApp — base class of the game application (40 functions, 0x100049086..0x10004940c): a system service
// (id 0x66, priority -2000000) that carries the module name and exposes the application hooks the framework
// calls while starting up and shutting down. The defaults here do nothing and succeed; the game application
// (cSC4App) overrides them. Layout: +0 cRZSystemService, +0x28 cIGZApp view, +0x38 module name string.
#pragma once
#include <cstdint>
#include <string>

#include "rz/cRZSystemService.h"

constexpr std::uint32_t kGZSID_cGZApp = 0x66;
constexpr std::int32_t kGZAppPriority = -2000000;
constexpr std::uint32_t kGZIID_cIGZApp = 0x66;

// The framework (cIGZFramework) as far as the application uses it.
class IGZAppFramework {
public:
    virtual ~IGZAppFramework() = default;
    virtual void AddSystemService(cIGZSystemService* service) = 0;   // vslot 0x18
};

class cIGZApp : public cIGZUnknown {
public:
    virtual cIGZSystemService* AsIGZSystemService() = 0;
    virtual void AddApplicationService(cIGZSystemService* service) = 0;
    virtual const char* ModuleName() = 0;
    virtual bool PreFrameWorkInit() = 0;
    virtual bool PostFrameWorkInit() = 0;
    virtual std::uint32_t GZRun() = 0;
    virtual bool PreFrameWorkShutdown() = 0;
    virtual IGZAppFramework* FrameWork() = 0;
    virtual void AddDynamicLibrariesHere() = 0;
    virtual void AddCOMDirectorsHere() = 0;
    virtual void AddApplicationServicesHere() = 0;
    virtual bool LoadRegistry() = 0;
};

class cGZApp : public cRZSystemService, public cIGZApp {
public:
    explicit cGZApp(const char* moduleName, IGZAppFramework* framework = nullptr)
        : cRZSystemService(kGZSID_cGZApp, kGZAppPriority), moduleName_(moduleName), framework_(framework) {}

    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return cRZSystemService::AddRef(); }
    std::uint32_t Release() override { return cRZSystemService::Release(); }
    bool Shutdown() override { return true; }

    cIGZSystemService* AsIGZSystemService() override { return this; }
    void AddApplicationService(cIGZSystemService* service) override { FrameWork()->AddSystemService(service); }
    const char* ModuleName() override { return moduleName_.c_str(); }
    bool PreFrameWorkInit() override { return true; }
    bool PostFrameWorkInit() override { return true; }
    std::uint32_t GZRun() override { return 0; }
    bool PreFrameWorkShutdown() override { return true; }
    IGZAppFramework* FrameWork() override { return framework_; }   // RZGetFrameWork()
    void AddDynamicLibrariesHere() override {}
    void AddCOMDirectorsHere() override {}
    void AddApplicationServicesHere() override {}
    bool LoadRegistry() override { return true; }

private:
    std::string moduleName_;      // +0x38
    IGZAppFramework* framework_;
};
