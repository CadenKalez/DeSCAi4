// cHitTrackPlayer — runs one Hit "track": a small bytecode program (see DoCommand) that sets
// registers, starts notes on a channel, waits, loops, fades and starts other tracks. Recovered from
// Sim City 4 Deluxe Editionsub 1.2.1 (vtable 0x100ab5010, 0x208-byte object, ctor @ 0x10047adf8).
//
// Subobject offsets as in the binary: cIHitTrackPlayer +0 (primary), cIGZSndRegisterSet +8,
// cIGZSnd3D +0x10, cIGZSnd +0x18, cIGZSndViewUpdateTarget +0x20, cIGZSndUpdateTarget +0x28,
// cIGZSndMultipleSources +0x30, cIGZSndCompletionNotification +0x38. The original embeds a
// cHitChannel at +0x188 which is only constructed/destroyed with the player (it is not used by
// the recovered code); the port keeps the member for size/layout parity.
#pragma once
#include <cstdint>
#include <list>
#include <set>
#include <vector>

#include "audio/HitChannel.h"
#include "audio/HitMan.h"
#include "audio/HitManServices.h"
#include "audio/interfaces/cIGZSndCompletionFunctionObject.h"
#include "audio/interfaces/cIGZSndCompletionNotification.h"
#include "audio/interfaces/cIHitTrackPlayer.h"
#include "gz/cGZPropertySet.h"
#include "gz/cIGZIStream.h"
#include "gz/cIGZOStream.h"
#include "rz/cRZAutoRefCount.h"
#include "rz/cRZString.h"

class cHitTrackPlayer : public cIHitTrackPlayer,
                              public cIGZSndRegisterSet,
                              public cIGZSnd3D,
                              public cIGZSnd,
                              public cIGZSndViewUpdateTarget,
                              public cIGZSndUpdateTarget,
                              public cIGZSndMultipleSources,
                              public cIGZSndCompletionNotification {
public:
    cHitTrackPlayer();    // @ 0x10047adf8
    virtual ~cHitTrackPlayer();  // @ 0x10047b2f8

    // Live sound objects (the binary's __g_lNumSoundObjects).
    static std::int32_t NumSoundObjects();

    // --- cIGZUnknown (shared). id 1 and 0xbb1be367 return the primary pointer. ---
    bool QueryInterface(std::uint32_t iid, void** out) override;   // @ 0x10047b6c0
    std::uint32_t AddRef() override { return static_cast<std::uint32_t>(++refCount_); }   // @ 0x10047b7c2
    std::uint32_t Release() override;                              // @ 0x10047b83e

    // --- cIHitTrackPlayer ---
    cIGZSnd* AsIGZSnd() override { return static_cast<cIGZSnd*>(this); }                   // @ 0x100473ad2 (+0x18)
    cIGZSnd3D* AsIGZSnd3D() override { return static_cast<cIGZSnd3D*>(this); }             // @ 0x100473adc (+0x10)
    cIGZSndMultipleSources* AsIGZSndMultipleSources() override { return this; }            // @ 0x100473ae6 (+0x30)
    cIGZSndRegisterSet* AsIGZSndRegisterSet() override { return static_cast<cIGZSndRegisterSet*>(this); }  // @ 0x100473af0
    bool SetTrackDef(cIGZPropertySet* trackDef) override;          // @ 0x10047ed56
    bool GetTrackDef(std::uint32_t iid, void** out) override;      // @ 0x10047ed34
    bool SetHitList(cIHitList* list) override;                     // @ 0x10047f040
    bool GetHitList(std::uint32_t iid, void** out) override;       // @ 0x10047f08c
    float CompletionTime() override;                               // @ 0x10047f0aa
    bool Reset() override;                                         // @ 0x10047f1ac
    void Update(std::int32_t elapsedMs) override;                  // @ 0x10047ba98 (-> HandleTimerCallback)
    bool UpdateArgs(std::int32_t, std::int32_t, std::int32_t, std::int32_t) override { return false; }  // @ 0x100480aea
    bool PlayPause() override;                                     // @ 0x10047f1c4
    void SetErrorCondition() override { errorCondition_ = true; }  // @ 0x100481210
    bool GetErrorCondition() override { return errorCondition_; }  // @ 0x10048121e
    void Step() override;                                          // @ 0x10047fccc
    bool CurrentPlayPosition(const std::uint8_t** position) override;  // @ 0x100480d00

    // --- own virtuals in vtable order (slots 22..96) ---
    virtual bool ReadResource(cIGZIStream&, std::uint32_t, cIGZUnknown*) { return false; }    // @ 0x100473b32
    virtual bool WriteResource(cIGZOStream&, std::uint32_t, cIGZUnknown*) { return false; }   // @ 0x100473b3a
    virtual std::uint32_t GetRefCount() const { return static_cast<std::uint32_t>(refCount_); }  // @ 0x100481232
    bool Init() override { return true; }                          // @ 0x10047b972
    bool Shutdown() override;                                      // @ 0x10047b982
    virtual void HandleTimerCallback(std::int32_t elapsedMs);      // @ 0x10047bac0
    void UpdateView() override {}                                  // @ 0x100473b4e
    bool SetRegisterValue(std::int32_t id, std::int32_t value, bool suppressNotification) override;  // @ 0x10047feb8
    void ClearRegisterValue(std::int32_t id, bool suppressNotification) override;                    // @ 0x100480302
    std::int32_t GetRegisterValue(std::int32_t id) const override;                                   // @ 0x10047fd08
    bool HasRegisterValue(std::int32_t id) const override;                                           // @ 0x10047fe56
    bool SetRegisterFloat(std::uint32_t id, float value, bool suppressNotification) override;        // @ 0x1004800e4
    float GetRegisterFloat(std::uint32_t id) const override;                                         // @ 0x10047fdb4
    virtual bool GetSourceFilenameAndLineNum(cRZString&, std::int32_t&) { return false; }            // @ 0x100480ac6
    virtual bool SetTrackDataEntryPoint(std::uint32_t entryKey);                                     // @ 0x100480af2
    bool SetSoundFileFromString(const cIGZString&, SndStreamingType) override { return false; }      // @ 0x100473b58
    bool SetSoundFileFromChars(const char*, std::uint32_t, SndStreamingType) override { return false; }  // @ 0x100473b60
    virtual bool SetDataSource(cIGZSndDataSource*, SndStreamingType) { return false; }                                        // @ 0x100473b68 (own virtual)
    bool PlayWithVolumeAndLoop(std::int32_t a, bool, SndDup) override { return PlayWithScriptArguments(a, 0, 0, 0); }  // @ 0x10047e878
    bool PlayWithScriptArguments(std::int32_t a0, std::int32_t a1, std::int32_t a2, std::int32_t a3) override;  // @ 0x10047e8ae
    bool IsPlaying() const override;                               // @ 0x10047b9f2
    bool Stop() override;                                          // @ 0x10047f9a8
    bool Kill() override;                                          // @ 0x10047fb0e
    bool StopAllInstances() override { return false; }             // @ 0x100473b70
    virtual bool CancelNote();                                     // @ 0x10047fc92
    std::uint32_t GetUseCount() const override { return 0; }      // @ 0x100473b78
    std::uint32_t GetID() const override { return id_; }          // @ 0x100473b80
    void SetID(std::uint32_t id) override { id_ = id; }           // @ 0x100473b8a
    std::int32_t GetSoundPriority() const override { return GetRegisterValue(0x13); }  // @ 0x100481248
    bool SetSoundPriority(std::int32_t p) override { SetRegisterValue(0x13, p, false); return true; }  // @ 0x100473b9a
    std::int32_t GetOriginalFrequency() const override { return GetRegisterValue(0x49); }  // @ 0x100481268
    bool SetCompletionNotification(cIGZSndCompletionFunctionObject* callback) override;  // @ 0x100480fbc
    bool StopCompletionNotification() override;                    // @ 0x100481034
    std::uint32_t EstimateRemainingPlayTime() override { return 0; }  // @ 0x100473bba
    bool GetPosition(float* x, float* y, float* z) const override;     // @ 0x100481094 (pointers must be non-null)
    bool SetPosition(float x, float y, float z) override;              // @ 0x10048118c
    bool GetVelocity(float*, float*, float*) const override { return false; }  // @ 0x100473bc2
    bool SetVelocity(float, float, float) override { return false; }           // @ 0x100473bc8
    bool GetAll(float*, float*, float*, float*, float*, float*) const override { return false; }  // @ 0x100473bd0
    bool SetAll(float, float, float, float, float, float) override { return false; }              // @ 0x100473bd6
    void SetPower(std::uint32_t) override {}                                   // @ 0x100473bde
    bool GetMinimumDistance(float*) const override { return false; }           // @ 0x100481288
    bool SetMinimumDistance(float) override { return false; }                  // @ 0x100473bea
    bool GetMaximumDistance(float*) const override { return false; }           // @ 0x100473bf2
    bool SetMaximumDistance(float) override { return false; }                  // @ 0x100473bf8
    bool SetHeadRelative(bool) override { return false; }                      // @ 0x100473c00
    bool GetHeadRelative() const override { return false; }                    // @ 0x100473c08
    bool SetEffectsType(std::int32_t, std::uint32_t) override { return false; }   // @ 0x100473c10
    bool SetEffectsLevel(std::int32_t, std::uint32_t) override { return false; }  // @ 0x100473c18
    bool IsLoaded() const override { return loadCount_ != 0; }     // @ 0x100473c20
    bool Load() override;                                          // @ 0x100480c60
    bool Unload() override;                                        // @ 0x100480cb8
    bool Pause(bool pauseChannel) override;                        // @ 0x10047f81a
    bool Unpause() override;                                       // @ 0x10047f8dc
    bool IsPaused() const override { return paused_ != 0; }        // @ 0x100473c2e
    std::int32_t GetVolume() const override { return GetRegisterValue(0x15); }  // @ 0x100473c3e
    bool SetVolume(std::int32_t v) override { SetRegisterValue(0x15, v, false); return true; }  // @ 0x100481298
    virtual bool SetPitch(std::int32_t v) { SetRegisterValue(0x17, v, false); return true; }   // @ 0x100473c5e
    std::int32_t GetPan() const override { return GetRegisterValue(0x16); }   // @ 0x100473c78
    bool SetPan(std::int32_t v) override { SetRegisterValue(0x16, v, false); return true; }   // @ 0x100473c8e
    virtual bool SetEffectsType(std::int32_t v) { SetRegisterValue(0x19, v, false); return true; }   // @ 0x100473ca8
    virtual bool SetEffectsLevel(std::int32_t v) { SetRegisterValue(0x1a, v, false); return true; }  // @ 0x100473cc2
    bool FadeVolume(std::int32_t, std::int32_t, std::uint32_t, bool) override { return false; }  // @ 0x1004812c0
    bool IsFading() const override { return GetRegisterValue(0x2a) != 0; }    // @ 0x1004812c8
    std::int32_t GetFrequency() const override { return GetRegisterValue(0x17); }  // @ 0x100473cde
    bool SetFrequency(std::int32_t frequency) override;            // @ 0x100480bc6
    std::set<std::uint32_t> SourceList() override { return sources_; }   // @ 0x1004812e8
    void AddSource(std::uint32_t sourceId) override;               // @ 0x100480d38
    bool RemoveSource(std::uint32_t sourceId) override;            // @ 0x100480e46
    void RemoveAllSources() override;                              // @ 0x100480ebe
    bool UpdateSources() override;                                 // @ 0x100480f58
    bool SetName(const cIGZString& name) override { name_.Copy(name); return true; }   // @ 0x100481308
    bool GetName(cIGZString& out) const override { out.Copy(name_); return true; }     // @ 0x100473cf6
    virtual bool HandleNotifySetPropertyValue(std::int32_t propertyId);                // @ 0x1004803f0
    virtual std::int32_t GetLocalRegisterVal(std::int32_t id) { return GetRegisterValue(id); }  // @ 0x100480ad8

    // Non-virtual helpers of the original.
    bool DoCommand();                          // @ 0x10047bda2 the bytecode interpreter
    bool NoteOn();                             // @ 0x100480a3c
    bool UpdatePatch();                        // @ 0x10048082a
    bool UpdatePitch();                        // @ 0x10047f596
    bool UpdateVolPan();                       // @ 0x10047f562
    bool StartSndDependent(std::uint32_t soundId);  // @ 0x10047eb34

    // Flag registers set by comparisons / SetRegister (+0x130/+0x131/+0x132).
    bool flagEqual() const { return flagEqual_; }
    bool flagGreater() const { return flagGreater_; }
    bool flagLess() const { return flagLess_; }
    std::int32_t waitTicks() const { return wait_; }
    void SetProgram(const std::uint8_t* base, const std::uint8_t* entry) {  // test hook for SetTrackDataEntryPoint
        base_ = base; pc_ = entry; entry_ = entry; loopMark_ = entry;
    }

private:
    cIHitMan* HitMan() const;
    Hit::cIHitManServices* Services() const { return Hit::GetServices(); }
    cIGZSnd* ChannelSnd() const { return channelSnd_.get(); }
    void ReportError(const char* message);
    void SemRefresh();
    cIGZPropertySet* OwnLayer() const { return layers_[0].get(); }
    bool ReadRegisterOperand(std::uint8_t& out, bool clamp = true);
    void SetFlags(std::int32_t v) { flagEqual_ = v == 0; flagGreater_ = v > 0; flagLess_ = v < 0; }

    cRZString name_;                                   // +0x40
    cRZAutoRefCount<cIGZUnknown> unknown68_;           // +0x68 (copied by operator=, never used)
    std::int32_t loadCount_ = 0;                       // +0x7c
    std::uint32_t id_ = 0;                             // +0x78
    std::int32_t wait_ = 0;                            // +0x80 ticks (ms) until the next instruction
    std::int32_t noteTicks_ = 0;                       // +0x84 note duration counter
    std::int32_t refCount_ = 0;                        // +0x8c
    bool playing_ = false;                             // +0x94
    std::int32_t paused_ = 0;                          // +0x98
    std::int32_t patchId_ = 0;                         // +0xa0 sound id currently loaded
    const std::uint8_t* base_ = nullptr;               // +0xa8 start of the track data block
    const std::uint8_t* pc_ = nullptr;                 // +0xb0
    const std::uint8_t* entry_ = nullptr;              // +0xb8
    const std::uint8_t* loopMark_ = nullptr;           // +0xc0
    std::list<const std::uint8_t*> callStack_;         // +0xc8
    cRZAutoRefCount<cIGZSndRegisterSet> channelRegs_;  // +0xe0
    cRZAutoRefCount<cIGZSnd> channelSnd_;              // +0xe8
    cRZAutoRefCount<cIGZSndRegisterSet> targetRegs_;   // +0xf0
    cRZAutoRefCount<cIHitSem> sem_;                    // +0xf8
    std::list<cRZAutoRefCount<cIGZSnd>> children_;     // +0x118 sounds started by "play dependent"
    bool flagEqual_ = false, flagGreater_ = false, flagLess_ = false;  // +0x130..+0x132
    std::vector<cRZAutoRefCount<cIGZPropertySet>> layers_;  // +0x138 [0] working, [1] track def, [2..] parents
    cRZAutoRefCount<cIHitList> defaultHitList_;        // +0x150
    cRZAutoRefCount<cIHitList> hitList_;               // +0x158
    std::set<std::uint32_t> sources_;                  // +0x160
    cRZAutoRefCount<cIGZSndCompletionFunctionObject> completion_;  // +0x178
    bool errorCondition_ = false;                      // +0x180
    cHitChannel embeddedChannel_;                      // +0x188 (parity only)
    bool fadingOut_ = false;                           // +0x200
};
