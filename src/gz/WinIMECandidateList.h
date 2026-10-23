// cGZWinIMECandidateList — the IME candidate list window (24 functions / 37 symbols with the thunks,
// 0x1001f8abc..0x1001fb5a0, class id 0x2ac45173, interface 0x2ac44a93). A small cGZWinGen dialog with one cGZWinGrid
// child (grid flags 0x19bff) laid out as a two-column list box: column 0 shows the candidate number (1-based), column 1
// the candidate text converted from the UTF-16 string in cGZIMECandidateListData to the framework encoding. The grid is
// an "act like a list box" grid; the selected row is the data block's selection.
//
// Layout (0x208 bytes, the size CreateGZWinIMECandidateList allocates): +0 cIGZWinIMECandidateList (primary vtable
// 0x100a85860), +8 cGZWinGen (its cGZWin view at +8, its cIGZWinGen view at +0x158), +0x1d0 candidate list flags (int),
// +0x1d4 candidate list font colours[6] (native), +0x1f0 the focus window captured by Init (cIGZWin*, referenced; the
// character messages are forwarded to it), +0x1f8 the grid (cGZWinGrid*, referenced), +0x200 the font (cIGZFont*,
// referenced; used to measure the two columns).
//
// The window registers with cGZMessageServer (service 0x64063387 / iid 0xc1085722, the local cRZSysServPtr in Init) for
//   0x2ac44ed8  candidate list data      -> SetCandidateListData          (SetupWindow)
//   0x2ac44ef0  candidate list selection -> SetCandidateListSelectionPosition (also SetupWindow)
// and forwards character messages (type 4) to the focus window while it has one.
//
// Quirks kept on purpose (see the test):
//  * SetCandidateListData and SetCandidateListSelectionPosition are the same function: each tail-jumps to the virtual
//    SetupWindow slot (0xa0) with the data pointer unchanged (0x1001fad46 / 0x1001fad58).
//  * SetCandidateListFlag / ClearCandidateListFlag test the bits in +0x1d0 first and only then call
//    SetCandidateListFlags with the merged word; an already-set (or already-clear) bit never reaches it
//    (0x1001fb38e / 0x1001fb3ca).
//  * SetCandidateListFlags only refreshes the change by reading cGZWin::get_init_flag(); it neither invalidates the
//    window nor touches the grid (0x1001fb3ac). SetFont ends with the same dead get_init_flag() read (0x1001fb2fe).
//  * Get/SetCandidateListFontColor test `index > 5` (signed) only: index -1 addresses the flag word at +0x1d0 instead
//    of the six colours (0x1001fb354 / 0x1001fb372).
//  * Init puts the window at (0x20, 0x20) without any coordinate conversion when no window has focus; with a focus
//    window it converts (0, focus height + 2) into the parent's coordinates and moves there (0x1001fa528).
//  * SetupWindow deletes every used row with DeleteRows(0, usedRows, 3) before filling the grid again, and the
//    empty-list path does not add the +4 margin the filled path adds to both column widths (0x1001fad6a).
//  * SetupWindow's fit-to-parent only calls SetArea when the window had to be clamped horizontally or vertically, or
//    when its left or top edge was already non-positive; the clamp uses ~overflow (0x1001fad6a).
//  * DoMessage calls cGZWin::DoMessage directly (skipping cGZWinGen::DoMessage, 0x1001facc4). With no focus window a
//    character message falls through to it as well.
//
// Seams: the fallback font comes from IWinStyleManager (gz/WinStyle.h) where the original asked
// cIGZWinCtrlMgr::SystemFont(0x68963c4c) through service 0xc2c2eb0f / 0x22c2eb1f, the same lookup
// cGZWinIMEComposition::Init uses. The grid's default font (Init vslot 0x78) comes from IWinFontSource
// (gz/WinFont.h, style 0x68963c4c) because the grid takes an IWinFont and a cIGZFont is not one; the original handed
// the same cIGZFont* straight to the grid, so with both seams pointing at the same font the result is identical.
// The message server is reached through cRZSysServPtr exactly like the original. Crash-only paths are guarded instead
// of reproduced: Init tolerates a null window manager / parent, SetupWindow a null grid / font / data block, and the
// destructor null pointers (the original dereferences all of them).
#pragma once
#include <cstdint>

#include "gz/WinGen.h"

class cIGZFont;
struct cGZIMECandidateListData;

namespace gz {

class cGZWinGrid;

constexpr std::uint32_t kGZIID_cIGZWinIMECandidateList = 0x2ac44a93;
constexpr std::uint32_t kGZCLSID_cGZWinIMECandidateList = 0x2ac45173;

// The window's own message types (registered in Init, handled in DoMessage); param1 is a cGZIMECandidateListData*.
constexpr std::uint32_t kGZMsgIMECandidateListData = 0x2ac44ed8;
constexpr std::uint32_t kGZMsgIMECandidateListSelection = 0x2ac44ef0;

constexpr std::uint32_t kIMECandidateListWindowID = 0x0ac57ed7;   // SetID in Init

// The IME candidate list view (vtable 0x100a85860): slots [3]..[20], then the complete / deleting destructor.
class cIGZWinIMECandidateList : public cIGZUnknown {
public:
    virtual cIGZWin* AsIGZWin() = 0;                                                 // [3]  +0x18  0x1001fb58c
    virtual bool Init() = 0;                                                         // [4]  +0x20  0x1001fa528
    virtual bool Shutdown() = 0;                                                     // [5]  +0x28  0x1001fabbe
    virtual cIGZFont* GetFont() const = 0;                                           // [6]  +0x30  0x1001fb2f0
    virtual void SetFont(cIGZFont* font) = 0;                                        // [7]  +0x38  0x1001fb2fe
    virtual bool GetCandidateListFontColor(int index, std::uint32_t& native) const = 0;   // [8]  +0x40  0x1001fb354
    virtual bool SetCandidateListFontColor(int index, std::uint32_t native) = 0;     // [9]  +0x48  0x1001fb372
    virtual void SetCandidateListFlag(int flag) = 0;                                 // [10] +0x50  0x1001fb38e
    virtual void SetCandidateListFlags(int flags) = 0;                               // [11] +0x58  0x1001fb3ac
    virtual void ClearCandidateListFlag(int flag) = 0;                               // [12] +0x60  0x1001fb3ca
    virtual bool IsCandidateListFlagSet(int flag) = 0;                               // [13] +0x68  0x1001fb3ea
    virtual std::uint32_t GetCandidateListFlags() = 0;                               // [14] +0x70  0x1001fb3fa
    // [15] / [16]: the complete / deleting destructor entries in the binary vtable.
    virtual ~cIGZWinIMECandidateList() = default;
    virtual bool DoMessage(cGZMessage& message) = 0;                                 // [17] +0x88  0x1001facc4
    virtual void SetCandidateListData(cGZIMECandidateListData* data) = 0;            // [18] +0x90  0x1001fad46
    virtual void SetCandidateListSelectionPosition(cGZIMECandidateListData* data) = 0;   // [19] +0x98  0x1001fad58
    virtual void SetupWindow(cGZIMECandidateListData* data) = 0;                     // [20] +0xa0  0x1001fad6a
};

class cGZWinIMECandidateList : public cIGZWinIMECandidateList, public cGZWinGen {
public:
    explicit cGZWinIMECandidateList(cIGZWinMgr* mgr);   // @ 0x1001f8abc (the Director's ctor) and 0x1001fa2b6 (the base
                                                        //    ctor cSC4WinIMECandidateList calls; both emit the same body)
    ~cGZWinIMECandidateList() override;                 // @ 0x1001fa33e (+ deleting form 0x1001fa426)

    bool QueryInterface(std::uint32_t iid, void** out) override;   // @ 0x1001fa462
    std::uint32_t AddRef() override;                               // @ 0x1001fa4d4
    std::uint32_t Release() override;                              // @ 0x1001fa4fe

    // cIGZWinIMECandidateList
    cIGZWin* AsIGZWin() override;                                  // @ 0x1001fb58c
    bool Init() override;                                          // @ 0x1001fa528
    bool Shutdown() override;                                      // @ 0x1001fabbe
    cIGZFont* GetFont() const override { return font_; }           // @ 0x1001fb2f0
    void SetFont(cIGZFont* font) override;                         // @ 0x1001fb2fe
    bool GetCandidateListFontColor(int index, std::uint32_t& native) const override;   // @ 0x1001fb354
    bool SetCandidateListFontColor(int index, std::uint32_t native) override;          // @ 0x1001fb372
    void SetCandidateListFlag(int flag) override;                  // @ 0x1001fb38e
    void SetCandidateListFlags(int flags) override;                // @ 0x1001fb3ac
    void ClearCandidateListFlag(int flag) override;                // @ 0x1001fb3ca
    bool IsCandidateListFlagSet(int flag) override;                // @ 0x1001fb3ea
    std::uint32_t GetCandidateListFlags() override { return flags_; }   // @ 0x1001fb3fa
    bool DoMessage(cGZMessage& message) override;                  // @ 0x1001facc4
    void SetCandidateListData(cGZIMECandidateListData* data) override;              // @ 0x1001fad46
    void SetCandidateListSelectionPosition(cGZIMECandidateListData* data) override; // @ 0x1001fad58
    void SetupWindow(cGZIMECandidateListData* data) override;                       // @ 0x1001fad6a

    // Internals (public for the test). The binary offsets are the ones in the layout comment above.
    cIGZWin* FocusTarget() const { return focus_; }    // +0x1f0
    cGZWinGrid* Grid() const { return grid_; }         // +0x1f8

private:
    // The word the original addresses at +0x1d4 + index*4. Its bounds check is `index > 5` only, so index -1 is the
    // flag word at +0x1d0; indexes below -1 walked outside the object and are clamped to that same word here.
    std::uint32_t* ColorWord(int index);
    const std::uint32_t* ColorWord(int index) const;

    std::uint32_t flags_ = 0;             // +0x1d0
    std::uint32_t colors_[6] = {};        // +0x1d4
    cIGZWin* focus_ = nullptr;            // +0x1f0 (reference held)
    cGZWinGrid* grid_ = nullptr;          // +0x1f8 (reference held)
    cIGZFont* font_ = nullptr;            // +0x200 (reference held)
};

}  // namespace gz
