#include <cstdio>

#include "sc4/AmbienceStateFilters.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace sc4;

namespace {
struct Host final : IAmbienceStateHost {
    bool night = false, driving = false, active = true, running = true;
    int view = 1, speed = 0, zoom = 0;
    bool IsNight() override { return night; }
    bool IsDrivingMode() override { return driving; }
    int View3DLevel() override { return view; }
    int SimSpeed() override { return speed; }
    bool SimIsActive() override { return active; }
    bool SimIsRunning() override { return running; }
    int ZoomLevel() override { return zoom; }
};
template <class T> bool Is() { auto* f = T::Create(); f->AddRef(); const bool r = f->IsTrue(); f->Release(); return r; }
}  // namespace

int main() {
    Host h;
    SetAmbienceStateHost(&h);
    CHECK(Is<cAmbienceFilterIsAlways>() && Is<cAmbienceFilterIsDay>() && !Is<cAmbienceFilterIsNight>());
    h.night = true;
    CHECK(!Is<cAmbienceFilterIsDay>() && Is<cAmbienceFilterIsNight>());
    h.driving = true; CHECK(Is<cAmbienceFilterIsDrivingMode>());
    CHECK(Is<cAmbienceFilterIsSurfaceView>() && !Is<cAmbienceFilterIsSubwayView>() && !Is<cAmbienceFilterIsPipeView>());
    h.view = 3; CHECK(Is<cAmbienceFilterIsPipeView>());
    h.view = 2; CHECK(Is<cAmbienceFilterIsSubwayView>());
    CHECK(!Is<cAmbienceFilterIsSimSpeedPaused>() && Is<cAmbienceFilterIsSimSpeedSlow>());
    h.running = false; CHECK(Is<cAmbienceFilterIsSimSpeedPaused>());
    h.active = false; CHECK(!Is<cAmbienceFilterIsSimSpeedPaused>());
    h.speed = 1; CHECK(Is<cAmbienceFilterIsSimSpeedMedium>() && !Is<cAmbienceFilterIsSimSpeedFast>());
    h.speed = 2; CHECK(Is<cAmbienceFilterIsSimSpeedFast>());
    h.zoom = 0; CHECK(Is<cAmbienceFilterIsZoom1>() && !Is<cAmbienceFilterIsZoom2>());
    h.zoom = 1; CHECK(Is<cAmbienceFilterIsZoom2>());
    h.zoom = 2; CHECK(Is<cAmbienceFilterIsZoom3>());
    h.zoom = 3; CHECK(Is<cAmbienceFilterIsZoom4>());
    h.zoom = 4; CHECK(Is<cAmbienceFilterIsZoom5>());
    auto* f = cAmbienceFilterIsDay::Create();
    f->AddRef();
    void* out = nullptr;
    CHECK(f->QueryInterface(0x4a40074c, &out) && out == f && !f->QueryInterface(7, &out));
    f->Release(); f->Release();
    std::printf(failures ? "FAILED\n" : "sc4_ambiencestatefilters ok\n");
    return failures != 0;
}
