// cSC4GamePreferencesWinProc — the in-game "Game Options" dialog (11 functions, 0x1003a2c42..0x1003a39f2). Script
// instance 0x0a7df315, window id 0x2a57db82. Five check boxes (0x4a7f19f2..f6) mirror bytes of the preference blob
// (0xf08, 0xf09, 0x4ec, 0xf0b, 0xf0a). Two tab buttons (0xea57db7e / 0xea5e99d9) show one of the two child pages
// (0x2a57db84 / 0x2a57dc83); 0xea57db7f asks the app to run its message 0xac3b3fa6 and closes; 0xea57da59 (OK) stores the
// check boxes, posts 0x231bbf91, saves the preferences and closes; 0x6a57da48 (Cancel) closes; 0x0a57da65 runs the
// credits dialog. The button 0xea5e99d9 is disabled while a city is loaded (it reports to the dialog otherwise).
// Quirk kept: ShowDialog answers false after building the dialog and true when it was already showing.
#pragma once
#include <cstdint>
#include <functional>

#include "gz/WinGen.h"
#include "gz/WinProc.h"
#include "sc4/WinLocationSaver.h"

namespace sc4 {

struct GamePreferencesServices {
    std::function<cIGZWinMgr*()> windowManager;
    std::function<std::uint8_t*()> preferences;                 // SL::spPreferences (the 0x1734-byte blob)
    std::function<bool()> cityLoaded;                           // SL::spCity != 0
    std::function<bool()> appPresent;                           // SL::spSC4 != 0
    std::function<void(std::uint32_t type)> postMessage;        // SL::spMS2->MessagePost
    std::function<void()> savePreferences;                      // SL::spSC4 vslot 0x88
    std::function<void(std::uint32_t type)> sendAppMessage;     // SC4SendMessage2(type, 0, 0, 0, 0, null)
    std::function<cSLWinLocationSaver*()> locationSaver;
    std::function<void()> showCredits;                          // builds, shows and runs cSC4CreditsWinProc
    std::function<void(bool begin)> gamePause;                  // cSC4AutoGamePause(false, false, true, true)
};
GamePreferencesServices& GetGamePreferencesServices();

class cSC4GamePreferencesWinProc final : public gz::cIGZWinProc {
public:
    enum eGameWindowType { kGameOptions = 0 };
    cSC4GamePreferencesWinProc() = default;
    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return ++refCount_; }
    std::uint32_t Release() override;
    bool DoWinProcMessage(cIGZWin* window, cGZMessage& message) override;
    virtual bool ShowDialog(eGameWindowType type);               // [5] @ 0x1003a2e8c
    virtual bool Shutdown();                                     // [6] @ 0x1003a3436

    bool DoModalWin();                                           // @ 0x1003a2c64
    void EndDialog();                                            // @ 0x1003a2dbe
    bool HandleControlMessage(std::uint32_t id);                 // @ 0x1003a357e
    static bool Displayed();

private:
    std::uint32_t refCount_ = 0;                                 // +0x18
    gz::cIGZWinGen* dialog_ = nullptr;                           // +8
    std::uint8_t* preferences_ = nullptr;                        // +0x10
};

}  // namespace sc4
