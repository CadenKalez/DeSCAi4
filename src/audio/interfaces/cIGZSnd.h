// cIGZSnd — a playable sound. Vtable slots 3..36 (byte offsets +0x18..+0x120) recovered from
// cGZSnd (generic) and cHitTrackPlayer/cHitChannel (Hit engine). Volume, pan and so on are on a
// 0..1024 scale in cGZSnd. Setters return true on success.
#pragma once
#include <cstdint>

#include "gz/cIGZString.h"

class cIGZSndDataSource;

class cIGZSnd : public cIGZUnknown {
public:
    using SndDup = std::int32_t;             // original: nested enum cIGZSnd::SndDup (members not recovered)
    using SndStreamingType = std::int32_t;   // original: nested enum cIGZSnd::SndStreamingType

    virtual bool Init() = 0;                                              // [3]
    virtual bool Shutdown() = 0;                                          // [4]
    // [5] cGZSnd: sets the volume, then starts playback (`loop` selects looping).
    virtual bool PlayWithVolumeAndLoop(std::int32_t volume, bool loop, SndDup duplicatePolicy) = 0;
    // [6] cHitTrackPlayer: starts the track script with four argument registers.
    virtual bool PlayWithScriptArguments(std::int32_t arg0, std::int32_t arg1, std::int32_t arg2, std::int32_t arg3) = 0;
    virtual bool IsPlaying() const = 0;                                   // [7]
    virtual bool Stop() = 0;                                              // [8]
    virtual bool Kill() = 0;                                              // [9] cGZSnd: same as Stop
    virtual bool StopAllInstances() = 0;                                  // [10]
    virtual std::uint32_t GetUseCount() const = 0;                        // [11]
    virtual std::uint32_t GetID() const = 0;                              // [12]
    virtual void SetID(std::uint32_t id) = 0;                             // [13]
    virtual bool Load() = 0;                                              // [14]
    virtual bool Unload() = 0;                                            // [15]
    virtual bool IsLoaded() const = 0;                                    // [16]
    virtual bool Pause(bool fromBeginning) = 0;                           // [17]
    virtual bool Unpause() = 0;                                           // [18]
    virtual bool IsPaused() const = 0;                                    // [19]
    virtual std::int32_t GetVolume() const = 0;                           // [20]
    virtual bool SetVolume(std::int32_t volume) = 0;                      // [21] clamped to [0,1024] in cGZSnd
    virtual bool FadeVolume(std::int32_t fromVolume, std::int32_t toVolume, std::uint32_t durationMs, bool startFade) = 0;  // [22]
    virtual bool IsFading() const = 0;                                    // [23]
    virtual std::int32_t GetPan() const = 0;                              // [24]
    virtual bool SetPan(std::int32_t pan) = 0;                            // [25]
    virtual std::int32_t GetFrequency() const = 0;                        // [26]
    virtual bool SetFrequency(std::int32_t frequency) = 0;                // [27]
    virtual std::int32_t GetOriginalFrequency() const = 0;                // [28]
    virtual bool SetEffectsType(std::int32_t type, std::uint32_t slot) = 0;   // [29]
    virtual bool SetEffectsLevel(std::int32_t level, std::uint32_t slot) = 0; // [30]
    virtual std::int32_t GetSoundPriority() const = 0;                    // [31]
    virtual bool SetSoundPriority(std::int32_t priority) = 0;             // [32]
    virtual bool SetName(const cIGZString& name) = 0;                     // [33]
    virtual bool GetName(cIGZString& outName) const = 0;                  // [34]
    virtual bool SetSoundFileFromString(const cIGZString& path, SndStreamingType streaming) = 0;                   // [35]
    virtual bool SetSoundFileFromChars(const char* path, std::uint32_t length, SndStreamingType streaming) = 0;    // [36]
};

constexpr std::uint32_t kGZIID_cIGZSnd = 0x206077d2;  // QueryInterface id seen in cHitTrackPlayer
