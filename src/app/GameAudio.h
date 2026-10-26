// Game-side audio: opens Sound.dat through the recovered persistence layer, decodes records with the
// recovered data sources and plays them through AVAudioPlayer (behind the AudioPlatform seam).
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

class GameAudio {
public:
    ~GameAudio();
    bool Open(const std::string& soundDat, std::string& error);
    // Ambient bed: the next suitable long sound, looped until replaced.
    void NextAmbient();
    void PlayClick();                 // short UI sound (placeholder; the game's own ids go through PlayTrack)
    // The game's sound ids are Hit track records of Sound.dat ({0x0b8d821a, 0xca4d1943, id}): "TKDT,2,,<sound>,<property set>,...".
    // Resolves the track to its sample and plays it; false when the id is not a track or has no decodable sample.
    bool PlayTrack(std::uint32_t trackId);
    // The radio: the mp3s of GameData/Radio/Stations/<station>/Music play one after another in a shuffled order
    // ("Mayor" in a city, "Region" on the region screen); an empty name stops the music.
    void SetStation(const std::string& stationDirectory);
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
    std::string stationDirectory_;                  // the current station's Music folder
    std::vector<std::string> playlist_;
    std::size_t nextSong_ = 0;
    std::map<std::uint32_t, std::uint32_t> trackSound_;   // track id -> sample instance (0 = not a track)
    bool muted_ = false;
    std::string current_;
};
