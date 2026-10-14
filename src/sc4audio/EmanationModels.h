// The three SimCity 4 sound emanation models ("sems"): they replace the Hit engine's default 2D
// and 3D sems at startup (cSC4AudioSemManager) and translate a sound's logical registers into the
// channel registers, scaled by the game's master volume. Recovered from Sim City 4 Deluxe
// Editionsub 1.2.1 (cSC4AudioEmanationModel2D vtable 0x100aac3a0, 3D 0x100aac440, 3DEffects
// 0x100aac4e0; 0x50-byte objects).
//
// Register numbers are the legacy ones: 0x0c master-scaled volume (written to the source set),
// 0x13 priority, 0x14 volume scale, 0x15 volume, 0x16 pan, 0x19/0x1a effects, 0x1b duck priority,
// 0x1c 3D flag, 0x1d head relative, 0x1e/0x1f distances, 0x20..0x22 position, 0x23..0x26 and 0x33
// pass-through, 0x38 distance factor (percent), 0x40..0x42 velocity.
#pragma once
#include <cstdint>

#include "audio/interfaces/cIHitSem.h"
#include "gz/cIGZInitShutdown.h"
#include "rz/cRZAutoRefCount.h"
#include "rz/cRZString.h"
#include "sc4audio/SC4AudioHost.h"

class cSC4AudioEmanationModelBase : public cIHitSem, public cIGZInitShutdown {
public:
    // cIGZUnknown: ids 1 and cIHitSem(0xddcf3bd7) -> the sem, cIGZInitShutdown(0x89e51472) -> +8.
    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return static_cast<std::uint32_t>(++refCount_); }
    // Destroys the object only when the count is exactly 1 (the binary's test is `== 1`).
    std::uint32_t Release() override;

    std::uint32_t GetId() const override { return id_; }
    bool GetName(cIGZString& out) const override { out.Copy(name_); return true; }

    bool Init() override;       // takes a reference on SFX::SoundManager(); false if there is none
    bool Shutdown() override;   // drops it

protected:
    cSC4AudioEmanationModelBase(const char* name, std::uint32_t id) : name_(name), id_(id) {}
    virtual ~cSC4AudioEmanationModelBase() = default;
    // Master volume of the sound manager (0 when none).
    std::int32_t MasterVolume() const { return sound_ ? sound_->GetMasterVolume() : 0; }
    // v * scale / 1024 rounding toward zero (the compiled `(x + ((x >> 31) >>> 22)) >> 10`).
    static std::int32_t ScaleBy1024(std::int32_t product) {
        return (product + static_cast<std::int32_t>(static_cast<std::uint32_t>(product >> 31) >> 22)) >> 10;
    }

    std::int32_t refCount_ = 0;                          // +0x10
    cRZString name_;                                     // +0x18
    std::uint32_t id_;                                   // +0x40
    cRZAutoRefCount<SC4Audio::cIMasterVolume> sound_;    // +0x48
};

// "2D SC4K" — id 0x7dcded2d.
class cSC4AudioEmanationModel2D final : public cSC4AudioEmanationModelBase {
public:
    cSC4AudioEmanationModel2D() : cSC4AudioEmanationModelBase("2D SC4K", 0x7dcded2d) {}
    bool UpdateVolPan(cIGZSndRegisterSet* source, cIGZSndRegisterSet* channel) const override;       // @ 0x1003f30d0
    bool ConfigureChannel(cIGZSndRegisterSet* source, cIGZSndRegisterSet* channel) const override;   // @ 0x1003f3198
};

// "3D SC4K" — id 0x7dcded3d.
class cSC4AudioEmanationModel3D final : public cSC4AudioEmanationModelBase {
public:
    cSC4AudioEmanationModel3D() : cSC4AudioEmanationModelBase("3D SC4K", 0x7dcded3d) {}
    bool UpdateVolPan(cIGZSndRegisterSet* source, cIGZSndRegisterSet* channel) const override;       // @ 0x1003f346a
    bool ConfigureChannel(cIGZSndRegisterSet* source, cIGZSndRegisterSet* channel) const override;   // @ 0x1003f365a
};

// "3DEffect" — id 0x0a63a543: pan and volume follow the on-screen position of the sound.
class cSC4AudioEmanationModel3DEffects final : public cSC4AudioEmanationModelBase {
public:
    cSC4AudioEmanationModel3DEffects() : cSC4AudioEmanationModelBase("3DEffect", 0x0a63a543) {}
    bool UpdateVolPan(cIGZSndRegisterSet* source, cIGZSndRegisterSet* channel) const override;       // @ 0x1003f39b2
    bool ConfigureChannel(cIGZSndRegisterSet* source, cIGZSndRegisterSet* channel) const override;   // @ 0x1003f3cf4
};
