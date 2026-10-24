// EARLY TEST / EXPERIMENTAL HOST - not recovered game code. See README_EARLY_TEST.md.
// The game loop. RunBoot stops after PostFrameWorkInit; this keeps the application alive, lets the recovered cWinSC4App
// create the splash screen and swap to the region screen, and pumps the message servers and the app tick for N frames.
// That is the difference between "the app object initialises" and "the game is running".
//
// NOT IN THE HOST LINK YET, deliberately: linking it pulls in cWinSC4App, which pulls in cSC4View3DWin, and that file is
// being recovered by a parallel agent right now. Add src/experimental_host/HostGame.cpp to scripts/host_link_list.txt
// (plus src/sc4/WinSC4App.cpp and the view) once the view is stable, then call RunGame from host_main's --play.
#include "experimental_host/HostBoot.h"
#include "experimental_host/HostUi.h"
#include "experimental_host/HostWinMgr.h"
#include "experimental_host/pending/HostWinSC4App.h"

#include <cstdio>

#include "sc4/App.h"
#include "sc4/ServiceLocator.h"

namespace exphost {

// RunGame - the boot, then the game's own screen chain driven in a loop. RunBoot stops after PostFrameWorkInit; this
// keeps the application alive, lets cWinSC4App create the splash screen and swap to the region screen, and pumps the
// message server plus the app tick for a number of frames. It is the difference between "the app object initialises" and
// "the game is running".
bool RunGame(int frames, bool verbose) {
    if (!RunBoot(verbose, true)) {
        std::printf("boot failed, not starting the game loop\n");
        return false;
    }
    auto* framework = exphost::BootFramework();
    cSC4App* app = BootApplication();
    if (!app) { std::printf("no application object after the boot\n"); return false; }

    static HostWinSC4App host(framework);
    host.SetMessageServer(SL::spMS2);
    app->StartupHooks().createApplicationWindow = [&](cSC4App& a) -> sc4::AppStartupHooks::ApplicationWindow {
        auto* window = new sc4::cWinSC4App(&a, g_mgr, &host);
        static sc4::WinSC4AppApplication appView(window);
        return {static_cast<cIGZWin*>(window), &appView};
    };
    std::printf("game: the screen chain is wired, pumping %d frames\n", frames);
    for (int i = 0; i < frames; ++i) {
        app->RunMessageServerPump(0, 64, 4);
        app->RunMessageServer2Pump(0, 64, 4);
        app->OnTick(16);
    }
    std::printf("game: %d frames pumped, music requested: %s\n", frames, host.readyToHearMusic() ? "yes" : "not yet");
    return true;
}
}  // namespace exphost
