// cGZWinMsgBox — the message box dialog (0x10051fb7a..0x1005212a0, 28 functions + thunks; views: +0x150 cIGZWinDlg, +0x158
// a small interface of iid 0x57c). It shows an optional title (the window caption), the message (+0x160) and one to three
// buttons chosen by the style: 0 OK, 1 OK / Cancel, 2 Retry / Cancel, 3 Yes / No, 4 Yes / No / Cancel, 5 Abort / Retry / Ignore.
// Enter presses the OK button and Escape or Alt+F4 the Cancel button (as commands 0x287259f6 with the button id); a command
// ends the dialog with the button id as the result. The window is centred in the main window, drawn as a bevelled panel and
// can be dragged anywhere.
// Layout: +0x160 message (cRZString), +0x188 style (u16), +0x18c title fill colour, +0x190 default button, +0x198 cancel
// button, +0x1a0 title text, +0x1a8 message text (each holds a reference), +0x1b0 background bitmap, +0x1b8 / +0x1bc outline
// colours (top-left, bottom-right).
// Quirks kept: the title-less CreateDlg overload also clears the title fill colour, background and outline colours (it
// zeroes 0x18c..0x1bc); styles above 5 fail Init after the text windows were built; the buttons of styles 4 and 5 that sit
// between the first and last are not remembered.
#pragma once
#include <cstdint>

#include "gz/SmallObjects.h"
#include "gz/WinBtn.h"
#include "gz/WinText.h"
#include "rz/cRZString.h"

namespace gz {

constexpr std::uint32_t kGZIID_cIGZWinMsgBox = 0x0000057c;
constexpr std::uint32_t kWinMsgBoxCommand = 0x287259f6, kWinMsgBoxDialogId = 0xa999b756;
constexpr std::uint32_t kMsgBoxAbort = 0x53018146, kMsgBoxCancel = 0x53018147, kMsgBoxIgnore = 0x53018148, kMsgBoxNo = 0x53018149,
                        kMsgBoxOK = 0x5301814a, kMsgBoxRetry = 0x5301814b, kMsgBoxYes = 0x5301814c;
enum tMsgBoxStyle : std::uint32_t { kMsgBoxOK_ = 0, kMsgBoxOKCancel = 1, kMsgBoxRetryCancel = 2, kMsgBoxYesNo = 3, kMsgBoxYesNoCancel = 4, kMsgBoxAbortRetryIgnore = 5 };
constexpr std::uint32_t kWinFlagDragging = 0x400;

class cIGZWinMsgBox : public cIGZUnknown {
public:
    virtual ~cIGZWinMsgBox() = default;
    virtual cIGZWin* AsIGZWin() = 0;
};

class cGZWinMsgBox : public cGZWinDlg, public cIGZWinMsgBox {
public:
    explicit cGZWinMsgBox(cIGZWinMgr* mgr);
    ~cGZWinMsgBox() override;

    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return cGZWinDlg::AddRef(); }
    std::uint32_t Release() override { return cGZWinDlg::Release(); }

    bool Init() override;
    bool Shutdown() override;
    bool GZPaint() override;
    bool GZOnKeyDown(std::uint32_t key, std::uint32_t modifiers) override;
    bool GZOnKeyUp(std::uint32_t, std::uint32_t) override { return true; }
    bool GZOnMouseDownL(int x, int y, std::uint32_t flags) override;
    bool GZOnMouseUpL(int x, int y, std::uint32_t flags) override;
    bool GZOnMouseMove(int x, int y, std::uint32_t flags) override;
    bool GZOnCommand(std::uint32_t command, std::uint64_t param) override;

    cIGZWin* AsIGZWin() override { return this; }
    virtual bool CreateDlg(const cIGZString& message, const cIGZString& title, std::uint32_t style);   // @ 0x100521054
    virtual bool CreateDlg(const cIGZString& message, std::uint32_t style);                            // @ 0x100521118
    virtual void DestroyDlg() { Shutdown(); }                                                          // @ 0x1005211ac
    virtual void SetTitleFillColor(std::uint32_t native) { titleFill_ = native; }                      // @ 0x1005211ce
    virtual void SetTitleFillColor(std::uint8_t r, std::uint8_t g, std::uint8_t b) { titleFill_ = MakeFillColor(r, g, b); }   // @ 0x1005211e4
    virtual cIGZWinBtn* GetDefaultButton() { return defaultButton_; }                                  // @ 0x10052122e
    virtual cIGZWinBtn* GetCancelButton() { return cancelButton_; }                                    // @ 0x10052123c
    virtual cIGZWinText* GetTitleText() { return titleText_; }                                         // @ 0x10052124a
    virtual cIGZWinText* GetMessageText() { return messageText_; }                                     // @ 0x100521258

    cIGZWinBtn* create_and_add_button(std::uint32_t id);                                               // @ 0x100520814
    const cRZString& Message() const { return message_; }
    std::uint32_t Style() const { return style_; }
    std::uint32_t TitleFillColor() const { return titleFill_; }

protected:
    cRZString message_;                    // +0x160
    std::uint32_t style_ = 0;              // +0x188 (u16)
    std::uint32_t titleFill_ = 0;          // +0x18c
    cIGZWinBtn* defaultButton_ = nullptr;  // +0x190
    cIGZWinBtn* cancelButton_ = nullptr;   // +0x198
    cIGZWinText* titleText_ = nullptr;     // +0x1a0
    cIGZWinText* messageText_ = nullptr;   // +0x1a8
    cIGZBuffer* background_ = nullptr;     // +0x1b0
    std::uint32_t outlineLight_ = 0, outlineDark_ = 0;   // +0x1b8, +0x1bc
};

}  // namespace gz
