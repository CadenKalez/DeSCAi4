// cGZWinIMEComposition — the floating composition window of the input-method (IME) layer (40 functions / 90 symbols
// with the thunks, 0x1001f8a04..0x1001fb4f8, class id 0x2ac45170, interface 0x2ac44a90). It is a multi-line text edit that follows the
// focus window: it shows the UTF-16 composition string from cGZWinIMEServer and underlines the characters whose
// attribute byte is set (1 = normal underline, 2 = thick underline, 3 = dark-red underline).
//
// Layout: +0 cIGZWinIMEComposition (primary vtable 0x100a84ca8), +8 cGZWinTextEdit (its cIGZWinTextEdit view at
// +0x158), +0x3d0 composition flags (int), +0x3d4 composition font colours[8] (native), +0x3f8 the focus window
// (cIGZWin*, referenced), +0x400 that window's cIGZWinTextEdit view (referenced, null when it is not a text edit),
// +0x408 the composition language (int; 0x14 = Chinese, no extra line), +0x410 the attribute bytes
// (std::vector<uint8_t>), +0x428 the four attribute colours (native, initially -1).
//
// Messages the window registers with cGZMessageServer (service 0x64063387 / iid 0xc1085722, the local
// cRZSysServPtr in Init) for itself:
//   0x2ac44c14  a new composition string data block (language at +0x18) -> SetCompositionString + SetWindowColors
//   0x2ac44c1a  the caret changed                                -> SetCompositionCaretPosition
//   0x2ac44c1b  the composition ended / the target moved         -> SetWindowPosition
// Character messages (type 4) are forwarded to the focus window while there is one.
//
// Quirks kept on purpose (see the test):
//  * SetCaretColor(value) calls GetCaretColor into a local and throws the value away (0x1001f9e48): the caret colour
//    never changes.
//  * SetCompositionFlag / ClearCompositionFlag test composition flags (+0x3d0) but push compositionFlags|bit,
//    respectively compositionFlags&~bit, into the text-edit flag word without updating +0x3d0 (0x1001fa1d2 /
//    0x1001fa272). SetCompositionFlags is the only writer of +0x3d0; it synchronises just the outline bit (0x10).
//  * SetWindowColors copies the target edit's colour 0 into attribute colours 1 and 2, and its colour 1 into
//    attribute colour 0 (0x1001f94a2).
//  * SetWindowPosition moves to (2, 2) when there is no focus window and grows the height by 2 for every language
//    except 0x14 (0x1001f92f8).
//  * SetCompositionCaretPosition walks the UTF-8 text with GetCharLength; the original looped forever when the caret
//    index was past the end (GetCharLength returns 0 there). Here it stops at the end of the text.
//
// Seams: the font comes from the recovered style seam (IWinStyleManager / WinStyleManager, the same lookup
// cGZWinTextEdit::Init uses) where the original asked cIGZWinCtrlMgr::SystemFont(0x68963c4c) (service 0xc2c2eb0f);
// the message server is reached through cRZSysServPtr exactly like the original, so a test installs a resolver.
#pragma once
#include <cstdint>
#include <vector>

#include "gz/WinTextEdit.h"

struct cGZIMECompositionStringData;

namespace gz {

constexpr std::uint32_t kGZIID_cIGZWinIMEComposition = 0x2ac44a90;
constexpr std::uint32_t kGZCLSID_cGZWinIMEComposition = 0x2ac45170;

// The window's own message types (registered in Init, handled in DoMessage).
constexpr std::uint32_t kGZMsgIMECompositionData = 0x2ac44c14;
constexpr std::uint32_t kGZMsgIMECompositionCaret = 0x2ac44c1a;
constexpr std::uint32_t kGZMsgIMECompositionEnd = 0x2ac44c1b;

// The IME composition view (vtable 0x100a84ca8, slots [3]..[29] then the complete / deleting destructor and
// DoMessage, GZPaint, SetWindowPosition, SetWindowColors, SetCompositionString, SetCompositionCaretPosition).
class cIGZWinIMEComposition : public cIGZUnknown {
public:
    virtual cIGZWin* AsIGZWin() = 0;                                                    // [3]  +0x18
    virtual bool Init() = 0;                                                            // [4]  +0x20
    virtual bool Shutdown() = 0;                                                        // [5]  +0x28
    virtual cIGZFont* GetFont() const = 0;                                              // [6]  +0x30
    virtual void SetFont(cIGZFont* font) = 0;                                           // [7]  +0x38
    virtual bool GetCompositionFontColor(int index, std::uint32_t& native) const = 0;   // [8]  +0x40
    virtual bool SetCompositionFontColor(int index, std::uint32_t native) = 0;          // [9]  +0x48
    virtual bool GetCaretColor(std::uint32_t& native) const = 0;                        // [10] +0x50
    virtual void SetCaretColor(std::uint32_t native) = 0;                               // [11] +0x58
    virtual bool GetCaretPeriod(std::uint32_t& ms) const = 0;                           // [12] +0x60
    virtual bool SetCaretPeriod(std::uint32_t ms) = 0;                                  // [13] +0x68
    virtual void GetOutlineUse(bool& on, std::uint32_t& native) const = 0;              // [14] +0x70
    virtual void SetOutlineUse(bool on, std::uint32_t native) = 0;                      // [15] +0x78
    virtual std::uint32_t GetTextGutterX() = 0;                                         // [16] +0x80
    virtual void SetTextGutterX(std::uint32_t gutter) = 0;                              // [17] +0x88
    virtual std::uint32_t GetTextGutterY() = 0;                                         // [18] +0x90
    virtual void SetTextGutterY(std::uint32_t gutter) = 0;                              // [19] +0x98
    virtual bool ResizeWindowForExactLineHeights() = 0;                                 // [20] +0xa0
    virtual const char* GetText() = 0;                                                  // [21] +0xa8
    virtual std::uint32_t GetTextLength() = 0;                                          // [22] +0xb0
    virtual void GetText(cIGZString& out) = 0;                                          // [23] +0xb8
    virtual void GetText(char* buffer, std::uint32_t& length) = 0;                      // [24] +0xc0
    virtual void SetCompositionFlag(int flag) = 0;                                      // [25] +0xc8
    virtual void SetCompositionFlags(int flags) = 0;                                    // [26] +0xd0
    virtual void ClearCompositionFlag(int flag) = 0;                                    // [27] +0xd8
    virtual bool IsCompositionFlagSet(int flag) = 0;                                    // [28] +0xe0
    virtual std::uint32_t GetCompositionFlags() = 0;                                    // [29] +0xe8
    // [30] / [31]: the complete / deleting destructor entries in the binary vtable.
    virtual ~cIGZWinIMEComposition() = default;
    virtual bool DoMessage(cGZMessage& message) = 0;                                    // [32] +0x100
    virtual bool GZPaint() = 0;                                                         // [33] +0x108
    virtual void SetWindowPosition() = 0;                                               // [34] +0x110
    virtual void SetWindowColors() = 0;                                                 // [35] +0x118
    virtual void SetCompositionString(cGZIMECompositionStringData* data) = 0;           // [36] +0x120
    virtual void SetCompositionCaretPosition(cGZIMECompositionStringData* data) = 0;    // [37] +0x128
};

class cGZWinIMEComposition final : public cIGZWinIMEComposition, public cGZWinTextEdit {
public:
    explicit cGZWinIMEComposition(cIGZWinMgr* mgr);        // @ 0x1001f8a04, flags 0x11ff
    ~cGZWinIMEComposition() override;                     // @ 0x1001f8c74

    bool QueryInterface(std::uint32_t iid, void** out) override;   // @ 0x1001f8d88
    std::uint32_t AddRef() override;                               // @ 0x1001f8df6
    std::uint32_t Release() override;                              // @ 0x1001f8e20

    // cIGZWinIMEComposition
    cIGZWin* AsIGZWin() override;                                  // @ 0x1001fb40e
    bool Init() override;                                          // @ 0x1001f8e4a
    bool Shutdown() override;                                      // @ 0x1001f90e0
    bool DoMessage(cGZMessage& message) override;                  // @ 0x1001f9226
    bool GZPaint() override;                                       // @ 0x1001f9b12
    cIGZFont* GetFont() const override;                            // @ 0x1001f9d70
    void SetFont(cIGZFont* font) override;                         // @ 0x1001f9d9a
    bool GetCompositionFontColor(int index, std::uint32_t& native) const override;   // @ 0x1001f9dc4
    bool SetCompositionFontColor(int index, std::uint32_t native) override;          // @ 0x1001f9ddc
    bool GetCaretColor(std::uint32_t& native) const override;      // @ 0x1001f9e1e
    void SetCaretColor(std::uint32_t native) override;             // @ 0x1001f9e48 (quirk: does not set)
    bool GetCaretPeriod(std::uint32_t& ms) const override;         // @ 0x1001f9ea8
    bool SetCaretPeriod(std::uint32_t ms) override;                // @ 0x1001f9ed2
    void GetOutlineUse(bool& on, std::uint32_t& native) const override;   // @ 0x1001f9efc
    void SetOutlineUse(bool on, std::uint32_t native) override;    // @ 0x1001f9f26
    std::uint32_t GetTextGutterX() override;                       // @ 0x1001f9f5c
    void SetTextGutterX(std::uint32_t gutter) override;            // @ 0x1001f9f80
    std::uint32_t GetTextGutterY() override;                       // @ 0x1001f9faa
    void SetTextGutterY(std::uint32_t gutter) override;            // @ 0x1001f9fce
    bool ResizeWindowForExactLineHeights() override;               // @ 0x1001f9ff8
    const char* GetText() override;                                // @ 0x1001fa022
    std::uint32_t GetTextLength() override;                        // @ 0x1001fa094
    void GetText(cIGZString& out) override;                        // @ 0x1001fa0e8
    void GetText(char* buffer, std::uint32_t& length) override;    // @ 0x1001fa1a8
    void SetCompositionFlag(int flag) override;                    // @ 0x1001fa1d2 (quirk: leaves +0x3d0)
    void SetCompositionFlags(int flags) override;                  // @ 0x1001fa1f8
    void ClearCompositionFlag(int flag) override;                  // @ 0x1001fa272 (quirk: leaves +0x3d0)
    bool IsCompositionFlagSet(int flag) override;                  // @ 0x1001fa29a
    std::uint32_t GetCompositionFlags() override;                  // @ 0x1001fa2aa
    void SetWindowPosition() override;                             // @ 0x1001f92f8
    void SetWindowColors() override;                               // @ 0x1001f94a2
    void SetCompositionString(cGZIMECompositionStringData* data) override;          // @ 0x1001f985c
    void SetCompositionCaretPosition(cGZIMECompositionStringData* data) override;   // @ 0x1001f9a7c

    // Internals (public for the test). The binary offsets are the ones in the layout comment above.
    cIGZWin* FocusTarget() const { return focusTarget_; }               // +0x3f8
    cIGZWinTextEdit* TargetTextEdit() const { return targetTextEdit_; } // +0x400
    std::uint32_t Language() const { return language_; }                // +0x408
    const std::vector<std::uint8_t>& Attributes() const { return attributes_; }   // +0x410
    const std::uint32_t* AttributeColors() const { return attributeColors_; }     // +0x428
    const std::uint32_t* CompositionColors() const { return compositionColors_; } // +0x3d4

private:
    // Our cGZWinTextEdit ends 0x38 bytes earlier than the binary class (its string / timer members are smaller
    // here) and the binary has another 8-byte tail before the IME fields; 0x40 bytes of pad keep every field
    // below at its documented binary offset. The pad has no behaviour.
    [[maybe_unused]] std::uint8_t baseLayoutPad_[0x40] = {};         // +0x390..+0x3cf
    std::uint32_t compositionFlags_ = 0;            // +0x3d0
    std::uint32_t compositionColors_[8] = {};       // +0x3d4
    cIGZWin* focusTarget_ = nullptr;                // +0x3f8 (reference held)
    cIGZWinTextEdit* targetTextEdit_ = nullptr;     // +0x400 (reference held)
    std::uint32_t language_ = 0;                    // +0x408
    std::vector<std::uint8_t> attributes_;          // +0x410
    std::uint32_t attributeColors_[4] = {0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu};   // +0x428
};

}  // namespace gz
