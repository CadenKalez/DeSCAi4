// cSC4WinText — the game's text window (3 symbols: GZPaint 0x1005c333c and the destructor 0x1005c3a90 / 0x1005c3a9a).
// It is a cGZWinText whose paint wraps the caption itself: the font is asked for the bytes that fit one line
// (cIGZFont [41] 0x148), each line is drawn to the window in turn, and a last line that does not hold the rest of the
// text is shortened and given the control manager's ellipsis text. The single-line case ellipsizes one copy of the
// caption (cIGZFont [50] 0x190) and draws it.
//
// The class adds no fields: every offset GZPaint touches is a cGZWinText member — +0x30 draw area, +0x68 caption,
// +0x98 draw buffer, +0x120/+0x124/+0x12c window area, +0x158 font, +0x164 / +0x165 gutters, +0x168 text rectangle,
// +0x178 text flags, +0x17c outline colour, +0x184 alignment, +0x188 / +0x18c text colours, +0x190 horizontal
// alignment. The vtable is cGZWinText's with GZPaint replaced (0x100acfad0, group +0x150 at 0x100ad0068).
//
// Quirks kept:
// * the wrapped path takes its vertical slack from the *window area* (+0x120..+0x12c) but places the box from the draw
//   area (+0x30): 0x1005c35d7..0x1005c3601 computes the slack, 0x1005c3772..0x1005c378e the box origin.
// * the last line is ellipsized only when the wrap loop stopped with text left over (the byte at rbp-0x38: set at
//   0x1005c3683, cleared at 0x1005c36d0, tested at 0x1005c394a); a last line that ends the text is drawn plainly.
// * alignment values above 7 get no vertical adjustment at all (0x1005c3747 `cmp edx,7 / ja`), unlike
//   cGZWinText::CalcTextPos which treats 8 as bottom.
// * the ellipsis width comes from the *manager's* ellipsis string (0x1005c37d6..0x1005c37fb), not from a literal: with
//   no control manager the original would dereference null; this port falls back to "..." (see sc4/WinText.cpp).
//
// GZCOM maker: GZCOM_CreatecSC4WinText @ 0x1005c32dc allocates 0x198 bytes, runs the cGZWinText constructor and answers
// the cIGZWinText view at +0x150; GZCOM_cSC4WinTextCLSID @ 0x1005c3331 answers 0xaa7cecfd.
#pragma once
#include <cstdint>

#include "gz/WinText.h"

namespace sc4 {

constexpr std::uint32_t kSC4WinTextCLSID = 0xaa7cecfd;             // GZCOM_cSC4WinTextCLSID @ 0x1005c3331
constexpr std::uint32_t kWinCtrlEllipsisTextId = 0x5377be31;       // RZ::WCM() vslot 0x118, the "..." default text

class cSC4WinText : public gz::cGZWinText {
public:
    // The original has no constructor of its own: the maker runs cGZWinText's ctor (0x10027e5e0, which finds the
    // window manager through the engine global) and swaps the two vtable pointers. This port takes the manager the
    // maker captured, like the other recovered window classes.
    explicit cSC4WinText(cIGZWinMgr* mgr);
    ~cSC4WinText() override;                                       // @ 0x1005c3a90 (+0x1005c3a9a deleting)

    bool GZPaint() override;                                       // @ 0x1005c333c

private:
    // One element of the 0x10-byte vector the wrapped path builds (text + the number of bytes that fit).
    struct cTextLine {
        const char* text = nullptr;
        std::uint32_t length = 0;
        std::uint32_t padding = 0;
    };
};

// GZCOM_CreatecSC4WinText @ 0x1005c32dc: the class maker (0x198 bytes), answering the cIGZWinText view at +0x150.
gz::cIGZWinText* CreateSC4WinText(cIGZWinMgr* mgr);

}  // namespace sc4
