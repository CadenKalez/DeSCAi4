// cSC4TimeDisplayMiniWindowWinProc — the small wall clock window (13 functions, 0x1002f2c10..0x1002f319e). A modeless
// window (script instance 0xaa5e60d1, window id 0xca5e6261, label 0x4a5e623e) that shows the local time of day. Every
// tick message (0xa2bf8acd) that arrives at least two seconds after the last refresh re-reads the clock; when the
// second count changed the label is set to the language manager's formatted hour:minute. Message 0xa2bf8ad1 shuts it
// down; the close button 0x42b7c356 ends it. Shutdown stores the window position and clears preference byte 0x4ee.
// The seconds counter is (julian day - epoch day) * 86400 + the time of day.
#pragma once
#include <cstdint>
#include <ctime>
#include <functional>
#include <string>

#include "gz/WinGen.h"
#include "gz/WinProc.h"
#include "gz/WinText.h"
#include "rz/cRZTimer.h"
#include "sc4/WinLocationSaver.h"

namespace sc4 {

struct TimeDisplayServices {
    std::function<cIGZWin*()> appWindow;                                    // SL::spWinSC4AppWin
    std::function<cSLWinLocationSaver*()> locationSaver;
    std::function<std::uint8_t*()> preferences;                             // SL::spPreferences
    std::function<std::string(std::uint32_t hour, std::uint32_t minute)> formatTime;   // language manager (service 0x441e5070)
    std::function<std::time_t()> now;                                       // time(): defaults to the system clock
};
TimeDisplayServices& GetTimeDisplayServices();

class cSC4TimeDisplayMiniWindowWinProc final : public gz::cIGZWinProc {
public:
    cSC4TimeDisplayMiniWindowWinProc();
    ~cSC4TimeDisplayMiniWindowWinProc();
    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return ++refCount_; }
    std::uint32_t Release() override;
    bool DoWinProcMessage(cIGZWin* window, cGZMessage& message) override;
    bool Init();                                          // [7] @ 0x1002f2e08
    bool Shutdown();                                      // [8] @ 0x1002f2e1a
    void EndDialog();                                     // @ 0x1002f2f7e
    void UpdateTimeDisplay();                             // @ 0x1002f3050
    static bool DoModelessWin();                          // @ 0x1002f319e

    std::uint32_t Seconds() const { return seconds_; }
    void Attach(gz::cIGZWinGen* dialog);

private:
    std::uint32_t refCount_ = 0;                          // +8
    bool initialized_ = false;                            // +0xc
    gz::cIGZWinGen* dialog_ = nullptr;                    // +0x10
    gz::cIGZWinText* label_ = nullptr;                    // +0x18
    cRZTimer timer_{cRZTimer::kSeconds};                  // +0x28
    std::uint32_t seconds_ = 0;                           // +0x40
};

}  // namespace sc4
