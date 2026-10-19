// cSLWinLocationSaver — remembers where the player left the movable windows (18 functions, 0x1002a649a..0x1002a6ab8, iids
// 0xaa601692 / 0xaa6016b1). The positions live in the preference blob as 200 (window id, x, y) triples at +0x4f0 (the
// list ends at the first id 0); Init loads them into a map, Shutdown writes the map back in id order and clears the rest.
// A window can be put back at its saved place, kept at least 8 pixels inside its parent.
#pragma once
#include <cstdint>
#include <map>

#include "gz/WinInterfaces.h"
#include "gz/cIGZUnknown.h"
#include "rz/cRZAutoRefCount.h"

constexpr std::uint32_t kGZIID_cSLWinLocationSaver = 0xaa6016b1;
constexpr std::uint32_t kGZCLSID_cSLWinLocationSaver = 0xaa601692;

class cSLWinLocationSaver final : public cIGZUnknown {
public:
    static constexpr int kSavedWindowCount = 200;
    static constexpr std::size_t kPreferencesOffset = 0x4f0;

    cSLWinLocationSaver() = default;                       // @ 0x1002a649a
    virtual ~cSLWinLocationSaver() = default;              // @ 0x1002a64cc

    bool QueryInterface(std::uint32_t iid, void** out) override;   // @ 0x1002a656a
    std::uint32_t AddRef() override { return ++refCount_; }        // @ 0x1002a6540
    std::uint32_t Release() override;                              // @ 0x1002a654e

    bool Init();                                                   // @ 0x1002a6594
    bool Shutdown();                                               // @ 0x1002a663c
    bool SaveWindowLocation(cIGZWin* window);                      // @ 0x1002a671e
    bool SaveWindowLocation(std::uint32_t id, int x, int y);       // @ 0x1002a6798
    bool GetWindowRestoreLocation(cIGZWin* window, int& x, int& y, bool unused);          // @ 0x1002a688a
    bool GetWindowRestoreLocation(std::uint32_t id, int& x, int& y, bool unused);         // @ 0x1002a68f0
    bool RestoreWindowLocation(cIGZWin* window, bool keepOnScreen);                       // @ 0x1002a693e
    bool RestoreWindowLocation(std::uint32_t id, bool keepOnScreen);                      // @ 0x1002a6a3c

private:
    struct WindowLocationData { int x = 0, y = 0; };
    std::uint32_t refCount_ = 0;                                   // +8
    bool initialized_ = false;                                     // +0xc
    cRZAutoRefCount<cIGZWin> mainWindow_;                          // +0x10
    std::map<std::uint32_t, WindowLocationData> locations_;        // +0x18
};
