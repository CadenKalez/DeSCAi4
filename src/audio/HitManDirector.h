// cHitManCOMDirector — the COM director of the Hit audio library (9 functions, 0x10043adb8..0x10043b45a,
// director id 0x9b2d3d2f). The constructor registers the ten Hit classes with the COM framework, PreAppInit
// hooks the Hit resource types up to cHitResourceFactory and installs the data-source-channel creator in the
// sound manager, PreAppShutdown shuts the hit manager and debug info down. The cRZCOMDllDirector base is not
// recovered; its AddCls() table and the framework services are the IHitDirectorHost seam.
#pragma once
#include <array>
#include <cstdint>

#include "audio/HitClassIds.h"
#include "audio/interfaces/cIGZSndMgr.h"
#include "audio/interfaces/cIHitDebugInfo.h"
#include "audio/interfaces/cIHitMan.h"
#include "gz/cIGZPersistResourceManager.h"

namespace sc4audio {

constexpr std::uint32_t kHitManDirectorId = HitIds::kHitManDirectorId;     // GetDirectorID() @ 0x10043b1f0

// Order of the AddCls() calls in the constructor (class id; the factory is looked up by the host).
inline constexpr std::array<std::uint32_t, 10> kHitManClassIds = {
    HitIds::kClsidHitMan,
    HitIds::kClsidHitDebugInfo,
    HitIds::kClsidHitResourceFactory,
    HitIds::kClsidHitSegmentTrackCreator,
    HitIds::kClsidHitSegmentDataSourceChannelCreator,
    HitIds::kClsidHitTrackPlayer,
    HitIds::kClsidHitChannel,
    HitIds::kClsidHitIniFile,
    HitIds::kClsidPropertySetUint32XmlReader,
    HitIds::kClsidHitList,
};

// Resource types registered with cHitResourceFactory by PreAppInit, in order (the first has no handler).
inline constexpr std::array<std::uint32_t, 4> kHitFactoryResourceTypes = {
    HitIds::kResTypeRegisteredButUnhandled, HitIds::kResTypeTrackPlayer, HitIds::kResTypeHitList,
    HitIds::kResTypePropertySetA,
};

class IHitDirectorHost {
public:
    virtual ~IHitDirectorHost() = default;
    virtual void AddClass(std::uint32_t classId) = 0;                                      // cRZCOMDllDirector::AddCls
    virtual cIGZPersistResourceManager* ResourceManager() = 0;                              // service 0x056b906e
    virtual cIGZSndMgr* SoundManager() = 0;                                                 // service 0x1af85bfb
    virtual cIHitMan* HitManager() = 0;                                                     // service 0x1b2ce671
    virtual cIHitDebugInfo* DebugInfo() = 0;                                                // service 0xfbc9cd04
    virtual bool CreateInstance(std::uint32_t classId, std::uint32_t iid, void** out) = 0;  // GZCOM
    // OnStart: registers a created object as a system service with the framework
    virtual void RegisterSystemService(cIGZUnknown* service) = 0;
    virtual void AddSystemServiceDirector(cIGZUnknown* director) = 0;                       // framework vslot 0x38
};

class cHitManCOMDirector {
public:
    explicit cHitManCOMDirector(IHitDirectorHost* host);   // registers the classes

    std::uint32_t GetDirectorID() const { return kHitManDirectorId; }
    // Creates the hit manager and debug info through COM and registers them as services.
    bool OnStart(cIGZUnknown* com);
    bool PreAppInit();
    bool PreAppShutdown();

private:
    IHitDirectorHost* host_;
};

}  // namespace sc4audio
