// cGZSndMgr — the sound manager service: the registry of sounds by id, sound creators, data
// source creators and data sources, system sounds and the autoplay list. Recovered from Sim City 4
// Deluxe Editionsub 1.2.1 (vtable 0x100ab62e0, 0x158-byte object, ctor @ 0x1004a03c0, Create
// @ 0x10043ca52; service id 0x1af85bfb, priority 0x7a11e).
//
// Bases/offsets as in the binary: cIGZSystemService +0, cRZUnknown +8 (refcount +0x10),
// cIGZSndMgr +0x28 (interface id 0x3af85bb7), cIGZSndUpdateTarget +0x30. Everything is guarded
// by one critical section; the containers carry "iteration guards" in the original that this
// port does not need (callbacks run on snapshots).
//
// Preserved behaviours: GetSound fails while the sound system has no renderer; Shutdown clears
// the sounds, creators and autoplay list but not the registered data sources; the update tick
// (every 250 ms) retires at most one finished autoplay sound; RegisterSnd and
// RegisterSndDataSource keep the first registration of an id.
#pragma once
#include <cstdint>
#include <list>
#include <map>
#include <mutex>
#include <set>
#include <unordered_map>

#include "audio/interfaces/cIGZSndMgr.h"
#include "audio/interfaces/cIGZSndSys.h"
#include "audio/interfaces/cIGZSndUpdateTarget.h"
#include "gz/cIGZPersistRecord.h"
#include "rz/cRZAutoRefCount.h"
#include "rz/cRZSystemService.h"

class cGZSndMgr final : public cRZSystemService, public cIGZSndMgr, public cIGZSndUpdateTarget {
public:
    static constexpr std::uint32_t kServiceId = 0x1af85bfb;
    static constexpr std::int32_t kServicePriority = 0x7a11e;
    static constexpr std::uint32_t kUpdateIntervalMs = 0xfa;     // RegisterTarget interval

    cGZSndMgr();                                                 // @ 0x1004a03c0
    ~cGZSndMgr() override;                                       // @ 0x1004a1370
    static cGZSndMgr* Create() { return new cGZSndMgr(); }       // @ 0x10043ca52

    // ids: 0x3af85bb7 -> cIGZSndMgr; others as cRZSystemService.
    bool QueryInterface(std::uint32_t iid, void** out) override;       // @ 0x1004a1730
    std::uint32_t AddRef() override { return cRZSystemService::AddRef(); }   // @ 0x1004a1850
    std::uint32_t Release() override;                            // @ 0x1004a18d0 (shuts down at count 1)

    // --- service ---
    bool Init() override;                                        // @ 0x1004a1970
    bool Shutdown() override;                                    // @ 0x1004a1c60

    // --- cIGZSndUpdateTarget / primary vtable slot 16 ---
    void Update(std::int32_t elapsedMs) override;                // @ 0x1004a9f00

    // --- cIGZSndMgr ---
    bool GetResource(const cGZPersistResourceKey& key, std::uint32_t iid, void** out) override;   // @ 0x1004a46c0
    void SetSystemSndVol(std::int32_t volume) override { systemSndVolume_ = volume; }   // @ 0x1004a2440
    std::int32_t GetSystemSndVol() const override { return systemSndVolume_; }          // @ 0x1004a2480
    std::uint32_t SystemSndID(std::int32_t index) override;      // @ 0x1004a24c0
    bool SetSystemSndID(std::int32_t index, std::uint32_t id) override;   // @ 0x1004a2830
    bool SetDefaultSndDataSourcePlayer2DClassId(std::uint32_t classId) override { default2D_ = classId; return true; }   // @ 0x1004ae790
    bool GetDefaultSndDataSourcePlayer2DClassId(std::uint32_t& classId) const override { classId = default2D_; return true; }   // @ 0x1004ae7e0
    bool SetDefaultSndDataSourcePlayer3DClassId(std::uint32_t classId) override { default3D_ = classId; return true; }   // @ 0x1004ae840
    bool GetDefaultSndDataSourcePlayer3DClassId(std::uint32_t& classId) const override { classId = default3D_; return true; }   // @ 0x1004ae890
    void RegisterSnd(cIGZSnd* snd, std::uint32_t id) override;   // @ 0x1004a6260
    void UnregisterSndByPointer(cIGZSnd* snd) override;          // @ 0x1004a8600
    void UnregisterSndById(std::uint32_t id) override;           // @ 0x1004a8180
    bool RegisterSndCreator(cIGZSoundCreator* creator) override;   // @ 0x1004a47b0
    bool RemoveSndCreator(cIGZSoundCreator* creator) override;   // @ 0x1004a52e0
    bool GetSoundById(std::uint32_t id, std::uint32_t flags, cIGZVariant* variant, std::uint32_t iid, void** out) override;   // @ 0x1004a5a50
    bool GetSoundByName(cIGZString& name, std::uint32_t flags, cIGZVariant* variant, std::uint32_t iid, void** out) override;   // @ 0x1004a5970
    bool SndAutoPlayWithVolume(std::uint32_t id, std::int32_t volume) override;   // @ 0x1004a9530
    bool SndAutoPlay(std::uint32_t id) override { return SndAutoPlayWithVolume(id, systemSndVolume_); }   // @ 0x1004a9a60
    bool SystemSndAutoPlayWithVolume(std::uint32_t index, std::int32_t volume) override;   // @ 0x1004a9ab0
    bool SystemSndAutoPlay(std::uint32_t index) override { return SystemSndAutoPlayWithVolume(index, systemSndVolume_); }   // @ 0x1004a9eb0
    bool AddSndToAutoPlayList(cIGZSnd* snd) override;            // @ 0x1004a8b40
    bool GetSndDataSource(std::uint32_t id, std::uint32_t flags, cIGZVariant* variant, std::uint32_t iid, void** out) override;   // @ 0x1004ac060
    bool RegisterSndDataSourceCreator(cIGZSndDataSourceCreator* creator, std::uint32_t id) override;   // @ 0x1004aa300
    bool RemoveSndDataSourceCreatorByPointer(cIGZSndDataSourceCreator* creator) override;   // @ 0x1004ab100
    bool RemoveSndDataSourceCreatorById(std::uint32_t id) override;   // @ 0x1004ab840
    bool GetSndDataSourceCreator(std::uint32_t id, std::uint32_t iid, void** out) override;   // @ 0x1004abc50
    void RegisterSndDataSource(cIGZSndDataSource* source, std::uint32_t id) override;   // @ 0x1004acab0
    void UnregisterSndDataSourceByPointer(cIGZSndDataSource* source) override;   // @ 0x1004ae0d0
    void UnregisterSndDataSourceById(std::uint32_t id) override;   // @ 0x1004add00

    // --- inspection ---
    std::size_t registeredSoundCount() const { return sounds_.size(); }
    std::size_t autoPlayCount() const { return autoPlay_.size(); }
    std::size_t soundCreatorCount() const { return soundCreators_.size(); }
    std::size_t dataSourceCreatorCount() const { return dataSourceCreators_.size(); }
    std::size_t dataSourceCount() const { return dataSources_.size(); }
    bool initialised() const { return initialised_; }

private:
    mutable std::recursive_mutex lock_;                          // +0xb0 critical section
    bool initialised_ = false;                                   // +0x38
    cRZAutoRefCount<cIGZSndSys> soundSystem_;                    // +0x40
    cRZAutoRefCount<cIGZPersistRecordService> persist_;          // +0x48
    std::int32_t systemSndVolume_ = 0x400;                       // +0x50
    std::unordered_map<std::uint32_t, cRZAutoRefCount<cIGZSnd>> sounds_;   // +0x58
    std::unordered_map<std::int32_t, std::uint32_t> systemSounds_;         // +0x88
    std::list<cRZAutoRefCount<cIGZSoundCreator>> soundCreators_;           // +0xb8
    std::list<cRZAutoRefCount<cIGZSnd>> autoPlay_;                         // +0xd8
    std::map<std::uint32_t, cRZAutoRefCount<cIGZSndDataSourceCreator>> dataSourceCreators_;   // +0xf8
    std::map<std::uint32_t, cRZAutoRefCount<cIGZSndDataSource>> dataSources_;                 // +0x118
    std::set<std::uint32_t> unusedIds_;                          // +0x138 (cleared by Shutdown, never filled)
    std::uint32_t default2D_ = 0;                                // +0x150
    std::uint32_t default3D_ = 0;                                // +0x154
};
