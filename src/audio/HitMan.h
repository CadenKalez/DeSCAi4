// cHitMan — the Hit audio manager (service 0x1b2ce671). Owns the loaded track bytecode blocks,
// the emanation-model (sem) registry, control groups, the duck map and bookkeeping lists, and
// creates track players and channels. Recovered from Sim City 4 Deluxe Editionsub 1.2.1
// (vtable 0x100ab6640, 0x230-byte object; ctor @ 0x1004be428).
//
// Layout of the original: +0 cIGZSystemService (via cRZSystemService), +8 cRZUnknown,
// +0x28 cIHitMan subobject. The port keeps that base order so the cIHitMan pointer is `this + 0x28`
// in the same way (tests/layout checks), see notes/audio-integration.md.
#pragma once
#include <cstdint>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <unordered_set>

#include "audio/HitControlGroup.h"
#include "audio/HitManServices.h"
#include "audio/HitSem.h"
#include "audio/HitTrackDataBlock.h"
#include "audio/interfaces/cIHitMan.h"
#include "rz/cRZAutoRefCount.h"
#include "rz/cRZRandom.h"
#include "rz/cRZSystemService.h"

class cHitMan;

namespace RZ {
// RZ::HM() @ 0x100127ee4 resolves the Hit manager service. The port registers the instance here:
// cHitMan::Init() sets it when none is registered yet, the destructor clears it.
void SetHitMan(cHitMan* manager);
cHitMan* HitManInstance();
}  // namespace RZ

class cHitMan final : public cRZSystemService, public cIHitMan {
public:
    cHitMan();                                          // @ 0x1004be428
    ~cHitMan() override;                                // @ 0x1004be7d6

    // --- cIGZUnknown. The id 1 and kGZIID_cIHitMan both return the cIHitMan subobject. ---
    bool QueryInterface(std::uint32_t iid, void** out) override;  // @ 0x1004bed56
    std::uint32_t AddRef() override { return cRZSystemService::AddRef(); }
    std::uint32_t Release() override { return cRZSystemService::Release(); }

    // --- cRZSystemService ---
    std::int32_t GetServicePriority() const override { return 499999; }  // @ 0x1004c2506
    bool Init() override;                               // @ 0x1004bede6
    bool Shutdown() override;                           // @ 0x1004bf27c

    // --- cHitMan's own virtuals, vtable order (cIHitMan overrides marked) ---
    bool Pause() override;                                                            // @ 0x1004bf790
    bool Unpause() override { return true; }                                          // @ 0x1004bf7b8
    bool IsPaused() const override { return paused_; }                                // @ 0x1004c251a
    bool RegisterTrackDataBlock(cIHitTrackDataBlock* block) override;                 // @ 0x1004c01b4
    bool RemoveTrackDataBlock(cIHitTrackDataBlock* block) override;                   // @ 0x1004c02ea
    bool GetTrackDataBlockAndOffset(std::uint32_t entryKey, cIHitTrackDataBlock** outBlock,
                                    std::uint32_t& outOffset) override;               // @ 0x1004c0430
    const std::uint8_t* TrackDataEntryPoint(std::uint32_t entryKey) const override;   // @ 0x1004c04ca
    bool LoadHitFile(const cIGZString& path) override;                                // @ 0x1004c06ea
    bool UnloadHitFile(const cIGZString& path) override;                              // @ 0x1004c1320
    virtual bool FilepathHasEmbeddedKey(const cIGZString& path, cGZPersistResourceKey& key);  // @ 0x1004c0c9c
    virtual bool ParseHitFileFromIGZPersistRecordAsIGZFile(const cGZPersistResourceKey& key);  // @ 0x1004c0e8a
    // The original takes a cIGZFile&; the port takes the file's path and bytes (see HitManServices.h).
    virtual bool ParseHitFile(const cIGZString& fileName, const std::uint8_t* bytes, std::uint32_t size);  // @ 0x1004c09ee
    bool CreateTrackPlayer(std::uint32_t iid, void** out) override;                   // @ 0x1004bf4b4
    bool AllocChannel(std::uint32_t iid, void** out) override;                        // @ 0x1004bfa7a
    void FreeChannel(cIGZSnd* channel) override;                                      // @ 0x1004bfb3c
    bool RegisterSem(cIHitSem* sem) override;                                         // @ 0x1004c1e76
    bool RemoveSem(std::uint32_t semId) override;                                     // @ 0x1004c1fc2
    bool RemoveAllSems() override;                                                    // @ 0x1004c2022
    bool GetSem(std::uint32_t semId, std::uint32_t iid, void** out) const override;   // @ 0x1004c1d24
    cIHitSem* GetSem(std::uint32_t semId) override;                                   // @ 0x1004c1d84
    std::uint32_t NumSems() override { return static_cast<std::uint32_t>(sems_.size()); }  // @ 0x1004c2524
    cIHitSem* GetSemAt(std::uint32_t index) override;                                 // @ 0x1004c1e10
    void UpdateView() override;                                                       // @ 0x1004bf576
    bool GetHitList(std::uint32_t listId, std::uint32_t ignored, void** out) override;  // @ 0x1004bf956
    void DuckMapSetSndobPri(const cIGZSnd* snd, std::int32_t priority) override;      // @ 0x1004bfbae
    std::int32_t DuckMapSndobPri(const cIGZSnd* snd) override;                        // @ 0x1004bfdb4
    std::int32_t DuckMapMaxPri() override;                                            // @ 0x1004bfd48
    void DuckMapRemoveAll() override;                                                 // @ 0x1004bfe24
    void SetSequenceGroupSnd(std::uint32_t groupId, cIGZSnd* snd) override;           // @ 0x1004bf65a
    bool GetSequenceGroupSnd(std::uint32_t groupId, cIGZSnd** out) override;          // @ 0x1004bf5be
    bool RegisterSourceDataRequestHandler(cIHitAppDataRequestHandler* handler) override;  // @ 0x1004c0082
    cIHitAppDataRequestHandler* GetSourceDataRequestHandler() override { return sourceDataHandler_.get(); }  // @ 0x1004c2530
    bool GetSourceDataField(std::int32_t sourceId, std::int32_t fieldId, std::int32_t* out) const override;  // @ 0x1004bfe92
    virtual bool AddToActiveTrackList(cIGZSnd* snd);                                  // @ 0x1004bf8de
    virtual bool RemoveFromActiveTrackList(const cIGZSnd* snd);                       // @ 0x1004bf922
    virtual std::list<cIGZSnd*>* ActiveTrackList() { return activeTracksLive_ ? &activeTracks_ : nullptr; }  // @ 0x1004c253e
    void SetTrackFlowErrorHandler(TrackFlowErrorHandler handler) override { flowErrorHandler_ = handler; }  // @ 0x1004be3e4
    void HandleTrackFlowError(cIHitTrackPlayer* player, const char* message) override;  // @ 0x1004be400
    bool SetRegisterVal(std::int32_t id, std::int32_t value) override;                // @ 0x1004bf854
    std::int32_t GetRegisterVal(std::int32_t id) const override;                      // @ 0x1004bf7c8
    bool AddToControlGroup(std::int32_t groupId, std::int32_t trackId) override;      // @ 0x1004c011a
    std::int32_t Random(std::int32_t upper, std::int32_t lower) override;             // @ 0x1004c2548
    bool Callback(std::int32_t, std::int32_t) override { return false; }              // @ 0x1004c2568
    void AddDBSegment(std::uint32_t segmentId, cIGZPersistDBSegment* segment) override;  // @ 0x1004c199c
    bool FindDBSegment(std::uint32_t segmentId, std::uint32_t iid, void** out) override;  // @ 0x1004c1ad2
    void RemoveDBSegment(std::uint32_t segmentId) override;                           // @ 0x1004c1b32
    bool AddTypeToGroupAssociation(std::uint32_t resourceType, std::uint32_t group) override;  // @ 0x1004c1b8e
    cIHitControlGroup* ControlGroup(std::int32_t groupId) override;                   // @ 0x1004bfebe

    // Default handler installed by the constructor (HandleTrackFlowControlError @ 0x1004be3b9).
    static void DefaultTrackFlowErrorHandler(cIHitTrackPlayer* player, const char* message);

private:
    static std::string ResolveAgainst(const std::string& name, const std::string& baseDirectory);
    void ReleaseBlockBuffer(cIHitTrackDataBlock* block);

    bool initialized_ = false;                                       // +0x30
    bool paused_ = false;                                            // +0x31 (never written in the binary)
    bool hasSoundSystem_ = false;                                    // +0x48 != nullptr
    std::list<cIGZSnd*> activeTracks_;                               // +0x50 (heap list in the original)
    bool activeTracksLive_ = false;
    cRZAutoRefCount<cIGZUnknown> propertySetResource_;               // +0x78
    cRZAutoRefCount<cIGZUnknown> trackDefaultsResource_;             // +0x88
    cRZAutoRefCount<cIGZUnknown> channelDefaultsResource_;           // +0x98
    cRZAutoRefCount<cIHitAppDataRequestHandler> sourceDataHandler_;  // +0xa8
    std::map<const cIGZSnd*, std::int32_t> duckMap_;                 // +0xb0
    std::list<cIGZSnd*> channels_;                                   // +0xc8 allocated channels
    std::map<std::uint32_t, cRZAutoRefCount<cIHitSem>> sems_;        // +0xe0
    std::list<cRZAutoRefCount<cIHitTrackDataBlock>> blocks_;         // +0xf8 (newest first)
    std::unordered_set<const std::uint8_t*> ownedBuffers_;           // buffers allocated by ParseHitFile
    std::map<std::uint32_t, cRZAutoRefCount<cIGZSnd>> sequenceSnds_; // +0x118
    cRZRandom random_{0xFFFFFFFFu};                                  // +0x130
    TrackFlowErrorHandler flowErrorHandler_ = &DefaultTrackFlowErrorHandler;  // +0x148
    std::map<std::int32_t, cIHitControlGroup*> controlGroups_;       // +0x168 (each holds one reference)
    std::map<std::uint32_t, cRZAutoRefCount<cIGZPersistDBSegment>> dbSegments_;  // +0x180
    std::map<std::uint32_t, std::list<std::uint32_t>> typeGroups_;   // +0x198
    std::map<std::int32_t, std::int32_t> registers_;                 // port: see notes (original +0x80 is null)
    cHitSem2dDefault sem2dDefault_;                                  // +0x1b0
    cHitSem3dDefault sem3dDefault_;                                  // +0x1f0
};

// Installs the body of the default track-flow error handler (supplied by cHitTrackPlayer).
void RegisterDefaultTrackFlowErrorHandler(cIHitMan::TrackFlowErrorHandler body);
