// The city view's render stack on the native Metal application: what cSC4View3DWin::DoPreCityInit / DoPostCityInit build when a
// city comes up (the camera, the camera control and the 3D renderer) and the window attaches them.
#pragma once

namespace gd { class cIGZGDriver; }
namespace gz { class cIGZDrawContext; }
namespace sc4 { class cSC4View3DWin; }
class cISC4City;

namespace native {

// Builds the stack for the city that is loaded and attaches it to the view; true when the renderer came up.
bool AttachCityRenderStack(sc4::cSC4View3DWin& view, cISC4City* city, gd::cIGZGDriver* driver, gz::cIGZDrawContext* screen, int viewportWidth, int viewportHeight);

}  // namespace native
