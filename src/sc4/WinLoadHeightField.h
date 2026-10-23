// cSC4WinLoadHeightField (12 functions at 0x1003d1e8e..0x1003d2584): the "load height field" dialog. A 404x268 window (id 0xea9660f3)
// that wraps one cGZWinFileBrowser (class 0x1aa52ea4, iid 0x3aa52e64) filtered to *.bmp *.png *.gif *.jpg *.jpeg *.tga. It ends
// itself (window manager EndDialog code 0xf) with result 1 when a file is chosen (OK, or the browser's "chosen" message) and 0 on
// Cancel / Escape.
#pragma once
#include "gz/Win.h"
#include "gz/WinFileBrowser.h"

namespace sc4 {

constexpr std::uint32_t kLoadHeightFieldWindowId = 0xea9660f3;
constexpr std::uint32_t kLoadHeightFieldBrowserId = 0x9aedef7c;
constexpr std::uint32_t kLoadHeightFieldMsgCommandSender = 0x287259f6;

class ILoadHeightFieldHost {
public:
    virtual ~ILoadHeightFieldHost() = default;
    // GZCOM() vslot 0x18 (0x1aa52ea4, 0x3aa52e64): a new file browser, one reference owned by the caller; null on failure.
    virtual cIGZWinFileBrowser* CreateFileBrowser() = 0;
    // browser vslot 0x248 (SetGutters).
    virtual void SetBrowserGutters(cIGZWinFileBrowser* browser, std::uint32_t a, std::uint32_t b, std::uint32_t c) = 0;
    // cSLString(0x6a231eaa, 0x4a9f1428, true): the title of the filter set.
    virtual std::string FilterSetTitle() = 0;
};
ILoadHeightFieldHost* LoadHeightFieldHost();
void SetLoadHeightFieldHost(ILoadHeightFieldHost* host);

class cSC4WinLoadHeightField : public cGZWin {
public:
    explicit cSC4WinLoadHeightField(cIGZWinMgr* mgr) : cGZWin(mgr) { id_ = kLoadHeightFieldWindowId; }   // @ 0x1003d1e8e
    ~cSC4WinLoadHeightField() override = default;      // @ 0x1003d1eee (+ 0x1003d1f24 / 0x1003d1f2e)

    bool DoMessage(cGZMessage& message) override;      // @ 0x1003d1f5c
    bool Init() override;                              // @ 0x1003d20f8
    bool Shutdown() override;                          // @ 0x1003d24d0
    bool GZPaint() override;                           // @ 0x1003d2516
    bool GZOnKeyDown(std::uint32_t key, std::uint32_t modifiers) override;   // @ 0x1003d2584

    bool GetSelection(FileBrowserSelection& out);                // @ 0x1003d2052
    void GetInitialDirectory(cRZString& out);                    // @ 0x1003d2074
    void SetInitialDirectory(const cRZString& directory);        // @ 0x1003d20b8

private:
    cIGZWinFileBrowser* browser_ = nullptr;   // +0x150
    cRZString directory_;                     // +0x158
};

}  // namespace sc4
