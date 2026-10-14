// cSC4AudioListener — keeps the 3D sound system's listener (position and orientation) in step
// with the game camera. The listener hovers above the point the camera looks at, at a height
// that depends on the zoom level, looking straight down; whenever the camera has moved far enough
// (a per-zoom-level squared-distance threshold) it posts message 0x8a4a69c7 so ambience code can
// re-evaluate. Recovered from Sim City 4 Deluxe Editionsub 1.2.1 (vtable 0x100aac690, 0xb8-byte
// object, ctor @ 0x1003f4af0, UpdateListener @ 0x1003f62c0).
//
// Layout: cIGZMessageTarget2 +0, refcount +8, initialised +0xc, eye +0x10, "pull" point +0x1c,
// target +0x28, last notified camera position +0x34, zoom distance table +0x40, zoom multiplier
// table +0x58, camera pull table +0x70, movement threshold table +0x88, zoom scale +0xa0,
// rolloff factor +0xa4, sound system +0xa8, message server +0xb0.
#pragma once
#include <cstdint>
#include <vector>

#include "gz/cIGZMessage2.h"
#include "rz/cRZAutoRefCount.h"
#include "sc4audio/SC4AudioHost.h"

namespace SC4Audio {
inline constexpr std::uint32_t kMessageCityInit = 0x26d31ec1;        // Init registers; city starts
inline constexpr std::uint32_t kMessageCityShutdown = 0x26d31ec2;
inline constexpr std::uint32_t kMessageCameraMoved = 0xa6b79602;      // -> UpdateListener
inline constexpr std::uint32_t kMessageCameraZoomed = 0xa6b79612;     // -> UpdateListener
inline constexpr std::uint32_t kMessageCheatCode = 0x230e27ac;        // parsed, no effect
inline constexpr std::uint32_t kMessageListenerMoved = 0x8a4a69c7;    // posted by UpdateListener
inline constexpr std::uint32_t kEventListenerTuningChanged = 0xaa36d9b6;   // posted to the sound system
inline constexpr int kZoomLevels = 5;
}  // namespace SC4Audio

class cSC4AudioListener final : public cIGZMessageTarget2 {
public:
    cSC4AudioListener() = default;                       // @ 0x1003f4af0
    static cSC4AudioListener* Create() { return new cSC4AudioListener(); }   // @ 0x1003dee96

    // ids 1, 0xca4a66b9, 0x452294aa (all the same pointer) @ 0x1003f7c30; Release destroys below 2.
    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return static_cast<std::uint32_t>(++refCount_); }   // @ 0x1003f7b70
    std::uint32_t Release() override;                    // @ 0x1003f7b90

    bool DoMessage(cIGZMessage2* message) override;      // @ 0x1003f5c00

    bool Init();                                         // @ 0x1003f57d0
    bool Shutdown();                                     // @ 0x1003f5a60
    // @ 0x1003f62c0. False when there is no occupant manager; true (doing nothing) when there is
    // no view utility.
    bool UpdateListener();

    // Adapter for cIGameAudioContext::GetListenerView.
    SC4Audio::ListenerView View() const;
    const float* eye() const { return eye_; }
    const float* target() const { return target_; }
    const float* pullPoint() const { return pull_; }
    const float* lastNotifiedCameraPosition() const { return lastCamera_; }
    bool initialised() const { return initialised_; }

private:
    ~cSC4AudioListener() = default;
    bool HandleCheatCodeMessage(cIGZMessage2* message);          // @ 0x1003f5e80
    bool HandleCityInitShutdownMessages(cIGZMessage2* message);  // @ 0x1003f5f80
    void ReadTuningValues();                                     // @ 0x1003f6b30
    void CalculateZoomValues();                                  // @ 0x1003f7330

    std::int32_t refCount_ = 0;                  // +8
    bool initialised_ = false;                   // +0xc
    float eye_[3] = {0, 0, 0};                   // +0x10
    float pull_[3] = {0, 0, 0};                  // +0x1c
    float target_[3] = {0, 0, 0};                // +0x28
    float lastCamera_[3] = {0, 0, 0};            // +0x34
    std::vector<float> zoomDistances_;           // +0x40 (prop 0xca359f17)
    std::vector<float> zoomMultipliers_;         // +0x58 (prop 0x6a359f22)
    std::vector<float> cameraPulls_;             // +0x70 (prop 0xea4a7c55), not read by recovered code
    std::vector<float> movementThresholds_;      // +0x88 (prop 0x2aa40ccc), stored squared * 256
    float zoomScale_ = 1.0f;                     // +0xa0 (prop 0x2a35a25f)
    float rolloffFactor_ = 4.0f;                 // +0xa4 (prop 0x2a36e3cb)
    cRZAutoRefCount<cIGZSndSys> soundSystem_;    // +0xa8
    cRZAutoRefCount<cIGZMessageServer2> messageServer_;   // +0xb0
};
