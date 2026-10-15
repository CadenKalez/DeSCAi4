#include "sc4/AmbienceStateFilters.h"

namespace sc4 {

static IAmbienceStateHost* g_host = nullptr;
IAmbienceStateHost* AmbienceStateHost() { return g_host; }
void SetAmbienceStateHost(IAmbienceStateHost* host) { g_host = host; }

bool cAmbienceStateFilter::QueryInterface(std::uint32_t iid, void** out) {
    if (iid != kGZIID_cIAmbienceFilter && iid != 0x4a40074c) return false;
    *out = this;
    AddRef();
    return true;
}

std::uint32_t cAmbienceStateFilter::Release() {
    if (refCount_ > 1) return --refCount_;
    delete this;
    return 0;
}

bool cAmbienceFilterIsAlways::IsTrue() { return true; }
bool cAmbienceFilterIsDay::IsTrue() { return !g_host->IsNight(); }
bool cAmbienceFilterIsNight::IsTrue() { return g_host->IsNight(); }
bool cAmbienceFilterIsDrivingMode::IsTrue() { return g_host->IsDrivingMode(); }
bool cAmbienceFilterIsSurfaceView::IsTrue() { return g_host->View3DLevel() == 1; }
bool cAmbienceFilterIsSubwayView::IsTrue() { return g_host->View3DLevel() == 2; }
bool cAmbienceFilterIsPipeView::IsTrue() { return g_host->View3DLevel() == 3; }
bool cAmbienceFilterIsSimSpeedPaused::IsTrue() { return g_host->SimIsActive() && !g_host->SimIsRunning(); }
bool cAmbienceFilterIsSimSpeedSlow::IsTrue() { return g_host->SimSpeed() == 0; }
bool cAmbienceFilterIsSimSpeedMedium::IsTrue() { return g_host->SimSpeed() == 1; }
bool cAmbienceFilterIsSimSpeedFast::IsTrue() { return g_host->SimSpeed() == 2; }
bool cAmbienceFilterIsZoom1::IsTrue() { return g_host->ZoomLevel() == 0; }
bool cAmbienceFilterIsZoom2::IsTrue() { return g_host->ZoomLevel() == 1; }
bool cAmbienceFilterIsZoom3::IsTrue() { return g_host->ZoomLevel() == 2; }
bool cAmbienceFilterIsZoom4::IsTrue() { return g_host->ZoomLevel() == 3; }
bool cAmbienceFilterIsZoom5::IsTrue() { return g_host->ZoomLevel() == 4; }

}  // namespace sc4
