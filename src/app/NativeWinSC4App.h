// Platform glue of the native Metal application (no recovered counterpart on macOS); derived from the early host.
// application window: it creates the splash screen, the region screen and the city screen, and swaps between them. Every
// engine call it makes goes through IWinSC4AppHost, so this file is the whole surface the host has to provide for the
// game's own screen chain to run instead of the host's painted stand-in.
//
// The services the host does not have yet are answered with "absent" (null / false), which is what the original does when
// the service is missing: the window skips that feature and keeps going. Nothing here fakes a game behaviour.
#pragma once
#include "sc4/WinSC4App.h"

namespace native {

class HostWinSC4App final : public sc4::IWinSC4AppHost {
public:
    explicit HostWinSC4App(cGZFramework* framework) : framework_(framework) {}

    // SL::spGS and its 0xc47b747c view: the host's graphic system object is the app's stub.
    cIGZUnknown* GraphicSystem() override { return graphicSystem_; }
    void SetGraphicSystem(cIGZUnknown* graphics) { graphicSystem_ = graphics; }

    // RZ::RDD: the realtime debug display. The host has none, so the fps overlay is simply not registered.
    cIGZUnknown* RealtimeDebugDisplay() override { return nullptr; }
    void AddDebugTextDisplay(cIGZString*) override {}
    void RemoveDebugTextDisplay(cIGZString*) override {}
    bool ShowFps() override { return false; }

    cIGZMessageServer2* MessageServer2() override { return messageServer_; }
    void SetMessageServer(cIGZMessageServer2* server) { messageServer_ = server; }

    // The cheat-code manager, the command server, the preferences and the cheat dialog are engine objects the host does
    // not build yet; the window skips those paths when they are absent.
    cGZCheatCodeManager* CheatCodeManager() override { return nullptr; }
    void ExecuteCommand(std::uint32_t) override {}
    bool ClockWindowPreference() override { return false; }
    void DoModelessClockWindow() override {}
    cIGZWin* CreateCheatCodeEntryDialog() override { return nullptr; }
    void AppFocus(bool) override {}

    // _sReadyToHearMusic = 1 at the end of CreateRegionScreen. The host records it so a caller can see the region screen
    // got far enough to ask for its music, which is the signal the audio path keys off.
    // The keyboard accelerator the key-unhandled path uses (resource {a2e3d533, 6a231eaa, 6a9362f1} then a GZCOM
    // object). The host has neither, so the path is skipped - the window checks for null.
    cIGZWinKeyAccelerator* CreateKeyAccelerator() override { return nullptr; }

    void ReadyToHearMusic() override { readyToHearMusic_ = true; }
    bool readyToHearMusic() const { return readyToHearMusic_; }

private:
    cGZFramework* framework_ = nullptr;
    cIGZUnknown* graphicSystem_ = nullptr;
    cIGZMessageServer2* messageServer_ = nullptr;
    bool readyToHearMusic_ = false;
};

}  // namespace native
