// cHitChannel — one playing voice of the Hit engine. It owns a property-set "register file"
// (own layer first, then inherited parent layers) and drives an underlying cIGZSnd that is
// created on demand from the sound manager's default 2D/3D player class. Recovered from Sim City 4
// Deluxe Editionsub 1.2.1 (vtable 0x100ab57c0, 0x78-byte object, ctor @ 0x100482a00).
//
// Bases/offsets as in the binary: cIGZSnd +0, cIGZSnd3D +8, cIGZSndUpdateTarget +0x10,
// cIGZSndRegisterSet +0x18, cIGZSndViewUpdateTarget +0x20, cIGZSndDataSourceHolder +0x28.
// Registers: property ids 0xFFxxxxxx (legacy numbers < 0x100 are translated, see HitRegisters.h).
#pragma once
#include <cstdint>
#include <list>

#include "audio/HitManServices.h"
#include "audio/HitRegisters.h"
#include "audio/interfaces/cIGZSnd.h"
#include "audio/interfaces/cIGZSnd3D.h"
#include "audio/interfaces/cIGZSndDataSource.h"
#include "audio/interfaces/cIGZSndDataSourceHolder.h"
#include "audio/interfaces/cIGZSndRegisterSet.h"
#include "audio/interfaces/cIGZSndUpdateTarget.h"
#include "audio/interfaces/cIGZSndViewUpdateTarget.h"
#include "gz/cGZPropertySet.h"
#include "rz/cRZAutoRefCount.h"

class cHitChannel final : public cIGZSnd,
                          public cIGZSnd3D,
                          public cIGZSndUpdateTarget,
                          public cIGZSndRegisterSet,
                          public cIGZSndViewUpdateTarget,
                          public cIGZSndDataSourceHolder {
public:
    cHitChannel();   // @ 0x100482a00: refcount 0, volume 0x400, one empty register layer
    // cHitChannel::operator= @ 0x100474874 copies flag, refcount, snd, holder, data source and layers.
    cHitChannel& operator=(const cHitChannel& other);

    // --- cIGZUnknown (shared): ids 0x206077d2 cIGZSnd, 0x306077d2 cIGZSnd3D, 0x5b34f0af
    // update target, 0xfd584467 register set, 0xaa11a64f view target, 0x09d002d2 data source holder.
    // cIGZUnknown's id 1 is NOT accepted. ---
    bool QueryInterface(std::uint32_t iid, void** out) override;  // @ 0x100482e3a
    std::uint32_t AddRef() override { return static_cast<std::uint32_t>(++refCount_); }  // @ 0x100482eec
    std::uint32_t Release() override;                                                    // @ 0x100482f40

    // --- cIGZSnd ---
    bool Init() override { refCount_ = 0; return true; }                   // @ 0x100484816 (resets the count!)
    bool Shutdown() override { snd_ = nullptr; return true; }              // @ 0x100484826
    bool PlayWithVolumeAndLoop(std::int32_t, bool, SndDup) override { return NoteOn(); }          // @ 0x100484848
    bool PlayWithScriptArguments(std::int32_t, std::int32_t, std::int32_t, std::int32_t) override { return NoteOn(); }  // @ 0x100484856
    bool IsPlaying() const override { return NoteIsPlaying(); }            // @ 0x100484864
    bool Stop() override { return NoteOff(); }                             // @ 0x100484872
    bool Kill() override { return NoteKill(); }                            // @ 0x100484880
    bool StopAllInstances() override { return false; }                     // @ 0x10048488e
    std::uint32_t GetUseCount() const override { return 0; }               // @ 0x100484896
    std::uint32_t GetID() const override;                                  // @ 0x100483b46
    void SetID(std::uint32_t id) override;                                 // @ 0x100483b5e
    bool Load() override;                                                  // @ 0x1004830be
    bool Unload() override;                                                // @ 0x100483100
    bool IsLoaded() const override;                                        // @ 0x10048489e
    bool Pause(bool) override;                                             // @ 0x1004848ba (register 0x18 = 1)
    bool Unpause() override;                                               // @ 0x1004848d8
    bool IsPaused() const override;                                        // @ 0x1004848f2
    std::int32_t GetVolume() const override;                               // @ 0x100483b30
    bool SetVolume(std::int32_t volume) override;                          // @ 0x100483b16
    bool FadeVolume(std::int32_t, std::int32_t, std::uint32_t, bool) override { return false; }  // @ 0x10048490c
    bool IsFading() const override { return false; }                       // @ 0x100484914
    std::int32_t GetPan() const override;                                  // @ 0x100483a9a
    bool SetPan(std::int32_t pan) override;                                // @ 0x100483ab0
    std::int32_t GetFrequency() const override;                            // @ 0x100483aca
    bool SetFrequency(std::int32_t frequency) override;                    // @ 0x100483ae0
    std::int32_t GetOriginalFrequency() const override;                    // @ 0x100483afa
    bool SetEffectsType(std::int32_t, std::uint32_t) override { return false; }    // @ 0x10048491c
    bool SetEffectsLevel(std::int32_t, std::uint32_t) override { return false; }   // @ 0x100484924
    std::int32_t GetSoundPriority() const override { return 0; }           // @ 0x10048492c
    bool SetSoundPriority(std::int32_t) override { return false; }         // @ 0x100484934
    bool SetName(const cIGZString&) override { return false; }             // @ 0x10048493a
    bool GetName(cIGZString&) const override { return false; }             // @ 0x100484942
    bool SetSoundFileFromString(const cIGZString&, SndStreamingType) override { return false; }  // @ 0x10048494a
    bool SetSoundFileFromChars(const char*, std::uint32_t, SndStreamingType) override { return false; }  // @ 0x100484952

    virtual ~cHitChannel();                                                // @ 0x100482c76

    // --- own virtuals in vtable order (slots 39..78) ---
    virtual cIGZSndUpdateTarget* AsIGZSndUpdateTarget() { return this; }   // @ 0x10048495a (+0x10)
    virtual cIGZSndRegisterSet* AsIGZSndRegisterSet() { return this; }     // @ 0x100484964 (+0x18)
    cIGZSnd* AsIGZSnd() override { return this; }                          // @ 0x10048496e
    bool SetDataSource(cIGZSndDataSource* source, cIGZSnd::SndStreamingType streaming) override;  // @ 0x100483414
    cIGZSndDataSource* GetDataSource() override;                           // @ 0x100484978
    bool SetRegisterValue(std::int32_t id, std::int32_t value, bool suppressNotification) override;  // @ 0x100484176
    std::int32_t GetRegisterValue(std::int32_t id) const override;         // @ 0x1004842a0
    void ClearRegisterValue(std::int32_t id, bool suppressNotification) override;  // @ 0x1004843fe
    bool HasRegisterValue(std::int32_t id) const override;                 // @ 0x100484456
    bool SetRegisterFloat(std::uint32_t id, float value, bool suppressNotification) override;  // @ 0x100484206
    float GetRegisterFloat(std::uint32_t id) const override;               // @ 0x10048434c
    void UpdateView() override {}                                          // @ 0x100484988
    bool GetPosition(float* x, float* y, float* z) const override;         // @ 0x100483b78
    bool SetPosition(float x, float y, float z) override;                  // @ 0x100483c0a
    bool GetVelocity(float* x, float* y, float* z) const override;         // @ 0x100483cdc
    bool SetVelocity(float x, float y, float z) override;                  // @ 0x100483d6e
    bool GetAll(float* px, float* py, float* pz, float* vx, float* vy, float* vz) const override;  // @ 0x100483e40
    bool SetAll(float px, float py, float pz, float vx, float vy, float vz) override;  // @ 0x100483f52
    void SetPower(std::uint32_t) override {}                               // @ 0x10048498e
    bool GetMinimumDistance(float* distance) const override;               // @ 0x100484020 (does not write *distance)
    bool SetMinimumDistance(float distance) override;                      // @ 0x100484052
    bool GetMaximumDistance(float* distance) const override;               // @ 0x100484090 (does not write *distance)
    bool SetMaximumDistance(float distance) override;                      // @ 0x1004840c2
    bool SetHeadRelative(bool headRelative) override;                      // @ 0x100484100
    bool GetHeadRelative() const override;                                 // @ 0x10048413e
    virtual bool NoteOn();                                                 // @ 0x100483014
    virtual bool NoteOff() { return NoteKill(); }                          // @ 0x100484994 (tail call)
    virtual bool NoteKill();                                               // @ 0x1004849a2
    virtual bool NoteIsPlaying() const;                                    // @ 0x1004849ba
    void Update(std::int32_t) override {}                                  // @ 0x1004849d2
    virtual bool UpdatePitch();                                            // @ 0x100483118
    virtual bool UpdateVolPan();                                           // @ 0x100483200
    virtual bool CopyAllRegisters(std::int32_t*, std::int32_t) { return true; }  // @ 0x1004849d8
    virtual void SetRolloffFactor(float factor);                           // @ 0x1004844a8
    virtual void GetRolloffFactor(float& factor) const;                    // @ 0x100484520
    virtual bool SetInputDryLevel(float level);                            // @ 0x100484592
    virtual bool SetInputEffectLevel(float level);                         // @ 0x100484616
    virtual bool SetLowPassCutoff(std::int32_t hertz);                     // @ 0x10048469a
    virtual bool SetHighPassCutoff(std::int32_t hertz);                    // @ 0x100484716
    virtual bool SetTimeMultiplier(float multiplier);                      // @ 0x100484792

    // Non-virtual helpers of the original.
    bool UpdatePatch();            // @ 0x100483072: (re)creates the sound, binds the data source, refreshes
    bool CreateSnd();              // @ 0x1004834a0
    bool HandleNotifySetPropertyValue(std::int32_t propertyId);  // @ 0x100483674

    // Inspection (tests / debugging).
    cIGZSnd* underlyingSnd() const { return snd_.get(); }
    std::int32_t effectiveVolume() const { return volume_; }
    std::size_t layerCount() const { return layers_.size(); }

private:
    cGZPropertySet* ownLayer() const { return static_cast<cGZPropertySet*>(layers_.front().get()); }
    float RegisterAsFloat(std::uint32_t id) const { return static_cast<float>(GetRegisterValue(static_cast<std::int32_t>(id))); }

    std::int32_t refCount_ = 0;                                  // +0x30
    std::int32_t volume_ = 0x400;                                // +0x34 effective volume (volume*scale/1024)
    bool unusedFlag_ = false;                                    // +0x38 copied by operator=, never read
    cRZAutoRefCount<cIGZSnd> snd_;                               // +0x40 underlying sound
    cRZAutoRefCount<cIGZSndDataSourceHolder> holder_;            // +0x48 its data-source holder interface
    cRZAutoRefCount<cIGZSndDataSource> dataSource_;              // +0x50
    cRZAutoRefCount<cIGZUnknown> soundManager_;                  // +0x58 sound manager service (unused: see HitManServices.h)
    std::list<cRZAutoRefCount<cIGZPropertySet>> layers_;         // +0x60 own layer first, then parents
};
