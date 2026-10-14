// cGZSnd3D (a positional cGZSnd) and cGZSnd3DListener (the listener all of them are heard from).
// Recovered from Sim City 4 Deluxe Editionsub 1.2.1: cGZSnd3D vtable 0x100ad9410 (0x118-byte
// object, ctor @ 0x100627380, Create @ 0x100624f20), cGZSnd3DListener vtable 0x100ad9708
// (ctor @ 0x100628ce0).
//
// Coordinates arrive in game space (x, y, z) and are stored remapped as (-y, z, x) for the mixer.
// Most getters of the original are empty stubs that never write their outputs; they are kept
// that way (they return false here so callers can tell). Every cGZSnd3D registers in a global
// list (sSnd3DList) so the listener can refresh all sounds when it moves.
#pragma once
#include <vector>

#include "audio/GZSnd.h"
#include "audio/interfaces/cIGZSnd3D.h"
#include "audio/interfaces/cIGZSnd3DListener.h"

class cGZSnd3DListener;

// cGZSndSysMac::GetListener() @ 0x100628bd0: the listener sounds are positioned against.
namespace GZSnd3D {
cGZSnd3DListener* ActiveListener();
void SetActiveListener(cGZSnd3DListener* listener);
}  // namespace GZSnd3D

class cGZSnd3D : public cGZSnd, public cIGZSnd3D {
public:
    cGZSnd3D();                                                        // @ 0x100627380
    ~cGZSnd3D() override;                                              // @ 0x100627620
    static cGZSnd3D* Create() { return new cGZSnd3D(); }               // @ 0x100624f20

    bool QueryInterface(std::uint32_t iid, void** out) override;       // 0x306077d2 -> cIGZSnd3D, else cGZSnd
    std::uint32_t AddRef() override { return cGZSnd::AddRef(); }       // @ 0x100627ff0
    std::uint32_t Release() override { return cGZSnd::Release(); }     // @ 0x100628070

    bool Init() override { return cGZSnd::Init(); }                    // @ 0x1006280f0
    bool Shutdown() override { return cGZSnd::Shutdown(); }            // @ 0x100628110

    // --- cIGZSnd3D ---
    cIGZSnd* AsIGZSnd() override { return static_cast<cGZSnd*>(this); }   // @ 0x1006299f0
    bool GetPosition(float*, float*, float*) const override { return false; }   // @ 0x100628130
    bool SetPosition(float x, float y, float z) override;              // @ 0x100628190
    bool GetVelocity(float*, float*, float*) const override { return false; }   // @ 0x1006287a0
    bool SetVelocity(float, float, float) override { return true; }    // @ 0x100628800
    bool GetAll(float*, float*, float*, float*, float*, float*) const override { return false; }   // @ 0x100628860
    bool SetAll(float, float, float, float, float, float) override { return false; }   // @ 0x100628900
    void SetPower(std::uint32_t) override {}                           // @ 0x1006289a0
    bool GetMinimumDistance(float*) const override { return false; }   // @ 0x1006289d0
    bool SetMinimumDistance(float distance) override;                  // @ 0x100628a10
    bool GetMaximumDistance(float*) const override { return false; }   // @ 0x100628a80
    bool SetMaximumDistance(float distance) override;                  // @ 0x100628ac0
    bool SetHeadRelative(bool headRelative) override;                  // @ 0x100628b30 (does not update)
    bool GetHeadRelative() const override { return false; }            // @ 0x100628b90

    // @ 0x100628250: recomputes the left/right volumes from the listener-relative position.
    // 2D mode (listener closer than 0.1): 0.8125 * master volume on both sides. Otherwise the
    // volume attenuates with distance (full below the minimum distance, base/distance above) and
    // is split between the ears by the side the sound is on; both are clamped to [0, 1].
    void Update3DSound();

    bool needsUpdate() const { return dirty_; }                        // +0xf8
    const float* mixerPosition() const { return position_; }           // +0xfc (-y, z, x)
    float minimumDistance() const { return minimumDistance_; }
    float maximumDistance() const { return maximumDistance_; }
    bool headRelative() const { return headRelative_; }

private:
    bool dirty_ = true;                    // +0xf8
    float position_[3] = {0, 0, 0};        // +0xfc
    float minimumDistance_ = 0.0f;         // +0x108
    float maximumDistance_ = 100000.0f;    // +0x10c (0x47c35000)
    bool headRelative_ = false;            // +0x110
};

// The global list of positional sounds (original: sSnd3DList guarded by sSoundListCriticalSection).
namespace GZSnd3D {
std::vector<cGZSnd3D*>& SoundList();
std::recursive_mutex& SoundListLock();
}  // namespace GZSnd3D

class cGZSnd3DListener : public cIGZSnd3DListener, public cIGZSnd3DListenerEx {
public:
    cGZSnd3DListener() = default;                                      // @ 0x100628ce0
    virtual ~cGZSnd3DListener() = default;

    // ids 1 and 0x406077d2 -> primary, 0x5b30e7b3 -> the Ex interface.
    bool QueryInterface(std::uint32_t iid, void** out) override;       // @ 0x100628e40
    std::uint32_t AddRef() override { return static_cast<std::uint32_t>(++refCount_); }   // @ 0x100628f40
    std::uint32_t Release() override;                                  // @ 0x100628f80 (destroys below 2)

    bool Init() override;                                              // @ 0x100629000
    bool Shutdown() override { return true; }                          // @ 0x100629090
    bool CommitDeferredSettings() override;                            // @ 0x1006290b0
    bool GetPosition(float*, float*, float*) const override { return false; }   // @ 0x100629310
    bool SetPosition(float x, float y, float z) override;              // @ 0x100629330 (commits)
    bool GetOrientation(float*, float*, float*, float*, float*, float*) const override { return false; }   // @ 0x1006293b0
    bool SetOrientation(float fx, float fy, float fz, float ux, float uy, float uz) override;   // @ 0x1006293e0
    cIGZSnd3DListenerEx* AsIGZSnd3DListenerEx() override { return this; }   // @ 0x100629490

    bool GetVelocity(float*, float*, float*) const override { return false; }   // @ 0x1006294b0
    bool SetVelocity(float, float, float) override { return true; }    // @ 0x100629510
    bool GetAll(float*, float*, float*, float*, float*, float*, float*, float*, float*, float*, float*, float*) const override { return false; }   // @ 0x100629570
    bool SetAll(float, float, float, float, float, float, float, float, float, float, float, float) override { return false; }   // @ 0x100629720
    bool GetDopplerFactor(float*) const override { return false; }     // @ 0x100629870
    bool SetDopplerFactor(float) override { return false; }            // @ 0x1006298b0
    bool GetRolloffFactor(float*) const override { return false; }     // @ 0x1006298f0
    bool SetRolloffFactor(float) override { return true; }             // @ 0x100629930
    bool GetDistanceFactor(float*) const override { return false; }    // @ 0x100629970
    bool SetDistanceFactor(float) override { return false; }           // @ 0x1006299b0

    const float* position() const { return position_; }               // +0x18 (-y, z, x)
    const float* front() const { return front_; }                      // +0x24
    const float* up() const { return up_; }                            // +0x30
    bool dirty() const { return dirty_; }                              // +0x14

private:
    std::int32_t refCount_ = 0;            // +0x10
    bool dirty_ = false;                   // +0x14
    float position_[3] = {0, 0, 0};        // +0x18
    float front_[3] = {0, 0, 0};           // +0x24
    float up_[3] = {0, 0, 0};              // +0x30
};
