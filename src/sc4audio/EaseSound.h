// cEaseSound — eases a sound's position and volume toward targets at limited speeds, so abrupt
// changes are smoothed. Recovered from Sim City 4 Deluxe Editionsub 1.2.1 (ctor @ 0x1003ba976,
// 0x40-byte object). Speeds are given per second (SetDelta) and applied per Update(seconds).
#pragma once
#include <cmath>

class cEaseSound {
public:
    cEaseSound() = default;                                   // @ 0x1003ba976 (everything zero)

    // @ 0x1003ba9aa: sets the target position; the current position jumps there when `snap` is set
    // or before the first Update.
    void SetPosition(const float position[3], bool snap);
    void GetPosition(float out[3]) const;                     // @ 0x1003ba9dc: the eased (current) position
    // @ 0x1003ba9f4
    void SetVolume(int volume, bool snap);
    int GetVolume() const { return currentVolume_; }          // @ 0x1003baa0a
    // @ 0x1003baa14: moves the current position/volume at most speed * seconds towards the targets.
    void Update(float seconds);
    // @ 0x1003baaec: speeds in units per second (stored per millisecond, /1000 as in the original).
    void SetDelta(float positionSpeed, float volumeSpeed) {
        positionStep_ = positionSpeed / 1000.0f;
        volumeStep_ = volumeSpeed / 1000.0f;
    }

private:
    float target_[3] = {0, 0, 0};        // +0
    float current_[3] = {0, 0, 0};       // +0xc
    int targetVolume_ = 0;               // +0x18
    int currentVolume_ = 0;              // +0x1c
    float positionStep_ = 0;             // +0x20
    float volumeStep_ = 0;               // +0x24
    [[maybe_unused]] float unused_[4] = {0, 0, 0, 0};   // +0x28
    bool updated_ = false;               // +0x38 set by the first Update
};
