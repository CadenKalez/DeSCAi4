// cSC4AudioScapeManager — the 3D **soundscape** manager: named layers of positioned sound instances.
//
// This is NOT the radio/music selector. It never sees cIGZRadio or a station name; the music for the region and
// mayor screens is chosen by cSC4AudioEventHandler::HandleRadioMessages (message 0x2a3ad653, Data1 1 -> station
// "krgn", 0 -> "kmay", through cIGZRadio::SetStation). This class is the other half of the SC4 audio layer: an
// emanation-free, position-grouped ambience system. Each layer (CreateLayer) owns sound definitions
// (RegisterLayerSound) and distance-bucketed groups (tGroup) of instances; adding an instance near an existing
// group joins it, otherwise a new group is created (radius from the layer), and the group's position is the
// running average of its instances. A group starts/stops a layer sound depending on how many instances it holds
// (tSoundDef +0 minInstances, +4 maxInstances) and fades the group volume towards its target in OnTick. Sounds
// are not played by this class: it posts cSC4AudioSoundManager event ids to the sound system (gpSndSys).
//
// Recovered from Sim City 4 Deluxe Editionsub 1.2.1: 52 member symbols 0x1003ded76..0x1003f2850 (the thunks share
// their targets), 11 tGroup symbols 0x1003f0942..0x1003f2720, 3 tLayer symbols 0x1003f06c2..0x1003f2794 and the
// free _PostStart @ 0x1003f23d1. Three vtables:
//   cISC4AudioScapeManager 0x100aac1b0 (slots 0..19; CreateLayer etc. at 5..17, destructors at 18/19)
//   cIGZSystemService      0x100aac2a0 (group at vtable+0xf0), cIGZMessageTarget2 0x100aac318 (group at +0x158)
// The original's primary vtable also repeats DoMessage and the system-service slots at 20..27; those are the same
// functions reached through the +8/+0x10 views, and the port does not duplicate them in the primary vtable.
// QueryInterface ids: 1 / 0x29e39db4 -> cISC4AudioScapeManager view (+0), 0x287fb697 -> cIGZSystemService view (+8);
// the cIGZMessageTarget2 view (+0x10) is not answered (quirk kept). Create() returns the **+8** view.
//
// Layout of the original (0x108 bytes, all offsets in the binary's base terms; the port's cRZCriticalSection is
// larger than the original's 0x10-byte block, so offsets after +0x50 differ in the port):
//   +0x00 vptr cISC4AudioScapeManager   +0x08 vptr cIGZSystemService   +0x10 vptr cIGZMessageTarget2
//   +0x18 cRZAutoRefCount<cIGZSndSys>   +0x20 cRZAutoRefCount<cIGZSndMgr>   +0x28 bool initialized
//   +0x2c uint32 reference count        +0x30 cRZTimer (milliseconds)       +0x48 uint32 last tick ms
//   +0x50 cRZCriticalSection (never locked)  +0x60 cRZCriticalSection (layers)  +0x70 cRZCriticalSection (OnTick)
//   +0x80 std::list<tLayer*> layers (size +0x90)   +0x98 std::list<tGroup*> pending groups (never filled)
//   +0xb0 std::list<cIGZSnd*> owned sounds (never filled)  +0xc8/+0xd4 two unused cS3DVector3
//   +0xe8 uint32 service id (0x49e50ba1)  +0xec bool running  +0xf0 std::vector<tSortingBin> binning scratch
//
// Original quirks kept (covered by scripts/tests/sc4_audioscapemanager_test.cpp; the layer name of the implicit
// layer is the only one the public API cannot observe, so it is only documented here):
//  * Init fetches cIGZSndSys (0x23cecba0/0x106077d2) and cIGZSndMgr (0x1af85bfb/0x3af85bb7), stores both and mirrors
//    the sound system into the file-static gpSndSys; when the sound manager is missing it returns 0 but keeps the
//    sound system held and gpSndSys set.
//  * Release destroys at a count of 1 without decrementing first; AddRef returns the incremented count.
//  * DoMessage answers `message != null` (it claims to consume every message).
//  * GetServicePriority is INT32_MIN and OnIdle always false.
//  * DestroyLayer clears the flags (+0x4c) of every layer it inspects, not only the matching one, and clears the
//    layer through the interface's own ClearLayer before deleting it.
//  * CreateLayer with flag bit 0 registers a layer sound whose id is the layer id, range 0..1, i.e.
//    RegisterLayerSound(layerId, layerId, false, 0, 1, 0x7fffffff).
//  * The sound instance key posted to the sound system is the low 32 bits of the heap tSoundDef copy (not the
//    sound id); two live copies are assumed to differ. The copy's +0x14 volume is copied but never read.
//  * InsertInstance on an unknown layer implicitly creates a layer named "nameless" with flags 1 and radius 50.0f,
//    and then loses the one-off sound definition of the 6-argument overload (it re-enters the 4-argument form).
//  * UpdateInstance ignores its layer-id argument and scans every layer; a moved instance that leaves its group's
//    radius is removed from that group and re-added through _AddInstanceToCorrectGroup.
//  * ClearAllLayers(keepInstances=true) removes the instances of a group but does not stop its sounds, and neither
//    branch updates the layer's instance count (+0x6c), which therefore stays stale for GetGroupPositions/...Avg
//    until the layer is cleared.
//  * _DeleteGroup only frees the group while a sound system is held (+0x18); without one it reports 0 and leaves
//    the group in place.
//  * tGroup::CenterRemovePosition divides 1.0f by the decremented instance count, so removing the last position
//    leaves the centre at NaN (-0 * inf); ClearLayer avoids it by using RemoveAllInstances.
//  * tGroup::RemoveAllInstances frees the instance records but leaves the accumulated position sum (+0xc) set.
//  * The pending-group list OnTick walks (+0x98, expiry +0x58) is never filled by any recovered function.
//  * The original allocates groups and instance records with malloc(sGroupAllocator = 0x78 = sizeof(tGroup)) and
//    frees them with free(); the port uses new/delete for groups and keeps malloc/free for instance records.
//
// Deviations forced by safety:
//  * _GetDistributedGroupPositionsAvg skips a group whose bin index would be negative or beyond the bin vector;
//    the original indexes the vector unchecked (a group left of / above the rectangle corrupts the heap).
//  * The same function guards the float-to-int bucket casts (the original's cast is undefined for inf/NaN and for
//    a zero bucket size) and treats a non-positive requested count as zero bins.
//  * ClearAllLayers advances past a group _DeleteGroup refused to free instead of looping forever.
//  * QueryInterface rejects a null out pointer.
#pragma once
#include <cstddef>
#include <cstdint>
#include <list>
#include <vector>

#include "audio/interfaces/cIGZSndMgr.h"
#include "audio/interfaces/cIGZSndSys.h"
#include "gz/StreamOps.h"
#include "gz/cIGZMessage2.h"
#include "gz/cIGZSystemService.h"
#include "gz/cIGZUnknown.h"
#include "rz/cRZAutoRefCount.h"
#include "rz/cRZCriticalSection.h"
#include "rz/cRZString.h"
#include "rz/cRZTimer.h"
#include "sc4/QuadGrid.h"   // SC4RectFloat

class cIGZString;

// The service/interface ids of the scape manager (framework GetSystemService(0x49e50ba1, 0x29e39db4)).
constexpr std::uint32_t kGZSID_cISC4AudioScapeManager = 0x49e50ba1;
constexpr std::uint32_t kGZIID_cISC4AudioScapeManager = 0x29e39db4;   // QueryInterface answers this with the +0 view
constexpr std::int32_t kAudioScapeServicePriority = static_cast<std::int32_t>(0x80000000u);

// cSC4AudioSoundManager event ids this class posts (see sc4audio/SoundManager.h).
constexpr std::uint32_t kAudioScapeEventStartSound = 0xea451903;   // data1 sound id, data2 instance key
constexpr std::uint32_t kAudioScapeEventStopSound = 0x6a451908;    // data1 instance key
constexpr std::uint32_t kAudioScapeEventSetPosition = 0xca45190f;  // data1 key, data2/3/4 x/y/z (ints)
constexpr std::uint32_t kAudioScapeEventSetVolume = 0x4a451915;    // data1 key, data2 volume

// Volume scale of the sound layer (cIGZSnd's 0..1024) and the fade step divisor of OnTick.
constexpr std::uint32_t kAudioScapeMaxVolume = 0x400;
constexpr std::int32_t kAudioScapeFadeDivisor = 7;

// The radius of the implicit "nameless" layer (float 50.0f) and the default tSoundDef volume float (also 50.0f,
// copied but never read).
constexpr float kAudioScapeImplicitLayerRadius = 50.0f;
constexpr float kAudioScapeDefaultSoundValue = 50.0f;

// Layer flags (+0x4c).
constexpr std::uint32_t kAudioScapeLayerFlagOwnSound = 0x1;        // CreateLayer registers a sound for the layer id
constexpr std::uint32_t kAudioScapeLayerFlagNoInstanceList = 0x4;  // groups do not keep instance records
constexpr std::uint32_t kAudioScapeLayerFlagPersistent = 0x10;     // Clear keeps groups that still have instances

// The name of the layer InsertInstance creates on demand.
constexpr const char* kAudioScapeImplicitLayerName = "nameless";

// The original mallocs groups and instance records with the static sGroupAllocator, initialised by the module
// initialiser @ 0x1003f2f5b to sizeof(tGroup) = 0x78.
constexpr std::size_t kAudioScapeGroupAllocationSize = 0x78;

// The interface the game asks the framework for (iid kGZIID_cISC4AudioScapeManager). Slot order as in the binary's
// primary vtable: [3] Init, [4] Shutdown, [5]..[17] the layer/instance calls, [18]/[19] destructors.
class cISC4AudioScapeManager : public cIGZUnknown {
public:
    virtual bool Init() = 0;                                                                              // [3] +0x18
    virtual bool Shutdown() = 0;                                                                          // [4] +0x20
    virtual bool CreateLayer(const cIGZString& name, std::uint32_t layerId, std::uint32_t flags, float radius) = 0;  // [5] +0x28
    virtual bool DestroyLayer(std::uint32_t layerId) = 0;                                                 // [6] +0x30
    virtual bool ClearLayer(std::uint32_t layerId) = 0;                                                   // [7] +0x38
    virtual bool ClearAllLayers(std::uint32_t flagsMask, bool keepInstances) = 0;                         // [8] +0x40
    virtual bool GetDistributedGroupPositionsAvgIntersected(std::uint32_t layerA, std::uint32_t layerB,
                                                            SC4RectFloat& rect, cS3DVector3* out,
                                                            int& count, int& total, bool clip) = 0;        // [9] +0x48
    virtual bool GetDistributedGroupPositionsAvg(std::uint32_t layerId, SC4RectFloat& rect, cS3DVector3* out,
                                                 int& count, int& total, bool clip) = 0;                  // [10] +0x50
    virtual bool GetGroupPositions(std::uint32_t layerId, cS3DVector3* out, int& count, int& total) = 0;   // [11] +0x58
    virtual bool InsertInstance(std::uint32_t layerId, std::uint32_t instanceId, const cS3DVector3& position,
                                float radius) = 0;                                                        // [12] +0x60
    virtual bool InsertInstance(std::uint32_t layerId, std::uint32_t soundId, bool soundFlag,
                                std::uint32_t instanceId, const cS3DVector3& position, float radius) = 0;    // [13] +0x68
    virtual bool RemoveInstance(std::uint32_t layerId, std::uint32_t instanceId) = 0;                     // [14] +0x70
    virtual bool UpdateInstance(std::uint32_t layerId, std::uint32_t instanceId, const cS3DVector3& position,
                                float radius) = 0;                                                        // [15] +0x78
    virtual bool RegisterLayerSound(std::uint32_t layerId, std::uint32_t soundId, bool soundFlag, int arg4,
                                    int arg5, int arg6) = 0;                                              // [16] +0x80
    virtual bool UnregisterLayerSound(std::uint32_t layerId, std::uint32_t soundId) = 0;                  // [17] +0x88
    virtual ~cISC4AudioScapeManager() = default;                                                          // [18]/[19]
};

class cSC4AudioScapeManager final : public cISC4AudioScapeManager,
                                    public cIGZSystemService,
                                    public cIGZMessageTarget2 {
public:
    // One accumulation bin of _GetDistributedGroupPositionsAvg (0x10 bytes): position sum then the instance count.
    struct tSortingBin {
        float x = 0, y = 0, z = 0;   // +0x00..+0x08
        std::uint32_t count = 0;     // +0x0c
    };

    // A registered sound definition (0x18 bytes). It lives in a tLayer's sound list and is copied into a tGroup's
    // list while that group plays it.
    struct tSoundDef {
        std::uint32_t minInstances = 0;   // +0x00 HandleSound wants at least this many group instances
        std::uint32_t maxInstances = 0;   // +0x04 ... and at most this many
        std::uint32_t unknown08 = 0;      // +0x08 RegisterLayerSound's 4th argument
        std::uint32_t soundId = 0;        // +0x0c the sound id posted with kAudioScapeEventStartSound
        bool soundFlag = false;           // +0x10 RegisterLayerSound's 3rd argument (copied only)
        float value = 0.0f;               // +0x14 50.0f by default; copied only, never read
    };

    // An instance record in a tGroup's instance list (0x10 bytes used). The original allocates it with
    // malloc(sGroupAllocator = 0x78) and frees it with free().
    struct tInstance {
        std::uint32_t id = 0;   // +0x00
        float x = 0, y = 0, z = 0;   // +0x04
    };

    // A distance bucket of one layer: instances whose position is within the group radius of the group centre.
    struct tGroup {
        cS3DVector3 center;                 // +0x00 running average of the instance positions
        cS3DVector3 sum;                    // +0x0c sum of the positions
        std::int32_t count = 0;             // +0x18 number of positions in the average
        std::int32_t mark = 0;              // +0x1c scratch counter of GetDistributedGroupPositionsAvgIntersected
        float radius = 0.0f;                // +0x20 copied from the layer when the group is created
        std::list<tInstance*> instances;    // +0x28 (size +0x38, count field +0x5c)
        std::list<tSoundDef*> sounds;       // +0x40 copies of the definitions this group is playing (+0x50/+0x60)
        std::int32_t pendingExpiry = 0;     // +0x58 ms used by the never-filled pending-group list OnTick walks
        std::int32_t instanceCount = 0;     // +0x5c
        std::int32_t soundCount = 0;        // +0x60
        std::uint32_t volume[2] = {0, 0};                     // +0x64/+0x68 eased by OnTick
        std::uint32_t targetVolume[2] = {kAudioScapeMaxVolume, kAudioScapeMaxVolume};   // +0x6c/+0x70
        std::uint32_t lastStartVolume = 0;  // +0x74 volume computed by the last _Start

        tGroup();                                          // @ 0x1003f2720
        bool RemoveAllInstances();                         // @ 0x1003f0942
        bool RemoveInstance(std::uint32_t instanceId);     // @ 0x1003f17ee
        void CenterRemovePosition(const cS3DVector3& position);   // @ 0x1003f1ac4
        void CenterAddPosition(const cS3DVector3& position);      // @ 0x1003f1b30
        bool StopAllSounds();                              // @ 0x1003f1fca
        std::uint32_t SetVolume(std::uint32_t index, std::uint32_t volume);   // @ 0x1003f205e
        void HandleSound(tSoundDef* soundDef);             // @ 0x1003f21c8
        void UpdatePositions();                            // @ 0x1003f21ec
        bool _Start(tSoundDef* soundDef);                  // @ 0x1003f2248
        bool _Stop(tSoundDef* soundDef);                   // @ 0x1003f2316
    };

    // A named layer of grouped instances and registered sounds (0x70 bytes).
    struct tLayer {
        cRZString name;                          // +0x00 (0x28 bytes)
        std::uint32_t id = 0;                    // +0x2c
        std::list<tGroup*> groups;               // +0x30 (size +0x40)
        std::int32_t groupCount = 0;             // +0x48
        std::uint32_t flags = 0;                 // +0x4c
        std::list<tSoundDef*> sounds;            // +0x50 (size +0x60)
        float radius = 0.0f;                     // +0x68
        std::int32_t instanceCount = 0;          // +0x6c
        // The original has no tLayer constructor symbol: the three creations inline the member initialisation.
        bool UnregisterAllSounds();              // @ 0x1003f06c2
        bool Clear();                            // @ 0x1003f07a6
        ~tLayer();                               // @ 0x1003f2794
    };

    // @ 0x1003ded76: operator new(0x108), the constructor, and the **second** (+8, cIGZSystemService) view is
    // returned (Ghidra guessed cSC4AudioScapeManager* from the symbol; the mangled name carries no return type).
    static cIGZSystemService* Create();

    cSC4AudioScapeManager();                                    // @ 0x1003f242c (alias @ 0x1003f25c0)
    ~cSC4AudioScapeManager() override;                          // @ 0x1003f25ca / 0x1003f26e6 / 0x1003f26f0

    // --- cISC4AudioScapeManager ---
    bool Init() override;                                       // @ 0x1003f0196
    bool Shutdown() override;                                   // @ 0x1003f034a
    bool CreateLayer(const cIGZString& name, std::uint32_t layerId, std::uint32_t flags, float radius) override;   // @ 0x1003f0450
    bool DestroyLayer(std::uint32_t layerId) override;          // @ 0x1003f05d8
    bool ClearLayer(std::uint32_t layerId) override;            // @ 0x1003f072a
    bool ClearAllLayers(std::uint32_t flagsMask, bool keepInstances) override;   // @ 0x1003f085e
    bool GetDistributedGroupPositionsAvgIntersected(std::uint32_t layerA, std::uint32_t layerB,
                                                    SC4RectFloat& rect, cS3DVector3* out,
                                                    int& count, int& total, bool clip) override;   // @ 0x1003f0a2a
    bool GetDistributedGroupPositionsAvg(std::uint32_t layerId, SC4RectFloat& rect, cS3DVector3* out,
                                         int& count, int& total, bool clip) override;             // @ 0x1003f10fa
    bool GetGroupPositions(std::uint32_t layerId, cS3DVector3* out, int& count, int& total) override;   // @ 0x1003f1212
    bool InsertInstance(std::uint32_t layerId, std::uint32_t instanceId, const cS3DVector3& position,
                        float radius) override;                             // @ 0x1003f12d6
    bool InsertInstance(std::uint32_t layerId, std::uint32_t soundId, bool soundFlag,
                        std::uint32_t instanceId, const cS3DVector3& position, float radius) override;   // @ 0x1003f12ee
    bool RemoveInstance(std::uint32_t layerId, std::uint32_t instanceId) override;   // @ 0x1003f16cc
    bool UpdateInstance(std::uint32_t layerId, std::uint32_t instanceId, const cS3DVector3& position,
                        float radius) override;                             // @ 0x1003f18c8
    bool RegisterLayerSound(std::uint32_t layerId, std::uint32_t soundId, bool soundFlag, int arg4,
                            int arg5, int arg6) override;                   // @ 0x1003f1bc8
    bool UnregisterLayerSound(std::uint32_t layerId, std::uint32_t soundId) override;   // @ 0x1003f1cfa

    // --- cIGZSystemService / cIGZUnknown / cIGZMessageTarget2 ---
    bool QueryInterface(std::uint32_t iid, void** out) override;   // @ 0x1003f20e0
    std::uint32_t AddRef() override;                              // @ 0x1003f2130
    std::uint32_t Release() override;                             // @ 0x1003f215a
    std::uint32_t GetServiceID() const override;                  // @ 0x1003f27cc
    void SetServiceID(std::uint32_t id) override;                 // @ 0x1003f27d8
    std::int32_t GetServicePriority() const override;             // @ 0x1003f27e4
    bool IsServiceRunning() const override;                       // @ 0x1003f27f0
    void SetServiceRunning(bool running) override;                // @ 0x1003f27fc
    bool OnIdle(std::uint32_t elapsedMs) override;                // @ 0x1003f280a
    bool OnTick(std::uint32_t elapsedMs) override;                // @ 0x1003f1dd4
    bool DoMessage(cIGZMessage2* message) override;               // @ 0x1003f20c8

private:
    tLayer* _FindLayerNoLock(std::uint32_t layerId);
    // @ 0x1003f09ac
    bool _DeleteGroup(tLayer* layer, std::list<tGroup*>::iterator& group);
    // @ 0x1003f0da6
    bool _GetDistributedGroupPositionsAvg(tLayer* layer, SC4RectFloat& rect, cS3DVector3* out,
                                          int& count, int& total, bool clip);
    // @ 0x1003f1522: adds the record to the nearest group of the layer (or a new one); the sound definition starts
    // playing in that group. The original's float argument (radius) and |this| are unused.
    bool _AddInstanceToCorrectGroup(tLayer* layer, tSoundDef* soundDef, tInstance* instance,
                                    const cS3DVector3& position, float radius, bool add);
    // @ 0x1003f1866: lets the group react to soundDef (may be null) and to every sound of the layer, then updates
    // the group's sound positions. |this| is unused in the original too.
    void _UpdateLayerGroup(tLayer* layer, tSoundDef* soundDef, tGroup* group);

    cRZAutoRefCount<cIGZSndSys> soundSystem_;    // +0x18
    cRZAutoRefCount<cIGZSndMgr> soundManager_;   // +0x20
    bool initialized_ = false;                   // +0x28
    std::uint32_t refCount_ = 0;                 // +0x2c
    cRZTimer timer_{cRZTimer::kMilliseconds};      // +0x30
    std::uint32_t lastTickMs_ = 0;               // +0x48
    cRZCriticalSection lock50_;                  // +0x50 (never used by the original)
    cRZCriticalSection lock60_;                  // +0x60 layers and layer sounds
    cRZCriticalSection lock70_;                  // +0x70 OnTick
    std::list<tLayer*> layers_;                  // +0x80
    std::list<tGroup*> pendingGroups_;           // +0x98 (never filled)
    std::list<cIGZSnd*> sounds_;                 // +0xb0 (never filled)
    cS3DVector3 unusedC8_;                       // +0xc8
    cS3DVector3 unusedD4_;                       // +0xd4
    std::uint32_t serviceId_ = kGZSID_cISC4AudioScapeManager;   // +0xe8
    bool serviceRunning_ = false;                // +0xec
    std::vector<tSortingBin> sortingBins_;       // +0xf0
};

static_assert(sizeof(cSC4AudioScapeManager::tSortingBin) == 0x10, "tSortingBin is 0x10 bytes in the binary");
static_assert(sizeof(cSC4AudioScapeManager::tSoundDef) == 0x18, "tSoundDef is 0x18 bytes in the binary");
static_assert(sizeof(cSC4AudioScapeManager::tInstance) == 0x10, "tInstance is 0x10 bytes in the binary");
static_assert(sizeof(cSC4AudioScapeManager::tGroup) == 0x78, "tGroup is 0x78 bytes in the binary (+0x00..+0x74)");
static_assert(sizeof(cSC4AudioScapeManager::tLayer) == 0x70, "tLayer is 0x70 bytes in the binary (+0x00..+0x6c)");
