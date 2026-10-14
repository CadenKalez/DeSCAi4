// cFreshPlayer — the "Freshness" adaptive music player. A score is a grid of cells (samples); every
// beat the player decides which cells to start (within a selection area around the X/Y position)
// and which to retire, so the music evolves with the listener's position and activity.
// Recovered from Sim City 4 Deluxe Editionsub 1.2.1 (vtable 0x100ab76c0, 0x8180-byte object).
//
// Ownership (as in the original): the player owns its cFreshScore and every cFreshCellPlayer.
// A single process-wide critical section (g_FreshSection) guards all players.
#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <string>

#include "audio/FreshCellPlayer.h"
#include "audio/FreshScore.h"
#include "audio/interfaces/cIFreshPlayer.h"
#include "audio/interfaces/cIGZSndUpdateTarget.h"
#include "rz/cRZCallbackTimer.h"
#include "rz/cRZCriticalSection.h"
#include "rz/cRZRandom.h"
#include "rz/cRZString.h"
#include "rz/cRZTimer.h"

class cFreshPlayer final : public cIFreshPlayer, public cIGZSndUpdateTarget, public cIFreshCellPlayerOwner {
public:
    cFreshPlayer();
    // cFreshPlayer::Create() @ 0x10043abb2
    static cFreshPlayer* Create() { return new cFreshPlayer(); }

    // --- cIGZUnknown (shared by both interface bases). Only kGZIID_cIFreshPlayer is accepted. ---
    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return static_cast<std::uint32_t>(++refCount_); }
    std::uint32_t Release() override;  // deletes when the count is exactly 1 (refcount starts at 0)

    // --- cIFreshPlayer, vtable order ---
    bool InitializeFromLoadedScore() override;
    bool InitializeFromResourceKey(const cGZPersistResourceKey& key) override;
    bool LoadScore(const cIGZString& fileName, std::uint32_t flags) override;
    cIFreshScore* GetScore(std::uint32_t) override { return score_; }
    bool Shutdown() override;
    bool Play() override;
    bool IsPlaying() override { return playing_; }
    bool Stop() override;
    bool SetVolume(std::int32_t volume) override;
    bool FadeVolume(std::int32_t durationMs, std::int32_t targetVolume, bool stopAtEnd) override;
    bool SetYPos(std::int32_t position) override;
    bool SetXPos(std::int32_t position) override;
    bool SetZoom(std::int32_t zoom) override;
    bool SetPlayPercent(std::int32_t percent) override;
    bool SetTempo(std::int32_t beatsPerMinute) override;
    std::int32_t GetYPos() override { return yPos_; }
    std::int32_t GetXPos() override { return xPos_; }
    std::int32_t GetZoom() override { return zoom_; }
    bool SetManual(bool manual) override;
    std::int32_t GetFirstSelectableRow() override;
    std::int32_t GetLastSelectableRow() override;
    std::int32_t GetFirstSelectableColumn() override;
    std::int32_t GetLastSelectableColumn() override;
    cIFreshCellPlayer* GetCellPlayer(std::uint32_t row, std::uint32_t column, std::uint32_t) override;
    bool Reseed(std::uint32_t seed) override;
    bool SetHeadRelative(bool headRelative) override { headRelative_ = headRelative; return true; }
    bool SetMinMaxDistance(std::uint32_t minimum, std::uint32_t maximum) override {
        minDistance_ = minimum; maxDistance_ = maximum; return true;
    }
    bool SetNumDivisionsX(std::uint32_t count) override;
    bool SetNumDivisionsZ(std::uint32_t count) override;
    bool SetDivisionEnabled(std::uint32_t x, std::uint32_t z, bool enabled) override;
    bool SetDivision3DPosition(std::uint32_t x, std::uint32_t z, std::int32_t px, std::int32_t py, std::int32_t pz) override;
    void SetKey(const cGZPersistResourceKey& key) override { key_ = key; }
    const cGZPersistResourceKey* GetKey() override { return &key_; }
    void SetExternalSoundSystemInterface(cIExternalSoundSystem* system) override;

    virtual ~cFreshPlayer();

    // --- cFreshPlayer's own virtuals, vtable order ---
    virtual bool InitializeWithScoreAndDataDir(cFreshScore* score, const cRZString& dataDir);   // Init(cFreshScore*, cRZString const&)
    virtual bool InitializeFromDirectoryAndFile(const cRZString& directory, const cRZString& fileName);  // Init(dir, file)
    virtual bool InitializeFromPath(const cRZString& path);                                      // Init(path)
    void Update(std::int32_t elapsedMs) override;                                                // cIGZSndUpdateTarget
    virtual bool PlayAtVolume(std::int32_t volume);                                              // Play(int)
    virtual void StopFade();
    virtual bool SetVolumeNoLock(std::int32_t volume);
    virtual bool SetZPos(std::int32_t) { return true; }  // no-op in the original
    virtual std::int32_t GetZPos() { return zPos_; }
    virtual void GetDataDir(cRZString& out) { out = dataDir_; }
    virtual bool SetSelectionAreaMaxDistances(std::int32_t rowRadius, std::int32_t columnRadius);
    virtual bool GetSelectionAreaMaxDistances(std::int32_t& rowRadius, std::int32_t& columnRadius);
    virtual void SetStressLevel(std::int32_t level) { stressLevel_ = level; }
    virtual void SetPause(bool paused) { paused_ = paused; }
    virtual void UpdateListener(std::int32_t x, std::int32_t z) { listenerX_ = x; listenerZ_ = z; }
    virtual bool CellIsInSelectionArea(std::int32_t row, std::int32_t column);
    virtual bool StopNoLock();
    // Window scan from a random start; returns the best ProbOfCellStarting (> 0) cell.
    virtual bool GetCellToAdd(std::int32_t& row, std::int32_t& column, std::int32_t preferredGroup);
    virtual bool GetCellToSubtract(std::int32_t& row, std::int32_t& column);
    virtual void SubtractCell(std::int32_t row, std::int32_t column);
    virtual bool ToggleCell(std::int32_t row, std::int32_t column);
    virtual std::int32_t ProbOfCellStarting(std::int32_t row, std::int32_t column, std::int32_t preferredGroup);
    virtual std::int32_t NumCellsBusy();
    virtual std::int32_t NumCellsDying();
    virtual std::int32_t NumCellsPlaying();
    virtual bool ShowUsage();
    virtual std::int32_t GetMinCellsPlaying();
    virtual std::int32_t GetMaxCellsPlaying();

    // --- cIFreshCellPlayerOwner ---
    cRZRandom& Random() override { return *random_; }
    std::int32_t MasterVolume() const override { return volume_; }
    std::int32_t ScoreVolumeScale() const override { return score_->header.volumeScale; }

    // Non-virtual helpers of the original.
    void UpdatePlayerInfo();
    bool CalculateCellplayer3DPosition(cFreshCellPlayer* cellPlayer);
    void ProcessVolumeFadeTimerCallback();

    // Test/inspection access.
    cFreshCellPlayer* cellPlayerAt(int row, int column) const { return grid_[row][column]; }
    std::int32_t activeCellCount() const { return activeCells_; }
    std::int32_t lastAddedRow() const { return lastAddedRow_; }
    std::int32_t lastAddedColumn() const { return lastAddedColumn_; }

private:
    template <class F> void ForEachCellPlayer(F&& f);
    static cRZCriticalSection& Section();
    static void FadeTimerThunk(void* player);
    void DestroyCellPlayersAndScore();

    bool initialized_ = false;                       // +0x10
    std::int32_t refCount_ = 0;                      // +0x14
    std::unique_ptr<cRZRandom> random_;              // +0x18
    std::int32_t volume_ = 0x400;                    // +0x20
    std::unique_ptr<cRZTimer> timer_;                // +0x28
    cFreshScore* score_ = nullptr;                   // +0x30 (owned)
    bool playing_ = false;                           // +0x38
    bool manual_ = false;                            // +0x39
    std::int32_t tempo_ = 0;                         // +0x3c  beats per minute
    std::int32_t xPos_ = 1, yPos_ = 1, zPos_ = 1;    // +0x40/+0x44/+0x48
    std::int32_t zoom_ = 1;                          // +0x4c
    std::int32_t activeCells_ = 0xf;                 // +0x50 present cells in the selection area
    std::int32_t beat_ = 0;                          // +0x54 1..beatsPerCycle
    std::int32_t beatTotal_ = 1;                     // +0x58 beats since Play (plus offset)
    std::int32_t beatOffset_ = 0;                    // +0x5c
    std::int32_t cachedMinCellsPercentWeight_ = 0;   // +0x60 copy of the score value taken at Init
    std::int32_t cachedMaxCellsAtFullPercent_ = 4;   // +0x64
    std::int32_t groupCellCount_ = 0;                // +0x68 ungrouped cells + distinct groups in the area
    std::int32_t columnRadius_ = 3;                  // +0x6c
    std::int32_t rowRadius_ = 3;                     // +0x70
    std::int32_t lastSubtractedRow_ = 0, lastSubtractedColumn_ = 0;  // +0x74/+0x78
    std::int32_t lastAddedRow_ = 0, lastAddedColumn_ = 0;            // +0x7c/+0x80
    std::int32_t listenerX_ = 1, listenerZ_ = 1;     // +0x84/+0x88
    std::array<std::array<cFreshCellPlayer*, cFreshScore::kMaxColumns>, cFreshScore::kMaxRows> grid_{};  // +0x90
    cRZString dataDir_;                              // +0x8090
    std::int32_t playPercent_ = 100;                 // +0x80b8
    std::int32_t stressLevel_ = 0;                   // +0x80bc
    bool paused_ = false;                            // +0x80c0
    bool fading_ = false;                            // +0x80c1
    std::unique_ptr<cRZCallbackTimer> fadeTimer_;    // +0x80c8
    std::int32_t fadeStep_ = 100;                    // +0x80d0
    std::int32_t fadeTarget_ = 0;                    // +0x80d4
    bool stopAfterFade_ = false;                     // +0x80d8
    std::uint32_t minDistance_ = 100;                // +0x80dc
    std::uint32_t maxDistance_ = 1000;               // +0x80e0
    std::uint32_t lastDivisionX_ = 0, lastDivisionZ_ = 0;   // +0x80e4/+0x80e8
    std::uint32_t numDivisionsX_ = 2, numDivisionsZ_ = 2;   // +0x80ec/+0x80f0
    bool headRelative_ = true;                       // +0x80f4
    std::array<std::array<bool, 3>, 3> divisionEnabled_{};  // +0x80f5
    std::array<std::array<std::int32_t, 3>, 3> divisionPosX_{}, divisionPosY_{}, divisionPosZ_{};  // +0x8100/+0x8124/+0x8148
    cGZPersistResourceKey key_;                      // +0x816c
    cIExternalSoundSystem* external_ = nullptr;      // +0x8178
};
