// Platform glue of the native Metal application (no recovered counterpart on macOS); derived from the early host.
#pragma once
#include <functional>

class cGZFramework;
namespace gd { class cIGZGDriver; }
class cSC4App;
namespace native {
// Runs PreFrameWorkInit -> Init -> PostFrameWorkInit (-> shutdown unless keepAlive) of the recovered cSC4App against
// the real game data.
// `beforeInit` runs after the framework and the application object exist and before PreFrameWorkInit: the place to register the window
// manager as a system service and to set the start-up hooks (the application window is created inside Init).
bool RunBoot(bool verbose, bool keepAlive = false, gd::cIGZGDriver* driver = nullptr,
             const std::function<void(cGZFramework&, cSC4App&)>& beforeInit = {});
// The application object RunBoot built (null before it ran).
cSC4App* BootApplication();
// The framework RunBoot built (null before it ran); with keepAlive it stays up so GZCOM() and the services work.
cGZFramework* BootFramework();
}
