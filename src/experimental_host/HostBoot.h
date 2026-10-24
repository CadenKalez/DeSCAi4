// EARLY TEST / EXPERIMENTAL HOST — not recovered game code. See README_EARLY_TEST.md.
#pragma once

class cGZFramework;
class cSC4App;
namespace exphost {
// Runs PreFrameWorkInit -> Init -> PostFrameWorkInit (-> shutdown unless keepAlive) of the recovered cSC4App against
// the real game data.
bool RunBoot(bool verbose, bool keepAlive = false);
// The application object RunBoot built (null before it ran).
cSC4App* BootApplication();
// The framework RunBoot built (null before it ran); with keepAlive it stays up so GZCOM() and the services work.
cGZFramework* BootFramework();
}
