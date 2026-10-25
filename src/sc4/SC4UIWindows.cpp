// nSC4UI window helpers that need nothing but the window interfaces: the host pointer, child sets, alignment and the cursor-position
// search. Split out of SC4UI.cpp so the screens that only use these do not link the catalogs, dialogs and the default engine host.
#include "sc4/SC4UI.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <vector>

#include "gz/RZServices.h"
#include "gz/WinInterfaces.h"
#include "rz/RZStringUtils.h"

namespace {
nSC4UI::INSC4UIHost* g_host = nullptr;
// ---- EnumChildren callbacks ---------------------------------------------------------------------------

// GetWindowChildrenSetEnum @ 0x10030ad94: every child (the iid is cIGZWin, so `child` is the window) goes into
// the set; the original always answers 1.
bool CollectChild(cIGZWin*, std::uint32_t, void* child, void* context) {
    static_cast<std::set<cIGZWin*>*>(context)->insert(static_cast<cIGZWin*>(child));
    return true;
}

// SetNoFocusFlagCallback @ 0x10030b25c: clear 0x8000 on the child, recurse into it, answer false (which only
// makes EnumChildren report a failure; it keeps walking the siblings).
bool ClearNoFocusFlag(cIGZWin*, std::uint32_t, void* child, void* context) {
    auto* window = static_cast<cIGZWin*>(child);
    window->SetFlag(nSC4UI::kNoFocusFlag, false);
    window->EnumChildren(nSC4UI::kChildEnumIID, &ClearNoFocusFlag, context);
    return false;
}

// SC4GetChildWindowForCursorPosEnumProc @ 0x10030a9cb: the first visible child that ignores the cursor (flag
// 0x200000) and contains the screen point becomes the answer, and the search continues inside it.
struct CursorHit {
    int x = 0;
    int y = 0;
    cIGZWin* found = nullptr;
};

bool FindChildAtCursor(cIGZWin*, std::uint32_t, void* child, void* context) {
    auto* hit = static_cast<CursorHit*>(context);
    auto* window = static_cast<cIGZWin*>(child);
    if (hit->found) return true;
    if (!window->IsVisible()) return true;
    if (window->GetFlag(kWinIgnoreCursor)) return true;
    if (!window->IsPointInWindowScreenCoordinates(hit->x, hit->y)) return true;
    hit->found = window;
    window->EnumChildren(nSC4UI::kChildEnumIID, &FindChildAtCursor, context);
    return true;
}

}  // namespace

namespace nSC4UI {
// SC4UI.cpp defines the engine-backed default host; a program that does not link it simply has none.
__attribute__((weak)) INSC4UIHost* DefaultHost() { return nullptr; }

INSC4UIHost* Host() { return g_host ? g_host : DefaultHost(); }
void SetHost(INSC4UIHost* host) { g_host = host; }

// ---- window sets ---------------------------------------------------------------------------------------

// @ 0x10030ae2d. The original clears the set, collects the direct children and answers the libc++ tree size
// (the caller uses the count, not a bool).
std::uint32_t GetWindowChildrenSet(cIGZWin* window, std::set<cIGZWin*>& children) {
    children.clear();
    if (!window) return 0;   // port guard: the original dereferences the window
    window->EnumChildren(kChildEnumIID, &CollectChild, &children);
    return static_cast<std::uint32_t>(children.size());
}

// @ 0x10030ae83. Collects the children, takes the set difference with `excluded` and *assigns* it to `out`
// (the original's __assign_multi replaces the destination contents), then answers its size.
std::uint32_t GetWindowChildrenNotInSet(cIGZWin* window, const std::set<cIGZWin*>& excluded,
                                        std::set<cIGZWin*>& out) {
    std::set<cIGZWin*> children;
    if (window) window->EnumChildren(kChildEnumIID, &CollectChild, &children);
    std::set<cIGZWin*> difference;
    std::set_difference(children.begin(), children.end(), excluded.begin(), excluded.end(),
                        std::inserter(difference, difference.end()));
    out = difference;
    return static_cast<std::uint32_t>(out.size());
}

// @ 0x10030ad12. The anchor child id is the constant 0xaaaa (the original passes 0xaaaa and 0 for the other two
// arguments). The return is always true.
bool AlignWindowSetToWindow(const std::set<cIGZWin*>& windows, cIGZWin* parent) {
    for (cIGZWin* window : windows) {
        if (!window) continue;
        AlignWindowToWindow(window->GetID(), kAlignChildId, 0, parent);
    }
    return true;
}

// The window with `id` closest to `window` (breadth first). The anchors of a script window carry the same id (0xaaaa) at every level
// of its tree; the one that positions the window is the outermost. A depth-first search (cGZWin::GetChildWindowFromIDRecursive) finds
// a nested panel's anchor first, whose absolute position is thousands of pixels off.
cIGZWin* FindShallowestChild(cIGZWin* window, std::uint32_t id) {
    std::vector<cIGZWin*> level{window};
    while (!level.empty()) {
        std::vector<cIGZWin*> next;
        for (cIGZWin* parent : level) {
            std::vector<cIGZWin*> children;
            parent->EnumChildren(kChildEnumIID, [](cIGZWin*, std::uint32_t, void* child, void* out) {
                static_cast<std::vector<cIGZWin*>*>(out)->push_back(static_cast<cIGZWin*>(child));
                return true;
            }, &children);
            for (cIGZWin* child : children) {
                if (child->GetID() == id) return child;
                next.push_back(child);
            }
        }
        level = std::move(next);
    }
    return nullptr;
}

// @ 0x10030aaaa.
bool AlignWindowToWindow(std::uint32_t windowId, std::uint32_t childId, std::uint32_t alignId, cIGZWin* parent) {
    if (!parent) {
        if (cIGZWinMgr* manager = Host()->WindowManager()) parent = manager->GetMainWindow();
    }
    if (!parent) return false;   // port guard: the original dereferences the main window

    cIGZWin* window = parent->GetChildWindowFromIDRecursive(windowId);   // original vslot 0x118
    cIGZWin* child = window ? FindShallowestChild(window, childId) : nullptr;
    if (!window || !child) return false;

    if (alignId == 0) {
        const cIGZString& caption = child->GetCaption();   // original vslot 0x248
        const char* text = caption.ToChar();
        if (caption.Strlen() > 2 && text[0] == '-') {
            static const char kVertical[] = "tmb";
            static const char kHorizontal[] = "lcr";
            const char* vertical = static_cast<const char*>(std::memchr(kVertical, text[1], 4));
            const char* horizontal = static_cast<const char*>(std::memchr(kHorizontal, text[2], 4));
            // Quirk kept: a caption that starts with '-' but carries an unknown letter answers true and does
            // nothing at all (the original sets its result to 1 before testing the two letters).
            if (!vertical || !horizontal) return true;

            cIGZWinMgr* manager = Host()->WindowManager();
            cIGZWin* main = manager ? manager->GetMainWindow() : nullptr;
            if (!main) return false;   // port guard
            const cRZRect& mainArea = main->GetAreaAbsolute();   // original vslot 0x188
            const cRZRect& area = child->GetAreaAbsolute();
            const int factorX = static_cast<int>(horizontal - kHorizontal);
            const int factorY = static_cast<int>(vertical - kVertical);
            const int x = (mainArea.right - mainArea.left - (area.right - area.left)) * factorX / 2 - area.left;
            const int y = (mainArea.bottom - mainArea.top - (area.bottom - area.top)) * factorY / 2 - area.top;
            window->GZWinOffset(x, y);   // original vslot 0x1c8
            return true;
        }
        // Anything else is a hex window id in the caption.
        alignId = RZAtoUint32HexA(text);
        if (alignId == 0) return false;
    }

    cIGZWin* reference = parent->GetChildWindowFromIDRecursive(alignId);
    if (!reference) return false;
    AlignWindowToWindow(window, child, reference);
    return true;
}

// @ 0x10030ac8a. Moves `window` by the difference between the two origins. The original reads the origins with
// WindowToScreenCoordinates(0, 0) (vslot 0x1e0) and always answers true.
bool AlignWindowToWindow(cIGZWin* window, cIGZWin* alignTo, cIGZWin* reference) {
    int referenceX = 0, referenceY = 0, alignToX = 0, alignToY = 0;
    if (!window || !alignTo || !reference) return false;   // port guard
    reference->WindowToScreenCoordinates(referenceX, referenceY);
    alignTo->WindowToScreenCoordinates(alignToX, alignToY);
    window->GZWinOffset(referenceX - alignToX, referenceY - alignToY);
    return true;
}

// @ 0x10030b220.
void ClearAcceptFocusFlagRecursive(cIGZWin* window) {
    if (!window) return;   // port guard
    window->SetFlag(kNoFocusFlag, false);
    window->EnumChildren(kChildEnumIID, &ClearNoFocusFlag, nullptr);
}

// ---- cursor hit test -----------------------------------------------------------------------------------

// @ 0x10030a98e.
cIGZWin* GetChildWindowForCursorPos(cIGZWin* window, int x, int y) {
    if (!window) return nullptr;
    CursorHit hit{x, y, nullptr};
    window->EnumChildren(kChildEnumIID, &FindChildAtCursor, &hit);
    return hit.found;
}

// @ 0x10030aa41: repeat the search on the window it found until nothing is hit; the original answers the last
// window it entered (pcVar1 = the previous answer).
cIGZWin* GetChildWindowForCursorPosRecursive(cIGZWin* window, int x, int y) {
    cIGZWin* last = nullptr;
    while (cIGZWin* found = GetChildWindowForCursorPos(window, x, y)) {
        last = found;
        window = found;
    }
    return last;
}

}  // namespace nSC4UI
