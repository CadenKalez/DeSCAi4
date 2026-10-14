// cFreshCellPlayer — plays one cell of a score: owns the sound, applies randomised volume/pitch,
// loop counting and fade-out. Recovered from Sim City 4 Deluxe Editionsub 1.2.1
// (vtable 0x100ab7508, 0x88-byte object).
//
// Fidelity notes
//  * The original also derives from cGZFileNameToResKeyConverterDefault (a file-name <-> resource-key
//    helper whose methods this class never overrides); that base is not reproduced.
//  * The external-sound-system start path (StartCellExternal) is not implemented: nothing in the
//    shipped game ever installs an external sound system.
//  * The original leaves the effective volume uninitialised until the first Start(); here it is 0.
#pragma once
#include <cstdint>

#include "audio/FreshCell.h"
#include "audio/FreshHost.h"
#include "audio/interfaces/cIExternalSoundSystem.h"
#include "audio/interfaces/cIFreshCellPlayer.h"
#include "rz/cRZRandom.h"

class cFreshCellPlayer;

// What a cell player needs from its owning player (the original reads these fields directly).
class cIFreshCellPlayerOwner {
public:
    virtual cRZRandom& Random() = 0;                   // owner +0x18
    virtual std::int32_t MasterVolume() const = 0;     // owner +0x20 (1024 = unity)
    virtual std::int32_t ScoreVolumeScale() const = 0; // owner score header +0x0c
};

class cFreshCellPlayer final : public cIFreshCellPlayer {
public:
    enum State : std::int32_t { kIdle = 0, kPlaying = 2, kFadingOut = 3 };

    explicit cFreshCellPlayer(cIFreshCellPlayerOwner* owner = nullptr) : owner_(owner) {}
    // cFreshCellPlayer::Create() @ 0x10043ac9a
    static cFreshCellPlayer* Create() { return new cFreshCellPlayer(); }
    virtual ~cFreshCellPlayer();

    bool QueryInterface(std::uint32_t iid, void** out) override;  // cIGZUnknown(1) or kGZIID_cIFreshCellPlayer
    std::uint32_t AddRef() override { return ++refCount_; }
    std::uint32_t Release() override;  // deletes when the count is < 2

    cIFreshCell* GetCell() override { return cell_; }
    void SetCell(cIFreshCell* cell) override { cell_ = static_cast<cFreshCell*>(cell); }
    bool Start() override;
    void Update() override;
    bool Stop() override;
    void Kill() override;
    void SetVol(std::int32_t volume) override;
    bool IsPlaying() override { return state_ == kPlaying; }
    bool IsBusy() override { return state_ != kIdle; }
    bool IsDead() override { return dead_; }
    void SetDead(bool dead) override { dead_ = dead; }
    void AutoPan() override;
    void SetHeadRelative(bool headRelative) override { headRelative_ = headRelative; }
    void Set3DPosition(std::int32_t x, std::int32_t y, std::int32_t z) override {
        has3DPosition_ = true; posX_ = x; posY_ = y; posZ_ = z;
    }
    void SetMinMaxDistance(std::uint32_t minimum, std::uint32_t maximum) override {
        minDistance_ = minimum; maxDistance_ = maximum;
    }
    void SetExternalSoundSystemInterface(cIExternalSoundSystem* system) override;
    bool GetHeadRelative() override { return headRelative_; }
    std::int32_t Get3DPosX() override { return posX_; }
    std::int32_t Get3DPosY() override { return posY_; }
    std::int32_t Get3DPosZ() override { return posZ_; }

    // --- state used by the owning cFreshPlayer (direct field access in the original) ---
    bool markedForRemoval() const { return markedForRemoval_; }
    void SetMarkedForRemoval(bool marked) { markedForRemoval_ = marked; }
    State state() const { return state_; }
    void SetOwner(cIFreshCellPlayerOwner* owner) { owner_ = owner; }

    // cFreshCellPlayer::FilenameHasGuid(cIGZString&) @ 0x1004cb85c — true if the name holds a run of
    // eight hex digits (an embedded resource id).
    static bool FilenameHasGuid(const cIGZString& fileName);

    static std::int32_t SoundsLoadedCount();  // g_lNumSndsLoaded

private:
    void RecomputeEffectiveVolume();
    void ApplyPitchShift();

    std::int32_t refCount_ = 0;
    cIFreshCellPlayerOwner* owner_;
    cIGZSnd* snd_ = nullptr;                       // +0x30, AddRef'd
    State state_ = kIdle;                          // +0x38
    cFreshCell* cell_ = nullptr;                   // +0x40 (not owned)
    std::int32_t loopCountdown_ = 0;               // +0x48
    bool markedForRemoval_ = false;                // +0x4d (set by the player; counted by NumCellsDying)
    std::int32_t randomizedVolume_ = 0x400;        // +0x50
    std::int32_t volume_ = 0x400;                  // +0x54
    std::int32_t effectiveVolume_ = 0;             // +0x58
    bool has3DPosition_ = false;                   // +0x5c
    bool headRelative_ = true;                     // +0x5d (ctor writes 0x100 to +0x5c: [5c]=0,[5d]=1)
    std::int32_t posX_ = 0, posY_ = 0, posZ_ = 100;  // +0x60/+0x64/+0x68
    std::uint32_t minDistance_ = 1000;             // +0x6c
    std::uint32_t maxDistance_ = 100;              // +0x70
    cIExternalSoundSystem* external_ = nullptr;    // +0x78
    bool dead_ = false;                            // +0x80
};
