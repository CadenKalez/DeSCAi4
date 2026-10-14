#include "audio/FreshCellPlayer.h"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <string>

#include "audio/FreshTables.h"
#include "audio/HitRegisters.h"
#include "audio/interfaces/cIGZSnd3D.h"
#include "audio/interfaces/cIGZSndRegisterSet.h"
#include "rz/RZConvert.h"
#include "rz/cRZString.h"

namespace {
std::atomic<std::int32_t> g_numSndsLoaded{0};
constexpr std::int32_t kUnityVolume = 0x400;
// Debug overrides present in the original (_bOverride2 / _bOverride1 / _lSlamQuadrant @ 0x100b6eae0..).
// Nothing in the shipped binary sets them.
bool g_panOverrideRandom = false;
bool g_slamOverride = false;
std::int32_t g_slamQuadrant = 0;
}  // namespace

std::int32_t cFreshCellPlayer::SoundsLoadedCount() { return g_numSndsLoaded.load(); }

// cFreshCellPlayer::~cFreshCellPlayer() @ 0x1004ccab8
cFreshCellPlayer::~cFreshCellPlayer() {
    if (external_ != nullptr) external_->Release();
    if (snd_ != nullptr) snd_->Release();
}

// cFreshCellPlayer::QueryInterface(unsigned int, void**) @ 0x1004cb726
bool cFreshCellPlayer::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIFreshCellPlayer || iid == kGZIID_cIGZUnknown) {
        *out = this;
        AddRef();
        return true;
    }
    return false;
}

// cFreshCellPlayer::Release() @ 0x1004cb6e2
std::uint32_t cFreshCellPlayer::Release() {
    if (refCount_ < 2) {
        delete this;
        return 0;
    }
    return static_cast<std::uint32_t>(--refCount_);
}

// cFreshCellPlayer::SetExternalSoundSystemInterface @ 0x1004cc7d4 — takes a reference.
void cFreshCellPlayer::SetExternalSoundSystemInterface(cIExternalSoundSystem* system) {
    cIExternalSoundSystem* old = external_;
    if (old == system) return;
    if (system != nullptr) system->AddRef();
    external_ = system;
    if (old != nullptr) old->Release();
}

// cFreshCellPlayer::FilenameHasGuid(cIGZString&) @ 0x1004cb85c
bool cFreshCellPlayer::FilenameHasGuid(const cIGZString& fileName) {
    static const char kHex[] = "0123456789abcdefABCDEF";
    const char* s = fileName.ToChar();
    const std::uint32_t n = fileName.Strlen();
    int run = 0;
    for (std::uint32_t i = 0; i < n; ++i) {
        run = std::memchr(kHex, s[i], 22) != nullptr ? run + 1 : 0;
        if (run == 8) return true;
    }
    return false;
}

// volume = (((randomised * cellVolume) * owner volume) >> 20 * score scale * cell-player volume) >> 20
// (the shifts are the original's division by 2^20 with round-toward-zero).
void cFreshCellPlayer::RecomputeEffectiveVolume() {
    auto div20 = [](std::int32_t v) { return (v + ((v >> 31) & 0xfffff)) >> 20; };
    std::int32_t v = randomizedVolume_ * cell_->GetVolume();
    v = div20(v * owner_->MasterVolume());
    v = div20(v * owner_->ScoreVolumeScale() * volume_);
    effectiveVolume_ = v;
}

// cFreshCellPlayer::SetVol(int) @ 0x1004cc5c8
void cFreshCellPlayer::SetVol(std::int32_t volume) {
    volume_ = volume;
    if (snd_ == nullptr) return;
    auto div20 = [](std::int32_t v) { return (v + ((v >> 31) & 0xfffff)) >> 20; };
    std::int32_t v = cell_->GetVolume() * randomizedVolume_;
    effectiveVolume_ = v;
    v = div20(v * owner_->MasterVolume());
    effectiveVolume_ = v;
    v = div20(v * volume * owner_->ScoreVolumeScale());
    effectiveVolume_ = v;
    snd_->SetVolume(effectiveVolume_);
}

// Picks a random semitone offset within [low, high] and scales the sound's frequency by 2^(n/12).
void cFreshCellPlayer::ApplyPitchShift() {
    const std::int32_t low = cell_->GetRandPitchShiftLow();
    const std::int32_t high = cell_->GetRandPitchShiftHigh();
    if ((low == 0 && high == 0) || low > high) return;
    const auto r = static_cast<std::int32_t>(owner_->Random().RandomUint32Uniform());
    const std::int32_t semitones = r % ((high + 1) - low) + low;
    const double base = snd_->GetFrequency();
    std::int32_t index = semitones < 0 ? -semitones : semitones;
    if (index > 12) index = 12;  // the table covers one octave
    const double mult = Fresh::kFrequencyMultipliers[static_cast<std::size_t>(index)];
    snd_->SetFrequency(static_cast<std::int32_t>(semitones < 0 ? base / mult : base * mult));
}

// cFreshCellPlayer::Start() @ 0x1004cb9a8
bool cFreshCellPlayer::Start() {
    Fresh::cIFreshHost* host = Fresh::GetHost();
    if (host == nullptr || !host->HasSoundSystem() || IsDead() || IsBusy()) return false;
    markedForRemoval_ = false;
    if (external_ != nullptr) return false;  // StartCellExternal: not recovered (never used in the game)

    cRZString name;
    name.AssignFromIGZString(*cell_->GetFilename());
    cIGZSnd* created = nullptr;
    if (!FilenameHasGuid(name)) {
        // plain file name: only the final path component is handed to the sound manager
        std::string full = name.str();
        const auto slash = full.find_last_of("/\\");
        cRZString baseName(full.substr(slash == std::string::npos ? 0 : slash + 1).c_str());
        created = host->CreateSndByFileName(baseName);
    } else {
        // "<prefix><8 hex digits>.<ext>": the id is the last eight characters before the first '.'
        cRZString before = name.SplitToken(".");
        const cRZString tail = before.Right(8);
        const std::uint32_t id = RZ::ConvertHexToUint(tail.ToChar());
        cell_->SetSndId(id);
        created = host->CreateSndByResourceId(id);
    }
    if (created == nullptr) return false;
    if (snd_ != nullptr) snd_->Release();
    snd_ = created;
    ++g_numSndsLoaded;

    void* iface = nullptr;
    if (snd_->QueryInterface(kGZIID_cIGZSndRegisterSet, &iface) && iface != nullptr) {
        auto* regs = static_cast<cIGZSndRegisterSet*>(iface);
        regs->SetRegisterValue(0x1c, 1, false);  // Use3DPositioning
        regs->Release();
    }
    void* iface3d = nullptr;
    if (snd_->QueryInterface(kGZIID_cIGZSnd3D, &iface3d) && iface3d != nullptr) {
        auto* snd3d = static_cast<cIGZSnd3D*>(iface3d);
        snd3d->SetHeadRelative(headRelative_);
        snd3d->SetMinimumDistance(static_cast<float>(minDistance_));
        snd3d->SetMaximumDistance(static_cast<float>(maxDistance_));
        if (has3DPosition_) {
            snd3d->SetPosition(static_cast<float>(posX_), static_cast<float>(posY_), static_cast<float>(posZ_));
        }
        snd3d->Release();
    }

    constexpr cIGZSnd::SndDup kDuplicatePolicy = 1;
    if (cell_->GetFadeInTime() == 0) {
        snd_->PlayWithVolumeAndLoop(effectiveVolume_, cell_->GetSampleLooped(), kDuplicatePolicy);
    } else {
        snd_->PlayWithVolumeAndLoop(0, cell_->GetSampleLooped(), kDuplicatePolicy);
        snd_->FadeVolume(0, effectiveVolume_, static_cast<std::uint32_t>(cell_->GetFadeInTime()), true);
    }
    state_ = kPlaying;

    void* updateIface = nullptr;
    if (snd_->QueryInterface(kGZIID_cIGZSndUpdateTarget, &updateIface) && updateIface != nullptr) {
        host->AddUpdateTarget(static_cast<cIGZSndUpdateTarget*>(updateIface), 250);
        // (the original keeps this reference in a local that is released below)
        static_cast<cIGZSndUpdateTarget*>(updateIface)->Release();
    }

    // volume randomisation: 1024 - (|raw rng| % 256) when the cell asks for random volume
    randomizedVolume_ = kUnityVolume;
    if (cell_->GetVolIsRandom()) {
        std::int32_t r = static_cast<std::int32_t>(owner_->Random().RandomUint32Uniform());
        if (r < 0) r = -r;
        randomizedVolume_ -= r % ((randomizedVolume_ + ((randomizedVolume_ >> 31) & 3)) >> 2);
    }
    RecomputeEffectiveVolume();
    snd_->SetVolume(effectiveVolume_);

    if (cell_->GetSampleLooped()) loopCountdown_ = cell_->GetLoopLength();
    ApplyPitchShift();
    return true;
}

// cFreshCellPlayer::Update() @ 0x1004cb76e — called once per beat by the owning player.
void cFreshCellPlayer::Update() {
    if (state_ == kFadingOut) {
        if (external_ == nullptr || external_->IsCellFinished(cell_)) {
            if (snd_ == nullptr) return;
            if (snd_->GetVolume() != 0) return;  // fade not finished yet
            Kill();
            return;  // Kill() already returned the state to idle
        }
        external_->StopCell(static_cast<float>(cell_->GetFadeOutTime()), cell_);
        state_ = kIdle;
        return;
    }
    if (state_ != kPlaying) return;

    if (cell_->GetSampleLooped()) {
        const std::int32_t remaining = --loopCountdown_;
        if (!markedForRemoval_) {
            if (remaining != 0) return;
            loopCountdown_ = cell_->GetLoopLength();  // restart the loop counter
            return;
        }
        if (remaining == 0) return;
        Kill();
        return;
    }
    // one-shot sample: finished when the sound stops playing
    if (external_ == nullptr || external_->IsCellFinished(cell_)) {
        if (snd_ == nullptr) return;
        if (snd_->IsPlaying()) return;
        Stop();
        state_ = kIdle;
        return;
    }
    external_->StopCell(static_cast<float>(cell_->GetFadeOutTime()), cell_);
    state_ = kIdle;
}

// cFreshCellPlayer::Stop() @ 0x1004cc3e8
bool cFreshCellPlayer::Stop() {
    if (external_ != nullptr) {
        // StopCellExternal @ 0x1004cc540
        if (state_ == kPlaying) {
            external_->StopCell(static_cast<float>(cell_->GetFadeOutTime()), cell_);
        }
        if (state_ != kFadingOut) {
            if (state_ == kPlaying) {
                if (cell_->GetFadeOutTime() != 0) state_ = kFadingOut;
            } else {
                state_ = kIdle;
            }
        }
        return true;
    }
    if (snd_ != nullptr) {
        // tell the sound system to stop updating this sound
        if (Fresh::cIFreshHost* host = Fresh::GetHost()) {
            void* updateIface = nullptr;
            if (snd_->QueryInterface(kGZIID_cIGZSndUpdateTarget, &updateIface) && updateIface != nullptr) {
                host->RemoveUpdateTarget(static_cast<cIGZSndUpdateTarget*>(updateIface));
                static_cast<cIGZSndUpdateTarget*>(updateIface)->Release();
            }
        }
    }
    if (state_ != kFadingOut) {
        if (state_ == kPlaying) {
            if (cell_->GetFadeOutTime() == 0) {
                Kill();
            } else {
                state_ = kFadingOut;
                if (snd_ != nullptr) {
                    snd_->FadeVolume(snd_->GetVolume(), 0, static_cast<std::uint32_t>(cell_->GetFadeOutTime()), true);
                }
            }
        } else {
            state_ = kIdle;
        }
    }
    return true;
}

// cFreshCellPlayer::Kill() @ 0x1004cc594
void cFreshCellPlayer::Kill() {
    if (snd_ != nullptr) {
        cIGZSnd* s = snd_;
        snd_ = nullptr;
        s->Release();
    }
    markedForRemoval_ = false;
    state_ = kIdle;
}

// cFreshCellPlayer::AutoPan() @ 0x1004cc62e — turns the cell's pan (0..1024, 512 = centre) into a
// 3D position at +-100 units; centred cells that pan randomly are thrown to a random quadrant/side.
void cFreshCellPlayer::AutoPan() {
    if (cell_ == nullptr) return;
    if (g_panOverrideRandom) cell_->SetPanIsRandom(true);
    const std::int32_t panX = cell_->GetPanX();
    const std::int32_t panZ = cell_->GetPanZ();
    double x = (panX - 0x200) * 0.001953125;
    double z = (panZ - 0x200) * 0.001953125;
    bool random = cell_->GetPanIsRandom();
    if (panZ == 0x200 && panX == 0x200 && random) {
        std::uint32_t quadrant = owner_->Random().RandomUint32Uniform() & 3;
        if (g_slamOverride) quadrant = static_cast<std::uint32_t>(g_slamQuadrant);
        if (quadrant < 4) {
            z = Fresh::kAutoPanQuadrantX[quadrant];  // DAT_100a22770 -> the "z" local of the original
            x = Fresh::kAutoPanQuadrantZ[quadrant];  // DAT_100a22790 -> the "x" local
        }
    } else {
        if (random && panX == 0x200) {
            x = static_cast<double>(static_cast<std::int32_t>((owner_->Random().RandomUint32Uniform() & 1) * 2) - 1);
            random = cell_->GetPanIsRandom();
        }
        if (random && panZ == 0x200) {
            z = static_cast<double>(static_cast<std::int32_t>((owner_->Random().RandomUint32Uniform() & 1) * 2) - 1);
        }
    }
    if (snd_ != nullptr) {
        void* iface3d = nullptr;
        if (snd_->QueryInterface(kGZIID_cIGZSnd3D, &iface3d) && iface3d != nullptr) {
            auto* snd3d = static_cast<cIGZSnd3D*>(iface3d);
            snd3d->SetPosition(static_cast<float>(x * 100.0), 0.0f, static_cast<float>(z * 100.0));
            snd3d->Release();
        }
    }
}
