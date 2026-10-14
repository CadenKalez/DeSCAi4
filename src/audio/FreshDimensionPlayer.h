// cFreshDimensionPlayer — plays several Freshness scores at once ("dimensions"). Each score has its
// own cFreshPlayer (created through COM); this class pools every cell player of the enabled
// scores and, every few seconds, starts one at random (weighted by cell probability), keeping at
// most four running. Recovered from Sim City 4 Deluxe Editionsub 1.2.1 (vtable 0x100ab7960,
// 0x1f0-byte object).
//
// Bases: cIFreshDimensionPlayer (primary), cIFreshPlayer (+8), cIGZSndUpdateTarget (+0x10).
// Own virtuals follow the destructors in the primary vtable in the order declared below.
#pragma once
#include <array>
#include <cstdint>
#include <list>
#include <map>

#include "audio/FreshHost.h"
#include "audio/interfaces/cIFreshDimensionPlayer.h"
#include "audio/interfaces/cIGZSndUpdateTarget.h"
#include "rz/cRZAutoRefCount.h"
#include "rz/cRZRandom.h"
#include "rz/cRZTimer.h"

class cFreshDimensionPlayer final : public cIFreshDimensionPlayer, public cIFreshPlayer, public cIGZSndUpdateTarget {
public:
    cFreshDimensionPlayer();
    // cFreshDimensionPlayer::Create() @ 0x10043ac26 — the original returns the cIFreshPlayer subobject.
    static cIFreshPlayer* Create() { return static_cast<cIFreshPlayer*>(new cFreshDimensionPlayer()); }

    // --- cIGZUnknown: accepts cIFreshPlayer, cIFreshDimensionPlayer and cIGZUnknown ids ---
    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return static_cast<std::uint32_t>(++refCount_); }
    std::uint32_t Release() override;

    // --- cIFreshDimensionPlayer ---
    cIFreshPlayer* AsIFreshPlayer() override { return this; }
    bool SetScoreEnabled(std::uint32_t scoreId, bool enabled) override;

    virtual ~cFreshDimensionPlayer();

    // --- own virtuals, vtable order ---
    void Update(std::int32_t elapsedMs) override;                    // cIGZSndUpdateTarget
    bool InitializeFromLoadedScore() override;                       // Init(): grabs the sound system
    bool InitializeFromResourceKey(const cGZPersistResourceKey&) override { return false; }
    bool Shutdown() override { return true; }
    bool LoadScore(const cIGZString& fileName, std::uint32_t scoreId) override;
    cIFreshScore* GetScore(std::uint32_t scoreId) override;
    bool Play() override;
    bool IsPlaying() override { return playing_; }
    bool Stop() override;
    bool SetVolume(std::int32_t volume) override;
    bool FadeVolume(std::int32_t durationMs, std::int32_t targetVolume, bool stopAtEnd) override;
    bool SetPlayPercent(std::int32_t percent) override;
    bool SetTempo(std::int32_t) override { return false; }
    bool SetManual(bool manual) override { manual_ = manual; return true; }
    bool Reseed(std::uint32_t seed) override;
    bool SetNumDivisionsX(std::uint32_t count) override;
    bool SetNumDivisionsZ(std::uint32_t count) override;
    bool SetDivisionEnabled(std::uint32_t x, std::uint32_t z, bool enabled) override;
    bool SetHeadRelative(bool headRelative) override { headRelative_ = headRelative; return true; }
    bool SetDivision3DPosition(std::uint32_t x, std::uint32_t z, std::int32_t px, std::int32_t py, std::int32_t pz) override;
    bool SetMinMaxDistance(std::uint32_t minimum, std::uint32_t maximum) override {
        minDistance_ = minimum; maxDistance_ = maximum; return true;
    }
    bool SetXPos(std::int32_t position) override;
    bool SetYPos(std::int32_t position) override;
    virtual bool SetZPos(std::int32_t position) { zPos_ = position; return true; }
    bool SetZoom(std::int32_t zoom) override;
    std::int32_t GetXPos() override { return xPos_; }
    std::int32_t GetYPos() override { return yPos_; }
    virtual std::int32_t GetZPos() { return zPos_; }
    std::int32_t GetZoom() override { return zoom_; }
    std::int32_t GetFirstSelectableRow() override { return 0; }
    std::int32_t GetLastSelectableRow() override { return 0; }
    std::int32_t GetFirstSelectableColumn() override { return 0; }
    std::int32_t GetLastSelectableColumn() override { return 0; }
    cIFreshCellPlayer* GetCellPlayer(std::uint32_t, std::uint32_t, std::uint32_t) override { return nullptr; }
    void SetKey(const cGZPersistResourceKey& key) override { key_ = key; }
    const cGZPersistResourceKey* GetKey() override { return &key_; }
    void SetExternalSoundSystemInterface(cIExternalSoundSystem*) override {}  // ignored
    virtual void SetPause(bool paused) { paused_ = paused; }

    // Non-virtual helpers of the original.
    void UpdateCellPlayerMap();
    bool CalculateCellplayer3DPosition(cIFreshCellPlayer* cellPlayer);

    // Test/inspection access.
    std::size_t activeCellPlayerCount() const { return active_.size(); }
    std::int32_t pooledCellCount() const { return poolCount_; }
    std::int32_t totalProbabilityWeight() const { return totalWeight_; }
    std::int32_t enabledScoreCount() const { return enabledScores_; }

private:
    template <class F> void ForEachPlayer(F&& f);

    std::int32_t refCount_ = 0;                                  // +0x1c
    bool hasSoundSystem_ = false;                                // +0x20 (the original holds RZ::SS())
    std::map<std::uint32_t, cRZAutoRefCount<cIFreshPlayer>> players_;   // +0x28 score id -> player
    std::map<std::uint32_t, cRZAutoRefCount<cIFreshScore>> scores_;     // +0x40
    std::map<std::uint32_t, bool> enabled_;                      // +0x58
    std::map<std::uint32_t, cIFreshCellPlayer*> pool_;           // +0x70 index -> cell player
    std::map<std::uint32_t, cIFreshCell*> poolCells_;            // +0x88 index -> its cell
    std::list<cIFreshCellPlayer*> active_;                       // +0xa0 currently started (max 4)
    std::uint32_t numDivisionsX_ = 2, numDivisionsZ_ = 2;        // +0xb8/+0xbc
    bool headRelative_ = true;                                   // +0xc0
    std::array<std::array<bool, 3>, 3> divisionEnabled_{};       // +0xc1
    std::array<std::array<std::int32_t, 3>, 3> divisionPosX_{}, divisionPosY_{}, divisionPosZ_{};  // +0xcc/+0xf0/+0x114
    std::uint32_t minDistance_ = 100, maxDistance_ = 1000;       // +0x138/+0x13c
    std::uint32_t lastDivisionX_ = 0, lastDivisionZ_ = 0;        // +0x140/+0x144
    cRZTimer timer_{cRZTimer::kMilliseconds};                    // +0x148
    std::uint32_t playStartTime_ = 0;                            // +0x160
    std::uint32_t lastStartTime_ = 0;                            // +0x164 reset to 0 after every start attempt
    std::int32_t poolCount_ = 0;                                 // +0x168
    std::int32_t totalWeight_ = 0;                               // +0x16c sum of pooled cell probabilities
    std::int32_t enabledScores_ = 0;                             // +0x170
    cRZRandom random_{0xFFFFFFFFu};                              // +0x178
    bool playing_ = false;                                       // +0x194
    bool manual_ = false;                                        // +0x195
    std::int32_t volume_ = 0;                                    // +0x198
    std::int32_t xPos_ = 0, yPos_ = 0, zPos_ = 0;                // +0x19c/+0x1a0/+0x1a4
    std::int32_t zoom_ = 0;                                      // +0x1ac
    std::int32_t playPercent_ = 0;                               // +0x1c4
    bool paused_ = false;                                        // +0x1cc
    bool fadeRequested_ = false;                                 // +0x1cd
    bool stopAtFadeEnd_ = false;                                 // +0x1e0
    cGZPersistResourceKey key_;                                  // +0x1e4
};
