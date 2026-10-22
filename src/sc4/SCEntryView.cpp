#include "sc4/SCEntryView.h"

namespace sc4 {

cSCStandardTypeEntryView::cSCStandardTypeEntryView(cIGZWinMgr* mgr) : cGZWin(mgr) {
    textColor_ = MakeFillColor(0, 0, 0);
    selectionColor_ = MakeFillColor(0, 0, 0x80);
    backgroundColor_ = MakeFillColor(0xff, 0xff, 0xff);
}

bool cSCStandardTypeEntryView::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cISCStandardTypeEntryView) {
        AddRef();
        *out = static_cast<cISCStandardTypeEntryView*>(this);
        return true;
    }
    return cGZWin::QueryInterface(iid, out);
}

void cSCStandardTypeEntryView::AddBackgroundFillControl() {
    auto* rect = new gz::cGZWinFlatRect(GetWindowManager());   // COM class 0xc2afa76e
    rect->AddRef();
    cIGZWin* win = rect->AsIGZWin();
    win->SetArea(0, 0, GetW(), GetH());
    win->SetFillColor(0xe0, 0xe0, 0xe0);
    win->SetFlag(kWinFlag800, false);
    rect->SetOutlineColor(MakeFillColor(0, 0, 0));   // the original asks the graphic system for the native black
    rect->SetSizeMode(0);
    rect->SetOutlineFlags(0xf);
    ChildAdd(win);
    rect->Release();
}

}  // namespace sc4
