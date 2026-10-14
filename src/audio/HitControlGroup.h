// cHitControlGroup — a control group of the Hit audio engine: a set of track ids plus a group
// volume that cHitMan hands out by id (cHitMan::ControlGroup @ 0x1004bfebe constructs it inline:
// refcount 1, volume 0x400). It also implements the whole cIGZSnd surface as stubs so it can be
// treated as a sound; only AddTrack, volume, id and name do anything.
// Recovered from Sim City 4 Deluxe Editionsub 1.2.1 (vtable 0x100ab6a20, 0x60-byte object).
#pragma once
#include <cstdint>
#include <set>

#include "audio/interfaces/cIHitControlGroup.h"
#include "rz/cRZString.h"

class cIGZSndDataSource;
class cIGZSndCompletionFunctionObject;

class cHitControlGroup final : public cIHitControlGroup, public cIGZSnd {
public:
    // Constructed inline by cHitMan::ControlGroup(int).
    explicit cHitControlGroup(std::int32_t groupId) : refCount_(1), id_(static_cast<std::uint32_t>(groupId)) {}

    // --- cIGZUnknown (both bases): only kGZIID_cIHitControlGroup and cIGZUnknown are accepted ---
    bool QueryInterface(std::uint32_t iid, void** out) override;     // @ 0x1004c058a
    std::uint32_t AddRef() override { return static_cast<std::uint32_t>(++refCount_); }   // @ 0x1004c05d2
    std::uint32_t Release() override;                                // @ 0x1004c05ee

    // --- cIHitControlGroup ---
    void AddTrack(std::int32_t trackId) override { tracks_.insert(trackId); }  // @ 0x1004c062a
    cIGZSnd* AsIGZSnd() override { return this; }                    // @ 0x1004c20a6

    virtual ~cHitControlGroup() = default;

    // --- own virtuals in vtable order (slots 7..64). Entries that implement cIGZSnd are marked. ---
    virtual bool SetRegisterVal(std::int32_t, std::int32_t) { return false; }        // [7]  @ 0x1004c20e8
    virtual std::int32_t GetRegisterVal(std::int32_t) const { return 0; }             // [8]  @ 0x1004c20f0
    bool Init() override { return true; }                                             // [9]  cIGZSnd
    bool Shutdown() override { return true; }                                         // [10] cIGZSnd
    virtual bool SetDataSource(cIGZSndDataSource*, SndStreamingType) { return false; }  // [11]
    bool SetVolume(std::int32_t volume) override { volume_ = volume; return true; }    // [12] cIGZSnd
    std::int32_t GetVolume() const override { return volume_; }                        // [13] cIGZSnd
    bool PlayWithScriptArguments(std::int32_t, std::int32_t, std::int32_t, std::int32_t) override { return true; }  // [14]
    bool Stop() override { return true; }                                              // [15]
    bool Kill() override { return true; }                                              // [16]
    virtual bool SetPitch(std::int32_t) { return true; }                               // [17]
    bool SetPan(std::int32_t) override { return true; }                                // [18]
    std::int32_t GetPan() const override { return 0; }                                 // [19]
    virtual bool SetPosition(std::int32_t, std::int32_t, std::int32_t) { return true; }  // [20]
    bool SetEffectsType(std::int32_t, std::uint32_t) override { return true; }         // [21]
    bool SetEffectsLevel(std::int32_t, std::uint32_t) override { return true; }        // [22]
    bool Pause(bool) override { return true; }                                         // [23]
    bool Unpause() override { return true; }                                           // [24]
    bool Load() override { return true; }                                              // [25]
    bool Unload() override { return true; }                                            // [26]
    bool IsLoaded() const override { return true; }                                    // [27]
    bool IsPlaying() const override { return true; }                                   // [28]
    bool IsPaused() const override { return true; }                                    // [29]
    virtual void Update(std::int32_t) {}                                               // [30]
    virtual bool SetRegisterVal(std::int32_t, std::int32_t, bool) { return false; }    // [31] @ 0x1004c06ca
    virtual bool RegisterVal(std::int32_t) { return true; }                            // [32]
    bool FadeVolume(std::int32_t, std::int32_t, std::uint32_t, bool) override { return false; }  // [33]
    bool IsFading() const override { return false; }                                   // [34]
    std::int32_t GetFrequency() const override { return 0; }                           // [35]
    bool SetFrequency(std::int32_t) override { return false; }                         // [36]
    std::int32_t GetOriginalFrequency() const override { return 0; }                   // [37]
    virtual bool SetDataSourceById(std::uint32_t) { return false; }                    // [38] SetDataSource(unsigned int)
    bool PlayWithVolumeAndLoop(std::int32_t, bool, SndDup) override { return false; }  // [39]
    virtual bool PlayWithUnsignedArguments(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) { return false; }  // [40]
    bool StopAllInstances() override { return false; }                                 // [41]
    std::int32_t GetSoundPriority() const override { return 0; }                       // [42]
    bool SetSoundPriority(std::int32_t) override { return false; }                     // [43] (returns void in the original)
    std::uint32_t GetUseCount() const override { return 0; }                           // [44]
    std::uint32_t GetID() const override { return id_; }                               // [45]
    void SetID(std::uint32_t id) override { id_ = id; }                                // [46]
    virtual void GetPosition(float*, float*, float*) const {}                          // [47]
    virtual bool SetPosition(float, float, float) { return false; }                    // [48]
    virtual void GetVelocity(float*, float*, float*) const {}                          // [49]
    virtual bool SetVelocity(float, float, float) { return false; }                    // [50]
    virtual void GetAll(float*, float*, float*, float*, float*, float*) const {}       // [51]
    virtual bool SetAll(float, float, float, float, float, float) { return false; }    // [52]
    virtual void SetPower(std::uint32_t) {}                                            // [53]
    virtual void GetMinimumDistance(float*) const {}                                   // [54]
    virtual bool SetMinimumDistance(float) { return false; }                           // [55]
    virtual void GetMaximumDistance(float*) const {}                                   // [56]
    virtual bool SetMaximumDistance(float) { return false; }                           // [57]
    virtual bool SetCompletionNotification(cIGZSndCompletionFunctionObject*) { return false; }  // [58]
    virtual bool StopCompletionNotification() { return false; }                        // [59]
    virtual std::uint32_t EstimateRemainingPlayTime() { return 0; }                    // [60]
    bool SetName(const cIGZString& name) override { name_.Copy(name); return true; }   // [61] @ 0x1004c2282
    bool GetName(cIGZString& out) const override { out.Copy(name_); return true; }     // [62] @ 0x1004c2294
    bool SetSoundFileFromString(const cIGZString&, SndStreamingType) override { return false; }  // [63]
    bool SetSoundFileFromChars(const char*, std::uint32_t, SndStreamingType) override { return false; }  // [64]

    const std::set<std::int32_t>& tracks() const { return tracks_; }

private:
    std::int32_t refCount_;               // +0x14
    std::uint32_t id_;                    // +0x10
    std::int32_t volume_ = 0x400;         // +0x1c  1024 = unity
    std::set<std::int32_t> tracks_;       // +0x20
    cRZString name_;                      // +0x38
};
