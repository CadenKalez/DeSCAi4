// Host seams for the SimCity 4 audio layer (cSC4Audio*). The original reaches the rest of the
// game through service-locator globals (SFX::SoundManager(), SL::ViewUtilities(), SL::City(), ...);
// the port injects the few services the recovered code needs. Implemented by the game integration
// (see notes/audio-integration.md section 10).
#pragma once
#include <cstdint>

#include "gz/cIGZUnknown.h"

namespace SC4Audio {

// The master volume (0..1024) kept by cSC4AudioSoundManager; the emanation models scale by it.
class cIMasterVolume : public cIGZUnknown {
public:
    virtual std::int32_t GetMasterVolume() = 0;
};

// SFX::SoundManager() @ 0x...: the registered sound manager, or null.
cIMasterVolume* SoundManager();
void SetSoundManager(cIMasterVolume* manager);

// SC4ViewUtilities as used by the 3D-with-effects model.
class cIViewport {
public:
    virtual ~cIViewport() = default;
    // Projects a world point (x, y, z) to viewport pixel coordinates in place (x, y written).
    virtual void ProjectPointToViewportCoordinates2D(float point[3]) = 0;
    virtual std::int32_t ViewportWidth() const = 0;    // +0x30
    virtual std::int32_t ViewportHeight() const = 0;   // +0x34
};
cIViewport* ViewUtilities();   // SL::spViewUtilities
void SetViewUtilities(cIViewport* viewport);

// cSC4AudioListener::afZoomDistances @ 0x100b6ddf0: camera distance per zoom level (index 0..4),
// written by cSC4AudioListener::_CalculateZoomValues and read by the 3D sems and ambience layers.
float* ZoomDistances();

}  // namespace SC4Audio
