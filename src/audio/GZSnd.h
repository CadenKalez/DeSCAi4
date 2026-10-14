// cGZSnd — the generic playable sound: a data source (decoded PCM, played through CPCMPlayer) or an
// audio file (played through CQTRemotePlayer), with volume, pan, frequency and timed fades.
// Recovered from Sim City 4 Deluxe Editionsub 1.2.1 (vtable 0x100ad91d0, 0xf0-byte object,
// ctor @ 0x1006253a0, Create @ 0x100624ec0).
//
// Bases/offsets as in the binary: cIGZSnd +0, cIGZSndDataSourceHolder +8,
// cIGZSndCompletionNotification +0x10; refcount +0x18; sound system +0x20; lock +0x28; file path
// +0x38; "file set" flag +0x60; id +0x64; name +0x68; "is data source" +0x90; PCM player +0x98;
// QuickTime-style file player +0xc0; volume +0xc8; data source +0xd0; fade state +0xd8..+0xe8.
//
// Notes on preserved behaviour:
//  * QueryInterface does not accept cIGZUnknown's id 1 (only 0x206077d2, 0x9d002d2, 0xe9d00a9a).
//  * Pan and frequency only affect PCM sounds; completion notifications are not implemented
//    (SetCompletionNotification returns false).
//  * Release() shuts the sound down (stops, unloads, drops the data source) before destroying it.
//  * After Stop(), PCM sounds keep reporting IsPlaying() (see CPCMPlayer).
#pragma once
#include <cstdint>
#include <mutex>
#include <string>

#include "audio/PCMPlayer.h"
#include "audio/interfaces/cIGZSnd.h"
#include "audio/interfaces/cIGZSndCompletionFunctionObject.h"
#include "audio/interfaces/cIGZSndCompletionNotification.h"
#include "audio/interfaces/cIGZSndDataSource.h"
#include "audio/interfaces/cIGZSndDataSourceHolder.h"
#include "audio/interfaces/cIGZSndUpdateTarget.h"
#include "rz/cRZAutoRefCount.h"

class cGZSnd : public cIGZSnd, public cIGZSndDataSourceHolder, public cIGZSndCompletionNotification {
public:
    cGZSnd();                                                          // @ 0x1006253a0
    virtual ~cGZSnd();                                                 // @ 0x100625770
    static cGZSnd* Create() { return new cGZSnd(); }                   // @ 0x100624ec0

    bool QueryInterface(std::uint32_t iid, void** out) override;       // @ 0x100625a30
    std::uint32_t AddRef() override { return static_cast<std::uint32_t>(++refCount_); }   // @ 0x100625b60
    std::uint32_t Release() override;                                  // @ 0x100625bc0

    // --- cIGZSnd ---
    bool Init() override { return true; }                              // @ 0x100625c80
    bool Shutdown() override;                                          // @ 0x100625ca0
    bool PlayWithVolumeAndLoop(std::int32_t volume, bool loop, SndDup duplicatePolicy) override;   // @ 0x100625d90
    bool PlayWithScriptArguments(std::int32_t, std::int32_t, std::int32_t, std::int32_t) override { return false; }   // @ 0x100625e90
    bool IsPlaying() const override;                                   // @ 0x100625ec0
    bool Stop() override;                                              // @ 0x100625f30
    bool Kill() override { return Stop(); }                            // @ 0x100626060
    bool StopAllInstances() override { return false; }                 // @ 0x100626090
    std::uint32_t GetUseCount() const override { return 0; }           // @ 0x1006260b0
    std::uint32_t GetID() const override { return id_; }               // @ 0x1006260c0
    void SetID(std::uint32_t id) override { id_ = id; }                // @ 0x1006260e0
    bool Load() override;                                              // @ 0x100626100
    bool Unload() override;                                            // @ 0x100626520
    bool IsLoaded() const override;                                    // @ 0x100626620
    bool Pause(bool) override { return Stop(); }                       // @ 0x100626690
    bool Unpause() override { return PlayWithVolumeAndLoop(GetVolume(), false, 1); }   // @ 0x1006266c0
    bool IsPaused() const override { return !IsPlaying(); }            // @ 0x100626710
    std::int32_t GetVolume() const override { return volume_; }        // @ 0x100626880
    bool SetVolume(std::int32_t volume) override;                      // @ 0x1006268a0
    bool FadeVolume(std::int32_t fromVolume, std::int32_t toVolume, std::uint32_t durationMs, bool startFade) override;   // @ 0x100626990
    bool IsFading() const override { return fading_; }                 // @ 0x100626ad0
    std::int32_t GetPan() const override { return 0; }                // @ 0x100626af0
    bool SetPan(std::int32_t pan) override;                            // @ 0x100626b00
    std::int32_t GetFrequency() const override;                        // @ 0x100626bd0
    bool SetFrequency(std::int32_t frequency) override;                // @ 0x100626c20
    std::int32_t GetOriginalFrequency() const override;                // @ 0x100626cb0
    bool SetEffectsType(std::int32_t, std::uint32_t) override { return false; }    // @ 0x100626d00
    bool SetEffectsLevel(std::int32_t, std::uint32_t) override { return false; }   // @ 0x100626d20
    std::int32_t GetSoundPriority() const override { return 0; }       // @ 0x100626d40
    bool SetSoundPriority(std::int32_t) override { return false; }     // @ 0x100626d50
    bool SetName(const cIGZString& name) override;                     // @ 0x100626d60
    bool GetName(cIGZString& name) const override;                     // @ 0x100626da0
    bool SetSoundFileFromString(const cIGZString& path, SndStreamingType streaming) override;   // @ 0x100626de0
    bool SetSoundFileFromChars(const char* path, std::uint32_t length, SndStreamingType streaming) override;   // @ 0x100626e50

    // --- cIGZSndDataSourceHolder ---
    bool SetDataSource(cIGZSndDataSource* source, SndStreamingType streaming) override;   // @ 0x100626f20
    cIGZSndDataSource* GetDataSource() override;                       // @ 0x100627010

    // --- cIGZSndCompletionNotification ---
    bool SetCompletionNotification(cIGZSndCompletionFunctionObject*) override { return false; }   // @ 0x1006270c0
    bool StopCompletionNotification() override { return false; }       // @ 0x100627110
    std::uint32_t EstimateRemainingPlayTime() override { return 0; }   // @ 0x100627150

    // cIGZSndUpdateTarget-style tick (slot 39 of the primary vtable): advances a running fade.
    // The original drives it from a 20 ms SDL timer.
    virtual void Update(std::int32_t elapsedMs);                       // @ 0x100626740

protected:
    // The primary sound system's master volume (0..1024), or 1024 when none is installed.
    std::int32_t MasterVolume() const;
    void StopFadeUpdates();                                            // @ 0x100626030
    void StartFadeUpdates();                                           // @ 0x100626aa0
    void UpdateUpdates();                                              // @ 0x100627180
    static std::uint32_t TimerProc(std::uint32_t interval, void* context);   // @ 0x100627250

    mutable std::recursive_mutex lock_;                                // +0x28
    CPCMPlayer pcm_;                                                   // +0x98
    CQTRemotePlayer file_;                                             // +0xc0
    bool dirtyForReload_ = false;                                      // +0xe4: source or file changed
    bool dataSourceMode_ = false;                                      // +0x90

private:
    std::int32_t refCount_ = 0;                                        // +0x18
    std::string path_;                                                 // +0x40 (inside the cRZString at +0x38)
    bool fileSet_ = false;                                             // +0x60
    std::uint32_t id_ = 0;                                             // +0x64
    std::string name_;                                                 // +0x68
    std::int32_t volume_ = 0x400;                                      // +0xc8
    cRZAutoRefCount<cIGZSndDataSource> dataSource_;                    // +0xd0
    bool timerRunning_ = false;                                        // +0xd8
    bool fading_ = false;                                              // +0xd9
    std::int32_t fadeStep_ = 0;                                        // +0xdc
    std::int32_t fadeTarget_ = 0;                                      // +0xe0
    std::uint32_t timerId_ = 0;                                        // +0xe8
};
