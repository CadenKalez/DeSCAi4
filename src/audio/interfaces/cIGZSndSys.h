// cIGZSndSys — the sound system service (service 0x23cecba0, iid 0x106077d2). Vtable order from
// cGZSndSys (vtable 0x100ab5e00), slots 3..50. Only the methods the recovered game code calls are
// documented in detail; the types of the others are forward declared.
#pragma once
#include <cstdint>

#include "audio/interfaces/cIGZSnd3DListener.h"
#include "audio/interfaces/cIGZSndEaRendererEffects.h"
#include "audio/interfaces/cIGZSndEventStd.h"
#include "audio/interfaces/cIGZSndInstanceCache.h"
#include "audio/interfaces/cIGZSndPropertyCache.h"
#include "audio/interfaces/cIGZSndRenderer.h"
#include "audio/interfaces/cIGZSndUpdateTarget.h"
#include "gz/cIGZString.h"

class cIGZCriticalSection;

class cIGZSndSys : public cIGZUnknown {
public:
    using SndCallbackInterval = std::uint32_t;   // original: nested enum (milliseconds in practice)
    using UpdateTargetFunction = bool (*)(cIGZSndUpdateTarget*, std::uint32_t, void*);

    virtual cIGZSnd3DListener* AsIGZSnd3DListener() = 0;           // [3]  +0x18
    virtual cIGZSnd3DListenerEx* AsIGZSnd3DListenerEx() = 0;       // [4]  +0x20
    virtual bool DoEvent(cIGZSndEventStd* event) = 0;              // [5]  +0x28 synchronous dispatch
    virtual bool DoEventIds(std::uint32_t id, std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d) = 0;  // [6] +0x30
    virtual bool DoEventUnknown(std::uint32_t id, cIGZUnknown* u, std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d) = 0;  // [7] +0x38
    virtual bool DoEventString(std::uint32_t id, cIGZString* s, std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d) = 0;    // [8] +0x40
    virtual bool PostEvent(cIGZSndEventStd* event) = 0;            // [9]  +0x48 queued dispatch
    virtual bool PostEventIds(std::uint32_t id, std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d) = 0;  // [10] +0x50
    virtual bool PostEventUnknown(std::uint32_t id, cIGZUnknown* u, std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d) = 0;  // [11] +0x58
    virtual bool PostEventString(std::uint32_t id, cIGZString* s, std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d) = 0;    // [12] +0x60
    virtual bool EventCreate(std::uint32_t classId, void** out) = 0;   // [13] +0x68
    // [14] +0x70: creates an event of class `classId` carrying the four payload integers.
    virtual bool EventCreateWithData(std::uint32_t eventId, std::uint32_t classId, void** out,
                                     std::uint32_t data1, std::uint32_t data2, std::uint32_t data3, std::uint32_t data4) = 0;
    virtual bool RegisterEventHandler(cIGZSndEventHandler* handler) = 0;     // [15] +0x78
    virtual bool UnregisterEventHandler(cIGZSndEventHandler* handler) = 0;   // [16] +0x80
    virtual bool Update(std::uint32_t elapsedMs) = 0;              // [17] +0x88
    virtual bool GetInitFlag() const = 0;                          // [18] +0x90
    virtual void SetDataDirectory(const cIGZString& directory) = 0;   // [19] +0x98
    virtual cIGZString* DataDirectory() = 0;                       // [20] +0xa0
    virtual bool GetEnabledFlag() const = 0;                       // [21] +0xa8
    virtual void SetEnabledFlag(bool enabled) = 0;                 // [22] +0xb0
    virtual bool SetExtensionDefaultDataSourceClassId(const cIGZString& extension, std::uint32_t classId) = 0;  // [23] +0xb8
    virtual bool GetExtensionDefaultDataSourceClassId(const cIGZString& extension, std::uint32_t& classId) = 0; // [24] +0xc0
    virtual std::uint32_t GetNumExtensionDefaultDataSourceClassIdMappings() = 0;   // [25] +0xc8
    virtual bool GetExtensionDefaultDataSourceClassIdMappingClassId(std::uint32_t index, std::uint32_t& classId) = 0;   // [26] +0xd0
    virtual bool GetExtensionDefaultDataSourceClassIdMappingExtension(std::uint32_t index, cIGZString& extension) = 0;  // [27] +0xd8
    virtual std::int32_t GetMasterVolume() const = 0;              // [28] +0xe0
    virtual bool SetMasterVolume(std::int32_t volume) = 0;         // [29] +0xe8
    // [30] +0xf0 / [31] +0xf8: periodic Update(int) registration.
    virtual bool RegisterTarget(cIGZSndUpdateTarget* target, SndCallbackInterval interval) = 0;
    virtual bool UnregisterTarget(cIGZSndUpdateTarget* target) = 0;
    // [32] +0x100: calls `function(target, arg, userData)` for every registered target until it
    // returns false.
    virtual bool EnumerateTargets(UpdateTargetFunction function, std::uint32_t arg, void* userData) = 0;
    virtual bool EnableUpdates() = 0;                              // [33] +0x108
    virtual bool DisableUpdates() = 0;                             // [34] +0x110
    virtual bool RegisterRenderer(cIGZSndRenderer* renderer) = 0;      // [35] +0x118
    virtual bool UnregisterRenderer(cIGZSndRenderer* renderer) = 0;    // [36] +0x120
    virtual bool IsRegisteredRenderer(cIGZSndRenderer* renderer) = 0;  // [37] +0x128
    virtual std::uint32_t NumRenderers() = 0;                      // [38] +0x130
    virtual bool FindPlayingGZSnd(std::uint32_t id, std::uint32_t iid, void** out) = 0;   // [39] +0x138
    virtual bool IsSndPlaying(std::uint32_t id) = 0;               // [40] +0x140
    virtual cIGZCriticalSection* GetCriticalSection() = 0;         // [41] +0x148
    virtual bool GetSndPropertyCacheByIid(cIGZSndPropertyCache** out) = 0;     // [42] +0x150
    virtual cIGZSndPropertyCache* GetSndPropertyCache() = 0;       // [43] +0x158
    virtual bool GetSndInstanceCacheByIid(cIGZSndInstanceCache** out) = 0;     // [44] +0x160
    virtual cIGZSndInstanceCache* GetSndInstanceCache() = 0;       // [45] +0x168
    virtual void AddToMainlineReleaseList(cIGZUnknown* object) = 0;    // [46] +0x170
    virtual void Panic() = 0;                                      // [47] +0x178 stops everything at once
    virtual void SetTestMode(bool testMode) = 0;                   // [48] +0x180
    virtual bool GetTestMode() const = 0;                          // [49] +0x188
    virtual std::uint32_t GetSoundSystemThreadId() = 0;            // [50] +0x190
};

constexpr std::uint32_t kGZSID_cGZSndSys = 0x23cecba0;
constexpr std::uint32_t kGZIID_cIGZSndSys = 0x106077d2;
