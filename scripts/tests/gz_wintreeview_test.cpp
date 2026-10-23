// cGZWinTreeView test: the node model (insert / transfer / remove), expansion and visibility, selection, images,
// the scrollbar model, painting into the canvas, the mouse / keyboard messages and the drag seam.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <SDL.h>

#include "fake_drawcontext.h"
#include "fake_graph.h"
#include "fake_winmgr.h"
#include "gz/WinCtrlFactory.h"
#include "gz/WinTreeView.h"
#include "rz/cRZKeyboard.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace gz;

namespace {

// 32bpp buffers so the canvas pixels can be read back.
struct TestBufferHost final : IBufferHost {
    bool HasGraphicsSystem() override { return true; }
    cGZBufferColorType DisplayColorType() override { return cGZBufferColorType{kBufferColorXRGB8888, 0x20}; }
    bool QueryTextureBitCount(std::uint32_t&) override { return false; }
};

struct Factory final : IWinCtrlFactory {
    cIGZWinMgr* mgr;
    std::uint32_t barsCreated = 0, dblClick = 500;
    explicit Factory(cIGZWinMgr* m) : mgr(m) {}
    bool CreateCtrl(std::uint32_t, std::uint32_t, void**) override { return false; }
    cIGZWinScrollbar* CreateScrollbar(std::uint32_t id, int direction, int low, int high, int value) override {
        ++barsCreated;
        auto* bar = new cGZWinScrollbar(mgr);
        bar->AddRef();
        cIGZWinScrollbar* view = nullptr;
        bar->QueryInterface(kGZIID_cIGZWinScrollbar, reinterpret_cast<void**>(&view));
        bar->Release();
        view->AsIGZWin()->SetID(id);
        view->SetOrientation(direction);
        view->SetMinMax(low, high);
        view->SetValue(value);
        return view;
    }
    std::uint32_t DefaultColor(int id) override { return 0xff000000u | static_cast<unsigned>(id & 0xffffff); }
    std::uint32_t DoubleClickMilliseconds() override { return dblClick; }
};

struct Services final : ITreeViewServices {
    Factory* factory = nullptr;
    graphfakes::Font* font = nullptr;
    int triangles = 0, buffers = 0, drags = 0;
    std::uint32_t triangleColor = 0;
    bool drop = false;
    cIGZBuffer* CreateBuffer() override {
        ++buffers;
        auto* buffer = new cGZBuffer();
        buffer->AddRef();
        return buffer;
    }
    cIGZFont* DefaultFont() override { return font; }
    cIGZWinScrollbar* CreateScrollbar(std::uint32_t id, int direction, int low, int high, int value) override {
        return factory ? factory->CreateScrollbar(id, direction, low, high, value) : nullptr;
    }
    std::uint32_t DefaultColor(std::uint32_t id) override {
        return factory ? factory->DefaultColor(static_cast<int>(id)) : 0;
    }
    std::uint32_t DoubleClickMilliseconds() override { return factory ? factory->DoubleClickMilliseconds() : 500; }
    void DrawTriangle(cIGZBuffer* target, int, int, int, int, int, int, std::uint32_t color) override {
        ++triangles;
        triangleColor = color;
        target->Fill(color, *target->GetBufferArea(), nullptr);
    }
    bool RunNodeDrag(cGZWinTreeView&, std::vector<cGZWinTreeView::GZWinTreeView_TreeNode*>& nodes, cIGZBuffer*,
                     cIGZWin*& outDragWindow) override {
        (void)nodes;
        ++drags;
        outDragWindow = nullptr;
        return drop;
    }
};

struct Target final : cGZWin {
    using cGZWin::cGZWin;
    std::vector<cGZMessage> got;
    bool allowDrag = true;
    bool DoMessage(cGZMessage& message) override {
        got.push_back(message);
        if (message.type == kGZMsgTreeViewQueryDrag && message.param2)
            *reinterpret_cast<bool*>(message.param2) = allowDrag;
        return true;
    }
    int Count(std::uint32_t type) const {
        int count = 0;
        for (const cGZMessage& message : got) count += message.type == type ? 1 : 0;
        return count;
    }
};

struct Tree final : cGZWinTreeView {
    explicit Tree(cIGZWinMgr* mgr) : cGZWinTreeView(mgr) {}
    void SetContext(cIGZDrawContext* context) { drawContext_ = context; }
};

std::uint32_t Pixel(cIGZBuffer* buffer, int x, int y) {
    return buffer ? buffer->GetPixel(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y)) : 0;
}

}  // namespace

int main() {
    FakeMgr mgr;
    FakeWinGraphics graphics;
    TestBufferHost bufferHost;
    fake_dc::Ctx context;
    Factory factory(&mgr);
    graphfakes::Font font;
    Services services;
    services.factory = &factory;
    services.font = &font;
    SetWinGraphics(&graphics);
    SetWinCtrlFactory(&factory);
    SetBufferHost(&bufferHost);
    SetTreeViewServices(&services);
    cRZKeyboard::SetKeyStateFunctions(nullptr, nullptr);
    g_ticks = 1000;
    cGZWinTreeView::ResetClickState();

    auto* parent = new Target(&mgr);
    parent->AddRef();
    parent->SetArea(0, 0, 400, 400);

    auto* tree = new Tree(&mgr);
    tree->SetID(0x1234);
    tree->SetNotificationTarget(parent);
    tree->SetArea(0, 0, 200, 100);
    parent->ChildAdd(tree);
    tree->SetContext(&context);
    tree->SetFlag(kWinEnabled, true);

    CHECK(tree->Init());
    CHECK(tree->Root() != nullptr && tree->Root()->id == 0);
    CHECK(tree->GetNodeChildrenCount(0) == 0);
    CHECK(tree->GetSelectionSize() == 0 && tree->GetSelection() == -1);
    CHECK(tree->GetRowHeight() == 16 && tree->GetMinMaxBoxSize() == 9 && tree->GetIconGutter() == 3);
    std::uint32_t gutterX = 0, gutterY = 0;
    tree->GetGutters(gutterX, gutterY);
    CHECK(gutterX == 3 && gutterY == 3);
    CHECK(tree->GetTVFlag(cIGZWinTreeView::kTVFlagCentreIcons) &&
          tree->GetTVFlag(cIGZWinTreeView::kTVFlagLines) &&
          tree->GetTVFlag(cIGZWinTreeView::kTVFlagBackground));
    CHECK(!tree->GetTVFlag(cIGZWinTreeView::kTVFlagMultiSelect));
    // The four default images were created with the stock background colour.
    CHECK(tree->Images().size() == 4);
    for (int i = 0; i < 4; ++i) {
        cIGZBuffer* image = nullptr;
        cRZRect imageArea{};
        CHECK(tree->GetImage(i, &image, imageArea));
        CHECK(image != nullptr && imageArea.right == 11 && imageArea.bottom == 11);
    }
    CHECK(services.triangles == 2 && services.triangleColor != 0);
    cIGZBuffer* unusedImage = nullptr;
    cRZRect unusedArea{};
    CHECK(!tree->GetImage(4, &unusedImage, unusedArea));

    // ---- insertion ----
    CHECK(tree->InsertNode(0, -3, cRZString("one"), nullptr) == 1);
    CHECK(tree->InsertNode(0, -3, cRZString("two"), nullptr) == 2);
    CHECK(tree->InsertNode(0, -3, cRZString("three"), nullptr) == 3);
    CHECK(tree->GetNodeChildrenCount(0) == 3);
    int children[8] = {0};
    CHECK(tree->GetNodeChildren(0, children) && children[0] == 1 && children[1] == 2 && children[2] == 3);
    CHECK(tree->GetNodeParent(2) == 0 && tree->GetNodeInsertAfter(2) == -3);
    CHECK(std::strcmp(tree->GetNodeText(2)->Data(), "two") == 0);
    CHECK(tree->InsertNode(0, -2, cRZString("zero"), nullptr) == 4);   // -2 = first child
    CHECK(tree->GetNodeChildren(0, children) && children[0] == 4 && children[1] == 1);
    CHECK(tree->InsertNode(0, 1, cRZString("after one"), nullptr) == 5);   // after sibling 1
    CHECK(tree->GetNodeChildren(0, children) && children[1] == 1 && children[2] == 5 && children[3] == 2);
    CHECK(tree->InsertNode(999, -3, cRZString("orphan"), nullptr) == -1);
    // Sorted children under node 1.
    CHECK(tree->InsertNode(1, -4, cRZString("pear"), nullptr) == 6);
    CHECK(tree->InsertNode(1, -4, cRZString("apple"), nullptr) == 7);
    CHECK(tree->InsertNode(1, -4, cRZString("fig"), nullptr) == 8);
    CHECK(tree->GetNodeChildrenCount(1) == 3);
    CHECK(tree->GetNodeChildren(1, children) && children[0] == 7 && children[1] == 8 && children[2] == 6);
    CHECK(tree->InsertNode(1, -3, cRZString("peach"), 10, 11, 12, 13) == 9);
    CHECK(tree->GetNodeIcon(9, cIGZWinTreeView::kNodeIconCollapsed) == 10);
    CHECK(tree->GetNodeIcon(9, cIGZWinTreeView::kNodeIconExpandedSelected) == 13);
    CHECK(tree->GetNodeIcon(9, static_cast<cIGZWinTreeView::TV_NodeIconIdx>(7)) == -1);
    CHECK(tree->InsertNode(3, -3, cRZString("child of three"), nullptr) == 10);
    CHECK(tree->InsertNode(0, -3, cRZString("a very long tree row label that overflows the window"), nullptr) == 11);

    // ---- node info round trip ----
    cIGZWinTreeView::TV_NodeInfo info;
    CHECK(tree->GetNodeInfo(2, info));
    CHECK(std::strcmp(info.text.Data(), "two") == 0);
    CHECK(info.icon[0] == 0 && info.icon[1] == 1 && info.icon[2] == 2 && info.icon[3] == 3);
    CHECK(info.children.empty());   // node 2 has no children
    const int userValue = 42;
    CHECK(tree->SetNodeUserData(2, const_cast<int*>(&userValue)));
    CHECK(tree->GetNodeUserData(2) == &userValue);
    info.userData = nullptr;
    info.text.AssignFromCString("deux");
    info.icon[0] = 5;
    info.textColor[0] = 0x11223344;
    info.expanded = true;
    CHECK(tree->SetNodeInfo(2, info));
    CHECK(std::strcmp(tree->GetNodeText(2)->Data(), "deux") == 0);
    CHECK(tree->GetNodeIcon(2, cIGZWinTreeView::kNodeIconCollapsed) == 5);
    CHECK(tree->GetNodeTextColor(2, cIGZWinTreeView::kNodeTextNormal) == 0x11223344);
    CHECK(tree->GetNodeExpanded(2) && tree->GetNodeUserData(2) == nullptr);
    // Colour slot quirk: the enum order and the memory order differ; the API keeps the logical order.
    CHECK(tree->SetNodeTextColor(2, cIGZWinTreeView::kNodeTextNormalShadow, 0x10));
    CHECK(tree->SetNodeTextColor(2, cIGZWinTreeView::kNodeTextSelected, 0x20));
    CHECK(tree->GetNodeTextColor(2, cIGZWinTreeView::kNodeTextNormalShadow) == 0x10);
    CHECK(tree->GetNodeTextColor(2, cIGZWinTreeView::kNodeTextSelected) == 0x20);
    CHECK(tree->SetNodeTextColor(2, static_cast<cIGZWinTreeView::TV_NodeTextColorIdx>(9), 1) == false);
    // The highlight colour shares storage with text colour 3 (a quirk of the original).
    tree->SetHighlightColor(0xabcdef);
    CHECK(tree->GetHighlightColor() == 0xabcdef);
    CHECK(tree->GetDefaultNodeTextColor(cIGZWinTreeView::kNodeTextSelectedShadow) == 0xabcdef);
    tree->SetDefaultNodeTextColor(cIGZWinTreeView::kNodeTextSelectedShadow, 0x123456);
    CHECK(tree->GetHighlightColor() == 0x123456);
    CHECK(tree->GetDefaultNodeTextColor(static_cast<cIGZWinTreeView::TV_NodeTextColorIdx>(7)) == 0);
    // The background colour is exchanged as a native colour and stored packed.
    std::uint32_t native = 0;
    tree->SetBackgroundColor(0xff010203);
    native = tree->GetBackgroundColor();
    CHECK((native & 0xffffff) == 0x010203);

    // Fonts: a node keeps the default font unless one is set; a null font means "the default".
    graphfakes::Font otherFont;
    CHECK(tree->GetNodeFont(1) == &font);
    CHECK(tree->SetNodeFont(1, &otherFont));
    CHECK(tree->GetNodeFont(1) == &otherFont);
    CHECK(tree->SetNodeFont(1, nullptr));
    CHECK(tree->GetNodeFont(1) == &font);
    tree->SetDefaultFont(&otherFont);

    // ---- expansion and visibility ----
    tree->SetTVFlag(cIGZWinTreeView::kTVFlagDrawGroupRow, false);
    tree->SetNodeExpanded(1, true);
    CHECK(tree->GetNodeVisible(1));
    CHECK(tree->GetNodeVisible(7));   // child of the expanded node
    tree->SetNodeExpanded(1, false);
    CHECK(!tree->GetNodeVisible(7));
    CHECK(tree->SetNodeVisible(8));   // expands the ancestor
    CHECK(tree->GetNodeExpanded(1));
    CHECK(tree->GetNodeVisible(8));
    CHECK(!tree->SetNodeVisible(12345));
    CHECK(parent->Count(kGZMsgTreeViewExpand) >= 1);

    // ---- selection ----
    parent->got.clear();
    CHECK(tree->AddSelection(2, true));
    CHECK(tree->GetSelectionSize() == 1 && tree->GetSelection() == 2 && tree->GetNodeSelected(2));
    CHECK(parent->Count(kGZMsgTreeViewSelectionChanged) == 1);
    CHECK(tree->AddSelection(7, true));   // replaces the single selection
    CHECK(tree->GetSelectionSize() == 1 && tree->GetSelection() == 7 && !tree->GetNodeSelected(2));
    tree->SetTVFlag(cIGZWinTreeView::kTVFlagMultiSelect, true);
    CHECK(tree->AddSelection(2, false));
    CHECK(tree->GetSelectionSize() == 2);
    int selection[4] = {0};
    CHECK(tree->GetSelection(selection) && selection[0] == 7 && selection[1] == 2);
    CHECK(tree->RemoveSelection(7) && tree->GetSelectionSize() == 1 && !tree->GetNodeSelected(7));
    CHECK(!tree->RemoveSelection(9));
    CHECK(tree->AddSelection(9, false) && tree->GetSelectionSize() == 2);
    CHECK(tree->ClearSelection() && tree->GetSelectionSize() == 0);
    CHECK(tree->AddSelection(1, false));
    CHECK(tree->RemoveNode(4));
    CHECK(tree->GetSelectionSize() == 1);

    // ---- painting and the scrollbar model ----
    tree->SetArea(0, 0, 120, 70);
    context.log.clear();
    CHECK(tree->GZPaint());
    CHECK(tree->Canvas() != nullptr);
    CHECK(tree->Canvas()->Width() > 0 && tree->Canvas()->Height() > 0);
    CHECK(!context.log.empty());
    bool presented = false;
    for (const std::string& entry : context.log) presented = presented || entry.rfind("blt ", 0) == 0;
    CHECK(presented);   // the canvas is blitted to the window's draw context
    CHECK(Pixel(tree->Canvas(), 0, 0) == 0xff010203u);   // the packed background is also passed to Fill (quirk)
    CHECK(tree->VisibleNodes().size() >= 3);
    const std::size_t rows = tree->VisibleNodes().size();
    CHECK(tree->VisibleRowCount() == rows);
    CHECK(tree->RowsPerPage() >= 1);
    CHECK(!font.glyphs.empty());
    CHECK(tree->VScrollbar() != nullptr);
    CHECK(tree->HScrollbar() != nullptr);   // the rows are wider than the 120 pixel window
    CHECK(tree->ScrollbarThickness() == 16);
    CHECK(tree->GetChildWindowFromID(kTreeViewVScrollId) != nullptr);
    CHECK(tree->GetChildWindowFromID(kTreeViewHScrollId) != nullptr);
    int minimum = 0, maximum = 0;
    tree->VScrollbar()->GetMinMax(minimum, maximum);
    CHECK(minimum == 0 && maximum >= 1);
    tree->HScrollbar()->GetMinMax(minimum, maximum);
    CHECK(minimum == 0 && maximum > 0);
    // The scrollbar command messages drive the top row / the horizontal offset.
    cGZMessage scroll;
    scroll.type = kGZMsgCommand;
    scroll.param1 = kGZMsgTreeViewCommandSource;
    scroll.param2 = kTreeViewVScrollId;
    tree->VScrollbar()->SetValue(2);
    tree->DoMessage(scroll);
    CHECK(tree->TopRow() == 2);
    scroll.param2 = kTreeViewHScrollId;
    tree->HScrollbar()->SetValue(5);
    tree->DoMessage(scroll);
    CHECK(tree->HScrollOffset() == 5);
    tree->VScrollbar()->SetValue(0);
    scroll.param2 = kTreeViewVScrollId;
    tree->DoMessage(scroll);
    CHECK(tree->TopRow() == 0);

    // ---- painting at a size where every row is visible, then mouse hit testing ----
    tree->SetArea(0, 0, 220, 220);
    tree->GZPaint();
    CHECK(tree->RowsPerPage() >= static_cast<int>(rows));
    for (std::size_t i = 0; i < rows; ++i) CHECK(tree->VisibleNodes()[i]->rowIndex == i);
    tree->SetNodeExpanded(3, false);
    tree->SetNodeExpanded(1, true);
    tree->GZPaint();
    CHECK(tree->VisibleRowCount() == rows);
    std::uint32_t gx = 0, gy = 0;
    tree->GetGutters(gx, gy);
    // Click the expand box of node 3 (a child of the root with one child, so it has a box).
    cGZWinTreeView::GZWinTreeView_TreeNode* node3 = tree->FindNode(3);
    CHECK(node3 && node3->boxDrawn);
    if (node3 && node3->boxDrawn) {
        const int clickX = node3->boxRect.left + 1 + static_cast<int>(gx);
        const int clickY = node3->boxRect.top + 1 + static_cast<int>(gy);
        parent->got.clear();
        CHECK(tree->GZOnMouseDownL(clickX, clickY, 0));
        CHECK(tree->GetNodeExpanded(3));
        CHECK(parent->Count(kGZMsgTreeViewExpand) == 1);
        CHECK(parent->Count(kGZMsgTreeViewCommand) == 1);
        g_ticks += 10;   // same box within the double click time: the box branch toggles again, no double click
        CHECK(tree->GZOnMouseDownL(clickX, clickY, 0));
        CHECK(!tree->GetNodeExpanded(3));
        CHECK(parent->Count(kGZMsgTreeViewCollapse) == 1);
        CHECK(parent->Count(kGZMsgTreeViewDoubleClick) == 0);
    }
    // Click the text of node 5 to select it.
    tree->GZPaint();
    cGZWinTreeView::GZWinTreeView_TreeNode* node5 = tree->FindNode(5);
    CHECK(node5 && node5->textDrawn);
    if (node5 && node5->textDrawn) {
        const int clickX = node5->textRect.left + 1 + static_cast<int>(gx);
        const int clickY = node5->textRect.top + 1 + static_cast<int>(gy);
        parent->got.clear();
        CHECK(tree->GZOnMouseDownL(clickX, clickY, 0));
        CHECK(tree->GetNodeSelected(5));
        CHECK(parent->Count(kGZMsgTreeViewCommand) == 1);
        // Right click on the text.
        parent->got.clear();
        CHECK(tree->GZOnMouseDownR(clickX, clickY, 0));
        CHECK(parent->Count(kGZMsgTreeViewRightClick) == 1);
        CHECK(parent->Count(kGZMsgTreeViewCommand) == 1);
        CHECK(tree->GetSelectionSize() == 1 && tree->GetSelection() == 5);
    }
    CHECK(!tree->GZOnMouseDownR(1, 1, 0));
    // Double click on the text of node 1 (which has children) toggles its expansion.
    tree->GZPaint();
    cGZWinTreeView::GZWinTreeView_TreeNode* node1 = tree->FindNode(1);
    CHECK(node1 && node1->textDrawn);
    if (node1 && node1->textDrawn) {
        const int clickX = node1->textRect.left + 1 + static_cast<int>(gx);
        const int clickY = node1->textRect.top + 1 + static_cast<int>(gy);
        parent->got.clear();
        const bool wasExpanded = tree->GetNodeExpanded(1);
        CHECK(tree->GZOnMouseDownL(clickX, clickY, 0));
        g_ticks += 10;
        CHECK(tree->GZOnMouseDownL(clickX, clickY, 0));
        CHECK(parent->Count(kGZMsgTreeViewDoubleClick) == 1);
        CHECK(tree->GetNodeExpanded(1) != wasExpanded);
    }

    // ---- keyboard ----
    CHECK(tree->AddSelection(5, true) && tree->GetSelection() == 5);
    parent->got.clear();
    CHECK(tree->GZOnKeyDown(0x28, 0));   // Down
    const int afterDown = tree->GetSelection();
    CHECK(afterDown != -1 && afterDown != 5);
    CHECK(tree->GZOnKeyDown(0x26, 0));   // Up
    CHECK(tree->GetSelection() == 5);
    CHECK(tree->GZOnKeyDown(0x23, 0));   // End
    CHECK(tree->GetSelection() != -1);
    CHECK(tree->GZOnKeyDown(0x24, 0));   // Home
    CHECK(tree->GetSelection() != -1);
    CHECK(tree->GZOnKeyDown(0x21, 0));   // Page up
    CHECK(tree->GZOnKeyDown(0x22, 0));   // Page down
    CHECK(tree->GZOnKeyDown(0x41, 0));   // unhandled
    tree->AddSelection(1, false);
    CHECK(tree->GZOnKeyDown(0xd, 0));    // Return toggles the expansion of the selected nodes
    CHECK(parent->Count(kGZMsgTreeViewDoubleClick) >= 1);
    CHECK(parent->Count(kGZMsgTreeViewExpand) + parent->Count(kGZMsgTreeViewCollapse) >= 1);
    CHECK(tree->GZOnKeyDown(0x1b, 0));   // Escape clears the selection
    CHECK(tree->GetSelectionSize() == 0);
    CHECK(!tree->GZOnKeyUp(0x28, 0));

    // ---- wheel ----
    tree->GZPaint();
    const int beforeWheel = tree->TopRow();
    CHECK(tree->GZOnMouseWheel(10, 10, 0, 1));    // at the top: nothing
    CHECK(tree->TopRow() == beforeWheel);
    CHECK(tree->GZOnMouseWheel(10, 10, 0, -1));

    // ---- drag seam ----
    tree->GZPaint();
    tree->AddSelection(5, true);
    CHECK(tree->GetSelection() == 5);
    parent->allowDrag = true;
    parent->got.clear();
    // Drag from row 2 (an unselected node).
    const int fromX = 40;
    const int fromY = static_cast<int>(2 * tree->GetRowHeight()) + 8 + static_cast<int>(gy);
    struct DragServices final : ITreeViewServices {
        Services* inner;
        int x = 0, y = 0;
        explicit DragServices(Services* s) : inner(s) {}
        cIGZBuffer* CreateBuffer() override { return inner->CreateBuffer(); }
        cIGZFont* DefaultFont() override { return inner->DefaultFont(); }
        cIGZWinScrollbar* CreateScrollbar(std::uint32_t id, int d, int l, int h, int v) override {
            return inner->CreateScrollbar(id, d, l, h, v);
        }
        std::uint32_t DefaultColor(std::uint32_t id) override { return inner->DefaultColor(id); }
        std::uint32_t DoubleClickMilliseconds() override { return inner->DoubleClickMilliseconds(); }
        void DrawTriangle(cIGZBuffer* b, int a, int c, int d, int e, int f, int g, std::uint32_t col) override {
            inner->DrawTriangle(b, a, c, d, e, f, g, col);
        }
        bool RunNodeDrag(cGZWinTreeView& tree, std::vector<cGZWinTreeView::GZWinTreeView_TreeNode*>& nodes,
                         cIGZBuffer*, cIGZWin*& out) override {
            tree.NotifyDrag(&nodes, x, y, false);   // the drag window selects the node under the cursor
            out = nullptr;
            return true;
        }
    } dragServices(&services);
    dragServices.x = fromX;
    dragServices.y = fromY;
    SetTreeViewServices(&dragServices);
    CHECK(tree->GZOnMouseMove(fromX, fromY, 0x10));
    CHECK(parent->Count(kGZMsgTreeViewQueryDrag) == 1);
    CHECK(parent->Count(kGZMsgTreeViewDropped) == 1);
    CHECK(tree->GetSelection() == 5);   // the dragged node is selected again after the drop
    CHECK(services.buffers >= 2);       // the drag image was built
    SetTreeViewServices(&services);
    // Without a drag window the seam reports no drop and the selection is restored.
    services.drop = false;
    CHECK(tree->GZOnMouseMove(fromX, fromY, 0x10));
    CHECK(!tree->SelectionList().empty());
    // A move without the left button or without a selection never asks for a drag.
    parent->got.clear();
    tree->GZOnMouseMove(fromX, fromY, 0);
    tree->ClearSelection();
    tree->GZOnMouseMove(fromX, fromY, 0x10);
    CHECK(parent->Count(kGZMsgTreeViewQueryDrag) == 0);
    parent->allowDrag = false;
    tree->AddSelection(5, false);
    tree->GZOnMouseMove(fromX, fromY, 0x10);
    CHECK(parent->Count(kGZMsgTreeViewQueryDrag) == 1);

    // ---- transfer ----
    CHECK(tree->GetNodeParent(9) == 1);
    tree->TransferNode(9, 3, -3);
    CHECK(tree->GetNodeParent(9) == 3);
    CHECK(tree->GetNodeChildrenCount(1) == 3);
    CHECK(tree->GetNodeChildrenCount(3) == 2);   // node 10 and node 9
    tree->TransferNode(9, 3, -2);
    int firstChildren[4] = {0};
    CHECK(tree->GetNodeChildren(3, firstChildren) && firstChildren[0] == 9);
    tree->TransferNode(12345, 3, -3);   // unknown node: no-op

    // ---- removal ----
    CHECK(tree->RemoveNode(9));
    CHECK(tree->FindNode(9) == nullptr);
    CHECK(tree->GetNodeChildrenCount(3) == 1);
    CHECK(!tree->RemoveNode(9));
    // Removing node 1 removes its subtree (7, 8, 6) from the map.
    CHECK(tree->RemoveNode(1));
    CHECK(tree->FindNode(1) == nullptr && tree->FindNode(6) == nullptr && tree->FindNode(7) == nullptr);
    CHECK(tree->GetNodeChildrenCount(0) == 4);   // 4, 5, 2, 3
    tree->RemoveAllNodes();
    CHECK(tree->GetNodeChildrenCount(0) == 0);
    CHECK(tree->Root() != nullptr);
    CHECK(tree->InsertNode(0, -3, cRZString("again"), nullptr) > 0);
    CHECK(tree->GetNodeChildrenCount(0) == 1);

    // ---- teardown ----
    CHECK(tree->Shutdown());
    parent->Release();   // the parent destroys the tree (cGZWin::~cGZWin -> ChildDeleteAll)
    SetTreeViewServices(nullptr);
    SetBufferHost(nullptr);
    std::printf(failures ? "gz_wintreeview: %d FAILURES\n" : "gz_wintreeview: ok\n", failures);
    return failures ? 1 : 0;
}
