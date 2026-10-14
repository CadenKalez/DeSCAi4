// Platform seam of the sound system. The original plays sounds through Apple's AVAudioPlayer
// (CPCMPlayer wraps a player created from an in-memory PCM buffer, CQTRemotePlayer one created
// from a file) and runs fade updates from an SDL timer. The port keeps the original classes and
// injects the player factory and timer service through this interface; the engine integration
// supplies an AVAudioPlayer-, miniaudio- or SDL-based backend (see notes/audio-integration.md).
#pragma once
#include <cstdint>

namespace AudioPlatform {

// One playable voice (an AVAudioPlayer).
class IVoice {
public:
    virtual ~IVoice() = default;
    // `loops` follows AVAudioPlayer: 0 plays once, -1 loops forever.
    virtual void Play(std::int32_t loops) = 0;
    virtual void Stop() = 0;
    virtual bool IsPlaying() const = 0;
    virtual void SetVolume(float volume) = 0;   // 0..1
    virtual void SetPan(float pan) = 0;         // -1 (left) .. 1 (right)
};

using TimerCallback = std::uint32_t (*)(std::uint32_t intervalMs, void* context);

class IBackend {
public:
    virtual ~IBackend() = default;
    // Builds a voice over `data` (interleaved PCM). Null on failure.
    virtual IVoice* CreatePcmVoice(const std::uint8_t* data, std::int32_t size, std::uint16_t channels,
                                   std::uint32_t sampleRate, std::uint16_t bitsPerSample) = 0;
    // Builds a streamed voice for an audio file. Null on failure.
    virtual IVoice* CreateFileVoice(const char* path) = 0;
    // SDL_AddTimer / SDL_RemoveTimer: the callback returns the next interval (0 stops).
    virtual std::uint32_t AddTimer(std::uint32_t intervalMs, TimerCallback callback, void* context) = 0;
    virtual void RemoveTimer(std::uint32_t timerId) = 0;
};

IBackend* GetBackend();
void SetBackend(IBackend* backend);

}  // namespace AudioPlatform
