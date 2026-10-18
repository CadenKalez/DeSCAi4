// cGZDrawSystem — the 2-D draw system service (service id 0xeb2ea3cb, priority 1500000, interface 0x4b2ea3b3; ctor
// 0x10065959c, vtable 0x100addce0). It keeps an ordered list of draw-context engine factories (the front one that
// accepts a target wins), the registry of hardware buffer caches with a clock that ticks once per service tick, and
// builds "private buffers": a plain GZ buffer wrapped by a draw context (cGZDrawContextAsBuffer).
//
// Layout (0x90 bytes): +0 cIGZDrawSystem, +8 cRZSystemService (+0x18 id/priority fields), +0x30 engine factory list,
// +0x48 buffer cache list, +0x60 cache clock, +0x68 recursive mutex.
//
// Every five ticks the clock is advanced through OnTick by calling FlushOldBufferCaches(5): caches that are still
// valid and last used more than four clock steps ago are invalidated.
//
// Seams: RZGetFramework() (tick registration) and RZ::GS() (the graphics system that makes the raw buffer) are
// IDrawSystemHost. The software engine factory the original installs in Init (cGZDrawContextRenderToBufferFactory)
// needs the unrecovered cGZDrawContextRenderToBuffer engine, so it builds engines through a creator hook.
#pragma once
#include <cstdint>
#include <list>
#include <mutex>

#include "gz/Buffer.h"
#include "gz/DrawContext.h"
#include "gz/cIGZUnknown.h"
#include "rz/cRZRefCount.h"
#include "rz/cRZSystemService.h"

constexpr std::uint32_t kGZSID_cGZDrawSystem = 0xeb2ea3cb;
constexpr std::int32_t kGZDrawSystemPriority = 1500000;
constexpr std::uint32_t kGZIID_cIGZDrawSystem = 0x4b2ea3b3;
constexpr std::uint32_t kGZIID_cIGZDrawContextEngineFactory = 0x6b2ea76c;

class cIGZDrawContextEngineFactory : public cIGZUnknown {
public:
    // Builds an engine for `target` (answers false when this factory cannot draw into it).
    virtual bool CreateEngine(cIGZUnknown* target, gz::cIGZDrawEngine** out) = 0;   // +0x18
};

class cIGZDrawSystem : public cIGZUnknown {
public:
    virtual void AddEngine(cIGZDrawContextEngineFactory* factory) = 0;                                  // +0x18
    virtual bool CreateEngine(cIGZUnknown* target, gz::cIGZDrawEngine** out) = 0;                       // +0x20
    virtual bool CreatePrivateBuffer(cIGZBuffer** out, int width, int height, std::int32_t colorType) = 0;   // +0x28
    virtual void RegisterBufferCache(cIGZBufferHardwareCache* cache) = 0;                               // +0x30
    virtual void UnregisterBufferCache(cIGZBufferHardwareCache* cache) = 0;                             // +0x38
    virtual void FlushOldBufferCaches(std::uint32_t maxAge) = 0;                                        // +0x40
    virtual void FlushBufferCaches() = 0;                                                               // +0x48
    virtual std::uint32_t GetBufferCacheClock() = 0;                                                    // +0x50
};

class IDrawSystemHost {
public:
    virtual ~IDrawSystemHost() = default;
    virtual void AddTickClient(cIGZSystemService* service) = 0;      // RZGetFramework() vslot 0x48
    virtual void RemoveTickClient(cIGZSystemService* service) = 0;   // RZGetFramework() vslot 0x50
    virtual bool CreateBuffer(cIGZBuffer** out) = 0;                 // RZ::GS() vslot 0x20 (counted reference)
};
void SetDrawSystemHost(IDrawSystemHost* host);
IDrawSystemHost* GetDrawSystemHost();

// The factory Init installs: accepts any target that exposes cIGZBuffer (0x20732180).
class cGZDrawContextRenderToBufferFactory final : public cIGZDrawContextEngineFactory, public cRZRefCount {
public:
    using EngineCreator = gz::cIGZDrawEngine* (*)();   // returns a counted reference
    static void SetEngineCreator(EngineCreator creator);

    bool QueryInterface(std::uint32_t iid, void** out) override;    // @ 0x1006594ce
    std::uint32_t AddRef() override { cRZRefCount::AddRef(); return RefCount(); }   // @ 0x10065949e
    std::uint32_t Release() override { return static_cast<std::uint32_t>(cRZRefCount::Release()); }   // @ 0x1006594b6
    bool CreateEngine(cIGZUnknown* target, gz::cIGZDrawEngine** out) override;       // @ 0x1006594f0
};

class cGZDrawSystem final : public cIGZDrawSystem, public cRZSystemService {
public:
    cGZDrawSystem() : cRZSystemService(kGZSID_cGZDrawSystem, kGZDrawSystemPriority) {}   // @ 0x10065959c
    ~cGZDrawSystem() override;

    bool QueryInterface(std::uint32_t iid, void** out) override;    // @ 0x1006597b6
    std::uint32_t AddRef() override { return cRZSystemService::AddRef(); }     // @ 0x10065976a
    std::uint32_t Release() override { return cRZSystemService::Release(); }   // @ 0x100659790

    bool Init() override;                      // @ 0x100659824
    bool Shutdown() override;                  // @ 0x100659974
    bool OnTick(std::uint32_t elapsedMs) override;   // @ 0x100659bb8

    void AddEngine(cIGZDrawContextEngineFactory* factory) override;                          // @ 0x1006599bc
    bool CreateEngine(cIGZUnknown* target, gz::cIGZDrawEngine** out) override;                // @ 0x100659a84
    bool CreatePrivateBuffer(cIGZBuffer** out, int width, int height, std::int32_t colorType) override;   // @ 0x100659d44
    void RegisterBufferCache(cIGZBufferHardwareCache* cache) override;                       // @ 0x100659ad0
    void UnregisterBufferCache(cIGZBufferHardwareCache* cache) override;                     // @ 0x100659b46
    void FlushOldBufferCaches(std::uint32_t maxAge) override;                                // @ 0x100659c3c
    void FlushBufferCaches() override;                                                       // @ 0x100659cd2
    std::uint32_t GetBufferCacheClock() override { return clock_; }                          // @ 0x100659d3a

private:
    std::list<cIGZDrawContextEngineFactory*> engines_;     // +0x30 (each entry holds a reference)
    std::list<cIGZBufferHardwareCache*> caches_;           // +0x48
    std::uint32_t clock_ = 0;                              // +0x60
    std::recursive_mutex mutex_;                           // +0x68
};
