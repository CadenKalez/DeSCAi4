// Game-side audio: opens Sound.dat through the recovered persistence layer, decodes records with the
// recovered data sources and plays them through AVAudioPlayer (behind the AudioPlatform seam).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

class GameAudio {
public:
    ~GameAudio();
    bool Open(const std::string& soundDat, std::string& error);
    // Ambient bed: the next suitable long sound, looped until replaced.
    void NextAmbient();
    void PlayClick();                 // short UI sound
    void SetMuted(bool muted);
    bool Muted() const { return muted_; }
    std::string CurrentName() const { return current_; }
    std::size_t SoundCount() const { return sounds_.size(); }
    void Tick();                      // call regularly: advances the ambient bed when it ends

private:
    struct Impl;
    Impl* impl_ = nullptr;
    std::vector<std::uint32_t> ambient_, clicks_;
    std::vector<std::uint32_t> sounds_;
    std::size_t nextAmbient_ = 0, nextClick_ = 0;
    bool muted_ = false;
    std::string current_;
};
