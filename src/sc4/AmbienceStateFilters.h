// The 16 ambience "state" filters (cAmbienceFilterIs*): global yes/no questions about time of day, sim speed,
// view level, zoom and driving mode (0x1003e48b6..0x1003e7xxx). Layout: +0 vptr, +8 refcount. The game
// services they ask are behind IAmbienceStateHost.
#pragma once
#include <cstdint>

#include "gz/cIGZUnknown.h"

namespace sc4 {

constexpr std::uint32_t kGZIID_cIAmbienceFilter = 0xea40074f;

class IAmbienceStateHost {
public:
    virtual ~IAmbienceStateHost() = default;
    virtual bool IsNight() = 0;           // lighting manager vslot 0x48
    virtual bool IsDrivingMode() = 0;     // vehicle manager vslot 0x68
    virtual int View3DLevel() = 0;        // view3D vslot 0xb0: 1 surface, 2 subway, 3 pipe
    virtual int SimSpeed() = 0;           // simulator vslot 0xd8: 0 slow, 1 medium, 2 fast
    virtual bool SimIsActive() = 0;       // simulator vslot 0x78
    virtual bool SimIsRunning() = 0;      // simulator vslot 0x88
    virtual int ZoomLevel() = 0;          // SC4ViewUtilities::GetZoomAndRotation, zoom 0..4
};
IAmbienceStateHost* AmbienceStateHost();
void SetAmbienceStateHost(IAmbienceStateHost* host);

class cIAmbienceFilter : public cIGZUnknown {
public:
    virtual bool Init() = 0;      // [3]
    virtual bool Shutdown() = 0;  // [4]
    virtual bool IsTrue() = 0;    // [5]
};

class cAmbienceStateFilter : public cIAmbienceFilter {
public:
    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return ++refCount_; }
    std::uint32_t Release() override;
    bool Init() override { return true; }
    bool Shutdown() override { return true; }

protected:
    virtual ~cAmbienceStateFilter() = default;
    std::uint32_t refCount_ = 0;     // +0x08
};

#define AMBIENCE_STATE_FILTER(Name) \
    class Name final : public cAmbienceStateFilter { \
    public: \
        static Name* Create() { return new Name(); } \
        bool IsTrue() override; \
    }

AMBIENCE_STATE_FILTER(cAmbienceFilterIsAlways);
AMBIENCE_STATE_FILTER(cAmbienceFilterIsDay);
AMBIENCE_STATE_FILTER(cAmbienceFilterIsNight);
AMBIENCE_STATE_FILTER(cAmbienceFilterIsDrivingMode);
AMBIENCE_STATE_FILTER(cAmbienceFilterIsSurfaceView);
AMBIENCE_STATE_FILTER(cAmbienceFilterIsSubwayView);
AMBIENCE_STATE_FILTER(cAmbienceFilterIsPipeView);
AMBIENCE_STATE_FILTER(cAmbienceFilterIsSimSpeedPaused);
AMBIENCE_STATE_FILTER(cAmbienceFilterIsSimSpeedSlow);
AMBIENCE_STATE_FILTER(cAmbienceFilterIsSimSpeedMedium);
AMBIENCE_STATE_FILTER(cAmbienceFilterIsSimSpeedFast);
AMBIENCE_STATE_FILTER(cAmbienceFilterIsZoom1);
AMBIENCE_STATE_FILTER(cAmbienceFilterIsZoom2);
AMBIENCE_STATE_FILTER(cAmbienceFilterIsZoom3);
AMBIENCE_STATE_FILTER(cAmbienceFilterIsZoom4);
AMBIENCE_STATE_FILTER(cAmbienceFilterIsZoom5);
#undef AMBIENCE_STATE_FILTER

}  // namespace sc4
