// cGZWinGenDlg — a generic dialog panel with factory methods for its controls (0x1003ad2a4..0x1003cb980, 33 functions +
// thunks; the cIGZWinGenDlg view at +0x150 has iid 0x20fedaa6). The dialog draws a bevelled / tiled / filled background,
// remembers two system fonts (the default font 0x68963c4c for its controls and 0x28963c70 for the title), and its Add*
// methods build a control through the window control manager, size it to a rectangle, give it the default control colour,
// add it as a child and hand it back (null when the manager has no such control or the child could not be added).
// Layout: +0x150 view, +0x158 control font, +0x160 title font, +0x170 background bitmap, +0x17c border widths (left, top,
// right, bottom), +0x190 command callback, +0x198 its context, +0x1a0 style.
// Quirks kept: Init succeeds without doing anything when there is no control manager; the control ids are the low 32 bits
// of the control's address; AddListBox / AddCombo / AddTextEdit / the graphs return the control even when ChildAdd failed;
// a command whose sender has id 0 ends the dialog with result 0 and every other command goes to the callback; a bevelled
// dialog (style 0) reserves 6 pixels left and right, 0x17 for the title bar and 8 at the bottom.
#pragma once
#include <cstdint>

#include "gz/WinFont.h"
#include "gz/Win.h"
#include "gz/StreamOps.h"
#include "gz/cIGZString.h"

class cIGZBuffer;

namespace gz {

constexpr std::uint32_t kGZIID_cIGZWinGenDlg = 0x20fedaa6;
constexpr std::uint32_t kWinGenDlgFont = 0x68963c4c, kWinGenDlgTitleFont = 0x28963c70, kWinGenDlgBackground = 0xc8963ed3,
                        kWinGenDlgControlColor = 0x68963f6f;
enum tWindowBackgroundStyle : std::int32_t { kWindowBackgroundBevelled = 0, kWindowBackgroundTiled = 1, kWindowBackgroundFilled = 2 };
using tGenDlgCommandCallback = void (*)(std::uint32_t command, std::uint32_t param, void* context);

class cIGZWinGenDlg : public cIGZUnknown {
public:
    virtual bool SetBackground(cIGZBuffer* image) = 0;                                                          // [3]
    virtual cIGZWin* AddListBox(const cRZRect& area) = 0;                                                       // [4]
    virtual cIGZWin* AddBMP(const cRZRect& area, cIGZBuffer* image) = 0;                                        // [5]
    virtual cIGZWin* AddBtn(const cRZRect& area, const cIGZString& caption) = 0;                                // [6]
    virtual cIGZWin* AddCombo(const cRZRect& area, bool editable) = 0;                                          // [7]
    virtual cIGZWin* AddLineInput(const cRZRect& area) = 0;                                                     // [8]
    virtual cIGZWin* AddSlider(const cRZRect& area, int low, int high, int value, int direction) = 0;           // [9]
    virtual cIGZWin* Scrollbar(const cRZRect& area, int low, int high, int value, int direction) = 0;          // [10]
    virtual cIGZWin* AddSpinner(const cRZRect& area, int low, int high, int value) = 0;                         // [11]
    virtual cIGZWin* AddText(const cRZRect& area, const cIGZString& text, std::uint32_t alignment, bool opaque) = 0;   // [12]
    virtual cIGZWin* AddTextEdit(const cRZRect& area, const cIGZString& text) = 0;                              // [13]
    virtual cIGZWin* AddCheck(const cRZRect& area, const cIGZString& caption) = 0;                              // [14]
    virtual cIGZWin* AddOptionGroup(const cRZRect& area, const cIGZString& caption) = 0;                        // [15]
    virtual cIGZWin* AddLineGraph(const cRZRect& area) = 0;                                                     // [16]
    virtual cIGZWin* AddBarGraph(const cRZRect& area) = 0;                                                      // [17]
    virtual cIGZWin* AddScatterGraph(const cRZRect& area) = 0;                                                  // [18]
    virtual cRZRect GetBorderWidths() = 0;                                                                      // [19]
    virtual int GetStyle() = 0;                                                                                 // [20]
    virtual void SetCommandCallback(tGenDlgCommandCallback callback, void* context) = 0;                       // [21]
};

class cGZWinGenDlg : public cGZWin, public cIGZWinGenDlg {
public:
    explicit cGZWinGenDlg(cIGZWinMgr* mgr, tWindowBackgroundStyle style = kWindowBackgroundBevelled);   // @ 0x1003ca670
    ~cGZWinGenDlg() override;

    bool QueryInterface(std::uint32_t iid, void** out) override;   // @ 0x1003ca76a
    std::uint32_t AddRef() override { return cGZWin::AddRef(); }
    std::uint32_t Release() override { return cGZWin::Release(); }
    bool Init() override;
    bool Shutdown() override;
    bool GZPaint() override;
    bool GZOnMouseDownL(int x, int y, std::uint32_t flags) override;
    bool GZOnMouseUpL(int x, int y, std::uint32_t flags) override;
    bool GZOnMouseMove(int x, int y, std::uint32_t flags) override;
    bool GZOnCommand(std::uint32_t command, std::uint64_t param) override;

    bool SetBackground(cIGZBuffer* image) override;
    cIGZWin* AddListBox(const cRZRect& area) override;
    cIGZWin* AddBMP(const cRZRect& area, cIGZBuffer* image) override;
    cIGZWin* AddBtn(const cRZRect& area, const cIGZString& caption) override;
    cIGZWin* AddCombo(const cRZRect& area, bool editable) override;
    cIGZWin* AddLineInput(const cRZRect& area) override;
    cIGZWin* AddSlider(const cRZRect& area, int low, int high, int value, int direction) override;
    cIGZWin* Scrollbar(const cRZRect& area, int low, int high, int value, int direction) override;
    cIGZWin* AddSpinner(const cRZRect& area, int low, int high, int value) override;
    cIGZWin* AddText(const cRZRect& area, const cIGZString& text, std::uint32_t alignment, bool opaque) override;
    cIGZWin* AddTextEdit(const cRZRect& area, const cIGZString& text) override;
    cIGZWin* AddCheck(const cRZRect& area, const cIGZString& caption) override;
    cIGZWin* AddOptionGroup(const cRZRect& area, const cIGZString& caption) override;
    cIGZWin* AddLineGraph(const cRZRect& area) override;
    cIGZWin* AddBarGraph(const cRZRect& area) override;
    cIGZWin* AddScatterGraph(const cRZRect& area) override;
    cRZRect GetBorderWidths() override { return borders_; }                                                     // @ 0x1003ad2a4
    int GetStyle() override { return style_; }                                                                  // @ 0x1003cb970
    void SetCommandCallback(tGenDlgCommandCallback callback, void* context) override { callback_ = callback; callbackContext_ = context; }   // @ 0x1003ad2c4

    IWinFont* ControlFont() const { return font_; }
    IWinFont* TitleFont() const { return titleFont_; }
    cIGZBuffer* Background() const { return background_; }

private:
    bool AddChild(cIGZWin* window);          // ChildAdd; on failure the window is destroyed
    std::uint32_t ControlColor() const;
    static std::uint32_t IdFromAddress(const void* object) { return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(object)); }

    IWinFont* font_ = nullptr;               // +0x158
    IWinFont* titleFont_ = nullptr;          // +0x160
    cIGZBuffer* background_ = nullptr;       // +0x170
    cRZRect borders_{};                      // +0x17c
    tGenDlgCommandCallback callback_ = nullptr;   // +0x190
    void* callbackContext_ = nullptr;        // +0x198
    tWindowBackgroundStyle style_;           // +0x1a0
};

}  // namespace gz
