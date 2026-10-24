#include "sc4/WinAdviceList.h"

#include <cstdio>
#include <string>

#include "gz/cIGZMessage2.h"
#include "rz/cRZMessage2Standard.h"
#include "sc4/Advice.h"

namespace sc4 {

namespace {

IWinAdviceListHost* g_host = nullptr;

// The index out of a "sc4://action/....?item=%d" link: the original copies the tail of the URL into a cRZString and
// sscanf's "item=%d" out of it (0x1005c7d4d..0x1005c7d8c).
bool ParseItemIndex(const cRZString& url, std::size_t prefixLength, int& index) {
    const cRZString tail(url, prefixLength, std::string::npos);
    return std::sscanf(tail.ToChar(), "item=%d", &index) == 1;
}

}  // namespace

IWinAdviceListHost* WinAdviceListHost() { return g_host; }
void SetWinAdviceListHost(IWinAdviceListHost* host) { g_host = host; }

// -------------------------------------------------------------------------------------------------- cAdviceEntry

cAdviceEntry::cAdviceEntry(const cAdviceEntry& other) {   // @ 0x1005c8a1c
    e = other.e;
    a = other.a;
    b = other.b;
    c = other.c;
    d = other.d;
    enabled = other.enabled;
    advice = other.advice;
    if (advice) advice->AddRef();
    noLink = other.noLink;
}

cAdviceEntry::~cAdviceEntry() {   // @ 0x1005c893c
    if (advice) advice->Release();
}

void cAdviceEntry::SetAdvice(cISC4Advice* value) {
    if (advice == value) return;
    if (value) value->AddRef();
    cISC4Advice* previous = advice;
    advice = value;
    if (previous) previous->Release();
}

// -------------------------------------------------------------------------------------------------- cSC4WinAdviceList

// The class has no constructor symbol of its own: the GZCOM creator (0x1005c7870) allocates 0x180 bytes and inlines
// cGZWin's no-argument constructor (0x100168870) plus the +0x150 vptr, the null +0x158 and the empty list at +0x160.
// The port's cGZWin has no default constructor, so this one takes the window manager the creator would have set.
cSC4WinAdviceList::cSC4WinAdviceList(cIGZWinMgr* mgr) : cGZWin(mgr) {}

cSC4WinAdviceList::~cSC4WinAdviceList() {   // @ 0x1005c7918
    entries_.clear();
    // The original's D2 only clears the list and runs cGZWin::~cGZWin: the reference Init took on the html child is
    // never dropped. The port releases it here (the only deliberate behaviour change, a leak fix).
    if (html_) {
        html_->Release();
        html_ = nullptr;
    }
}

bool cSC4WinAdviceList::QueryInterface(std::uint32_t iid, void** out) {   // @ 0x1005c79e2
    if (iid == kGZIID_cIGZWinAdviceList || iid == 1) {
        AddRef();
        *out = static_cast<cIGZWinAdviceList*>(this);
        return true;
    }
    if (iid == kGZIID_cSC4WinAdviceList) {
        AddRef();
        *out = static_cast<cIGZWin*>(this);
        return true;
    }
    return false;
}

bool cSC4WinAdviceList::Init() {   // @ 0x1005c7a54
    if (get_init_flag()) return true;
    IAdviceListHtml* html = nullptr;
    if (!cGZWin::Init()) return false;
    if (g_host) html = g_host->CreateHtmlWindow();
    if (!html) return false;
    html->SetArea(0, 0, GetW(), GetH());   // AsIGZWin()->SetArea
    html->ClearFlag(kAdviceListHtmlFlag);
    ChildAdd(html->AsIGZWin());
    html_ = html;
    return true;
}

bool cSC4WinAdviceList::Shutdown() {   // @ 0x1005c7b7a
    ClearList();
    // The original does not release the html child here either (cGZWin::Shutdown's ChildDeleteAll only drops the
    // window from the tree); the reference the creation took is dropped by the destructor in this port.
    return cGZWin::Shutdown();
}

void cSC4WinAdviceList::SetArea(int left, int top, int right, int bottom) {   // @ 0x1005c7b9a
    cGZWin::SetArea(left, top, right, bottom);
    if (html_) html_->SetArea(0, 0, right - left, bottom - top);
}

cIGZWin* cSC4WinAdviceList::AsIGZWin() {   // @ 0x1005c88b2
    return static_cast<cIGZWin*>(this);
}

void cSC4WinAdviceList::SetGutters(int left, int top, int right, int bottom) {   // @ 0x1005c7c0c
    if (html_) html_->SetGutters(left, top, right, bottom);
}

void cSC4WinAdviceList::ScrollToAnchor(const cIGZString& anchor) {   // @ 0x1005c8848
    if (html_) html_->ScrollToAnchor(anchor);
}

void cSC4WinAdviceList::DisableVerticalScrollbar() {   // @ 0x1005c8870
    if (html_) html_->ClearFlag(kAdviceListHtmlVScrollFlag);   // cIGZWinHTML::ClearFlag(8)
}

void cSC4WinAdviceList::SetbgImage(std::uint32_t image) {   // @ 0x1005c828a
    bgImage_ = image;
}

void cSC4WinAdviceList::ClearList() {   // @ 0x1005c8828
    entries_.clear();
}

void cSC4WinAdviceList::AddEntry(const cIGZString& a, const cIGZString& b, const cIGZString& c, const cIGZString& d,
                                 bool enabled, cISC4Advice* advice, const cIGZString& e, bool noLink,
                                 std::uint32_t image) {   // @ 0x1005c806e
    entries_.push_back(cAdviceEntry());
    cAdviceEntry& entry = entries_.back();   // the original's push_back 0x1005c813c then the "last entry" reload
    entry.enabled = enabled;
    entry.a = cRZString(a);
    entry.b = cRZString(b);
    entry.c = cRZString(c);
    entry.d = cRZString(d);
    entry.e = cRZString(e);
    entry.SetAdvice(advice);
    entry.noLink = noLink;
    bgImage_ = image;
}

// -------------------------------------------------------------------------------------------------- the HTML list

void cSC4WinAdviceList::RebuildHTML(bool force) {   // @ 0x1005c82a0
    cRZString html;
    int scrollX = 0;
    int scrollY = 0;
    if (force && html_) html_->GetScrollPosition(scrollX, scrollY);   // cIGZWinHTML vslot 0x30

    cRZString column;
    const int width = html_ ? html_->WindowWidth() : 0;   // AsIGZWin()->GetW(); the original dereferences it
    column.Sprintf("</TD><TD WIDTH=\"%d\">", width - kAdviceListColumnBias);

    if (bgImage_ != 0) {
        html.Sprintf("<HTML><HEAD></HEAD><BODY BACKGROUND=\"sc4://HTML/46a006b0/%x\"><FONT FACE=\"Arta\" SIZE=3>"
                     "<TABLE BORDER=0>",
                     bgImage_);
    } else {
        html = cRZString("<HTML><HEAD></HEAD><BODY BGCOLOR=\"#dee8e3\"><FONT FACE=\"Arta\" SIZE=3>"
                         "<TABLE BORDER=0>");
    }

    int index = 0;
    for (const cAdviceEntry& entry : entries_) {
        html.Strcat("<TR><TD WIDTH=\"18\">");
        const cRZString& title = entry.enabled ? entry.a : entry.b;
        if (!entry.noLink) {
            cRZString piece;
            piece.Sprintf("<A NAME=\"item%d\" HREF=\"sc4://action/toggle?item=%d\">", index, index);
            html.AppendString(piece);
        }
        html.AppendString(title);
        if (!entry.noLink) html.Strcat("</A>");
        html.AppendString(column);
        if (!entry.noLink) {
            cRZString piece;
            piece.Sprintf("<A HREF=\"sc4://action/toggle?item=%d\"><SPAN STYLE=\"text-decoration: none\">", index);
            html.AppendString(piece);
            html.AppendString(entry.d);
            html.Strcat("</SPAN></A>");
        }
        if (entry.enabled) {
            html.Strcat("<BR>");
            html.AppendString(entry.c);
        }
        {
            cRZString piece;
            piece.Sprintf("</TD><TD WIDTH=\"18\"><A NAME=\"item%d\" HREF=\"sc4://action/close?item=%d\">", index,
                          index);
            html.AppendString(piece);
        }
        html.AppendString(entry.e);
        html.Strcat("</A></TD></TR>");
        ++index;
    }
    html.Strcat("</TABLE></FONT></BODY></HTML>");

    if (!html_) return;
    const std::string document(html.ToChar(), html.Strlen());
    html_->SetDocument(document, cRZString(kAdviceListBaseUrl));   // cIGZWinHTML vslot 0xb0
    if (force) html_->SetScrollPosition(scrollX, scrollY);          // vslot 0x38
}

// -------------------------------------------------------------------------------------------------- link handling

bool cSC4WinAdviceList::DoMessage(cGZMessage& message) {   // @ 0x1005c7c48
    if (message.type != kAdviceListMsgLinkJump) return cGZWin::DoMessage(message);
    const auto* url = reinterpret_cast<const cRZString*>(static_cast<std::uintptr_t>(message.param2));
    if (!url) return false;
    // Quirk kept: the click sound plays for every link notification, before the URL is looked at.
    if (g_host) g_host->PlaySound(kAdviceListClickSound);

    const bool toggle = url->BeginsWith(kAdviceListUrlToggle, kAdviceListUrlToggleLength);
    const bool close = !toggle && url->BeginsWith(kAdviceListUrlClose, kAdviceListUrlCloseLength);
    if (!toggle && !close) {
        // cSC4HTMLLinkHandler::DoURL; the notification reports the inverse of its answer.
        return !(g_host && g_host->HandleUrl(*url));
    }

    int index = 0;
    if (!ParseItemIndex(*url, toggle ? kAdviceListUrlToggleLength : kAdviceListUrlCloseLength, index)) return false;
    // Port guard: the original walks off the list for an index outside [0, size).
    if (index < 0 || static_cast<std::size_t>(index) >= entries_.size()) return false;
    auto it = entries_.begin();
    std::advance(it, index);
    cAdviceEntry& entry = *it;

    if (toggle) {
        entry.enabled = !entry.enabled;
        if (entry.advice) {
            auto* msg = new cRZMessage2Standard();
            msg->AddRef();
            msg->SetType(kAdviceListMsgAdviceToggled);
            msg->SetData1(static_cast<std::uint64_t>(entry.advice->GetAdvisorID()));
            msg->SetData2(static_cast<std::uint64_t>(entry.advice->GetMood()));
            if (g_host) {
                if (cIGZMessageServer2* server = g_host->MessageServer()) server->MessagePost(msg, false);
            }
            msg->Release();
            entry.advice->SetFlags(kAdviceListToggleAdviceFlag, entry.enabled);
        }
        RebuildHTML(true);
        return false;   // (sic) the handled link still reports "not handled"
    }

    if (entry.advice) {
        entry.advice->SetFlags(kAdviceListNoLinksAdviceFlag, true);
        auto* msg = new cRZMessage2Standard();
        msg->AddRef();
        msg->SetType(kAdviceListMsgAdviceRemoved);
        msg->SetIGZUnknown(entry.advice);
        msg->SetData1(kAdviceListMsgAdviceRemoved);
        if (g_host) {
            if (cIGZMessageServer2* server = g_host->MessageServer()) server->MessageSend(msg);
        }
        msg->Release();
    }
    RebuildHTML(true);
    return false;
}

}  // namespace sc4
