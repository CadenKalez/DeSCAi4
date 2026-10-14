// CPCMPlayer / CQTRemotePlayer — the Mac player wrappers used by cGZSnd. Recovered from Sim City 4
// Deluxe Editionsub 1.2.1 (CPCMPlayer @ 0x10043c290..0x10043c8b0, CQTRemotePlayer @
// 0x10069931a..0x100699486). The AVAudioPlayer calls go through AudioPlatform::IVoice.
//
// Quirks preserved: CPCMPlayer::IsPlaying() is a sticky flag set by Play() and never cleared by
// Stop() or Close(); the frequency is stored but never applied; the left/right volumes only
// influence the balance of 3D sounds.
#pragma once
#include <cstdint>
#include <memory>

#include "audio/platform/AudioPlatform.h"

class CPCMPlayer {
public:
    CPCMPlayer() = default;                                    // @ 0x10043c290
    ~CPCMPlayer() { Close(); }                                 // @ 0x10043c300
    CPCMPlayer(const CPCMPlayer&) = delete;
    CPCMPlayer& operator=(const CPCMPlayer&) = delete;

    // @ 0x10043c3a0: takes ownership of `data` (a new[] buffer), always frees it after the player
    // was created. Returns whether the player could be created.
    bool Open(std::uint16_t channels, std::uint16_t bitsPerSample, std::uint32_t sampleRate, std::uint8_t* data, std::int32_t size);
    void Close();                                              // @ 0x10043c320
    bool IsOpen() const { return voice_ != nullptr; }          // @ 0x10043c5c0
    void Stop();                                               // @ 0x10043c5e0
    void Play(bool loop);                                      // @ 0x10043c640
    void SetVolume(float volume);                              // @ 0x10043c6f0
    void SetVolumes(float left, float right);                  // @ 0x10043c740
    void SetBalance(float balance);                            // @ 0x10043c7c0 (stored; not applied)
    void SetFrequency(std::uint32_t frequency);                // @ 0x10043c810
    bool IsPlaying() const { return playing_; }                // @ 0x10043c850
    void SetIs3D(bool is3D) { is3D_ = is3D; }                  // @ 0x10043c870
    std::uint32_t GetOriginalFrequency() const { return originalFrequency_; }   // @ 0x10043c890
    std::uint32_t GetFrequency() const { return frequency_; }  // @ 0x10043c8b0

private:
    void UpdateVolumeAndBalance();                             // @ 0x10043c4a0
    void UpdateFrequency() {}                                  // @ 0x10043c570: no effect

    bool is3D_ = false;                // +0
    bool loop_ = false;                // +1
    bool playing_ = false;             // +2
    float volume_ = 1.0f;              // +4
    float leftVolume_ = 1.0f;          // +8
    float rightVolume_ = 1.0f;         // +0xc
    float balance_ = 0.0f;             // +0x10
    std::uint32_t frequency_ = 0;      // +0x14
    std::uint32_t originalFrequency_ = 0;   // +0x18 (reset by Close, never set otherwise)
    std::unique_ptr<AudioPlatform::IVoice> voice_;   // +0x20
};

class CQTRemotePlayer {
public:
    CQTRemotePlayer() = default;                               // @ 0x10069931a
    CQTRemotePlayer(const CQTRemotePlayer&) = delete;
    CQTRemotePlayer& operator=(const CQTRemotePlayer&) = delete;

    bool Open(const char* path);                               // @ 0x10069933e (replaces any player)
    void Close() { voice_.reset(); }                           // @ 0x1006993de
    void Play(bool loop);                                      // @ 0x100699406
    void Stop();                                               // @ 0x10069943e
    bool IsPlaying() const { return voice_ && voice_->IsPlaying(); }   // @ 0x100699454
    void SetVolume(float volume) { if (voice_) voice_->SetVolume(volume); }   // @ 0x100699470
    bool IsOpen() const { return voice_ != nullptr; }          // @ 0x100699486

private:
    std::unique_ptr<AudioPlatform::IVoice> voice_;
};
