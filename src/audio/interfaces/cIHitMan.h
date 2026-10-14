// cIHitMan — public interface of the Hit audio manager. Slots 3..46 (+0x18..+0x170), taken from
// the third sub-vtable (offset-to-top -40) of cHitMan @ 0x100ab6640. Service id 0x1b2ce671,
// interface id 0xbb1bd9a8. See notes/audio-integration.md for the semantics callers must rely on.
#pragma once
#include <cstdint>

#include "audio/interfaces/cIHitAppDataRequestHandler.h"
#include "audio/interfaces/cIHitControlGroup.h"
#include "audio/interfaces/cIHitList.h"
#include "audio/interfaces/cIHitSem.h"
#include "audio/interfaces/cIHitTrackDataBlock.h"
#include "gz/cIGZPersistDBSegment.h"

class cIHitTrackPlayer;  // interface of cHitTrackPlayer (recovered with that class)

class cIHitMan : public cIGZUnknown {
public:
    using TrackFlowErrorHandler = void (*)(cIHitTrackPlayer* player, const char* message);

    virtual bool Init() = 0;                                                          // [3]
    virtual bool Shutdown() = 0;                                                      // [4]
    virtual void UpdateView() = 0;                                                    // [5]
    virtual bool Pause() = 0;                                                         // [6]
    virtual bool Unpause() = 0;                                                       // [7]
    virtual bool IsPaused() const = 0;                                                // [8]
    virtual bool LoadHitFile(const cIGZString& path) = 0;                             // [9]
    virtual bool UnloadHitFile(const cIGZString& path) = 0;                           // [10]
    virtual bool RegisterTrackDataBlock(cIHitTrackDataBlock* block) = 0;              // [11]
    virtual bool RemoveTrackDataBlock(cIHitTrackDataBlock* block) = 0;                // [12]
    virtual bool GetTrackDataBlockAndOffset(std::uint32_t entryKey, cIHitTrackDataBlock** outBlock,
                                            std::uint32_t& outOffset) = 0;            // [13]
    virtual const std::uint8_t* TrackDataEntryPoint(std::uint32_t entryKey) const = 0;  // [14]
    virtual bool CreateTrackPlayer(std::uint32_t iid, void** out) = 0;                // [15]
    virtual bool GetHitList(std::uint32_t listId, std::uint32_t ignored, void** out) = 0;  // [16]
    virtual bool AllocChannel(std::uint32_t iid, void** out) = 0;                     // [17]
    virtual void FreeChannel(cIGZSnd* channel) = 0;                                   // [18]
    virtual bool RegisterSem(cIHitSem* sem) = 0;                                      // [19]
    virtual bool RemoveSem(std::uint32_t semId) = 0;                                  // [20]
    virtual bool RemoveAllSems() = 0;                                                 // [21]
    virtual bool GetSem(std::uint32_t semId, std::uint32_t iid, void** out) const = 0;  // [22]
    virtual cIHitSem* GetSem(std::uint32_t semId) = 0;                                // [23]
    virtual std::uint32_t NumSems() = 0;                                              // [24]
    virtual cIHitSem* GetSemAt(std::uint32_t index) = 0;                              // [25]
    virtual std::int32_t Random(std::int32_t upper, std::int32_t lower) = 0;          // [26] lower + rand(upper-lower)
    virtual bool Callback(std::int32_t, std::int32_t) = 0;                            // [27] returns false
    virtual void SetTrackFlowErrorHandler(TrackFlowErrorHandler handler) = 0;         // [28]
    virtual void HandleTrackFlowError(cIHitTrackPlayer* player, const char* message) = 0;  // [29]
    virtual bool SetRegisterVal(std::int32_t id, std::int32_t value) = 0;             // [30]
    virtual std::int32_t GetRegisterVal(std::int32_t id) const = 0;                   // [31]
    virtual bool GetSourceDataField(std::int32_t sourceId, std::int32_t fieldId, std::int32_t* out) const = 0;  // [32]
    virtual bool RegisterSourceDataRequestHandler(cIHitAppDataRequestHandler* handler) = 0;  // [33]
    virtual cIHitAppDataRequestHandler* GetSourceDataRequestHandler() = 0;            // [34]
    virtual cIHitControlGroup* ControlGroup(std::int32_t groupId) = 0;                // [35] creates on demand
    virtual bool AddToControlGroup(std::int32_t groupId, std::int32_t trackId) = 0;   // [36]
    virtual void SetSequenceGroupSnd(std::uint32_t groupId, cIGZSnd* snd) = 0;        // [37]
    virtual bool GetSequenceGroupSnd(std::uint32_t groupId, cIGZSnd** out) = 0;       // [38]
    virtual void DuckMapSetSndobPri(const cIGZSnd* snd, std::int32_t priority) = 0;   // [39]
    virtual std::int32_t DuckMapSndobPri(const cIGZSnd* snd) = 0;                     // [40]
    virtual std::int32_t DuckMapMaxPri() = 0;                                         // [41]
    virtual void DuckMapRemoveAll() = 0;                                              // [42]
    virtual void AddDBSegment(std::uint32_t segmentId, cIGZPersistDBSegment* segment) = 0;  // [43]
    virtual bool FindDBSegment(std::uint32_t segmentId, std::uint32_t iid, void** out) = 0;  // [44]
    virtual void RemoveDBSegment(std::uint32_t segmentId) = 0;                        // [45]
    virtual bool AddTypeToGroupAssociation(std::uint32_t resourceType, std::uint32_t group) = 0;  // [46]
};

constexpr std::uint32_t kGZIID_cIHitMan = 0xbb1bd9a8;
constexpr std::uint32_t kServiceId_HitMan = 0x1b2ce671;
