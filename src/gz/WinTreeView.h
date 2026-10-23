// cGZWinTreeView — the GZ tree control (93 tracked function symbols 0x100519550..0x10051edc2, plus DrawNode
// 0x10051d142 and NotifyDrag 0x10051ee1a, iid 0x3ae8bad2 for the
// cIGZWinTreeView view at +0). It is an ordinary cGZWin at +8 with a full vtable of its own at +0:
//
//   +0x00 cIGZWinTreeView vtable (iid 0x3ae8bad2), 57 interface methods (slot n at 8n, [3] = SetTVFlag .. [59] = AsIGZWin)
//   +0x08 cGZWin (0x150 bytes; the window rectangle, children, colours, message dispatch)
//   +0x158 next node id (the first node allocated gets id 0, the root)
//   +0x160 std::map<int, TreeNode*> nodes (id -> node; the map node holds the key at +0x20)
//   +0x178 std::vector<TreeNode*> — never used by the class (the destructor frees its storage; kept for fidelity)
//   +0x190 std::vector<TreeNode*> visible rows, rebuilt by RecalculateAll / FindTreeInfo (row 0 at [0])
//   +0x1a8 std::vector<TreeNode*> selection (selected nodes, newest last)
//   +0x1c0 root TreeNode*, +0x1c8 default font (RZ::WCM() vslot 0x28 / style 0x68963c4c), +0x1d0 icon cell (11)
//   +0x1d4 initialised, +0x1d5 needs recalc, +0x1d6 selection list dirty, +0x1d7 h scrollbar shown,
//   +0x1d8 v scrollbar shown, +0x1d9 drag preview (drop into the top half of the row)
//   +0x1dc background colour (packed RGB; 0xdeadbeef until InternalInit makes it 0xffffffff; GZPaint also fills
//            the canvas with it even though cIGZBuffer::Fill wants a native colour — quirk kept)
//   +0x1e0 row height (16), +0x1e4 widest image, +0x1e8 min/max box (9), +0x1ec icon gutter (3),
//   +0x1f0 / +0x1f4 window gutters (3, 3), +0x1f8 horizontal scroll offset in pixels, +0x1fc scrollbar thickness
//   +0x200 TV_Flag bits (initial 0x2c = centre icons | lines | background)
//   +0x208 the tree canvas (a cIGZBuffer holding every visible row, rebuilt when the content size changes)
//   +0x210 / +0x218 the horizontal / vertical cIGZWinScrollbar (child windows 0x1756bac7 / 0x1756bac6)
//   +0x220 index of the first visible row, +0x224 last visible row, +0x228 number of visible rows,
//   +0x22c rows that fit (rounded), +0x230 rows per page (truncated)
//   +0x234 / +0x238 line colours, +0x23c..+0x248 default text colours [0..3] (0xdeadbeef until InternalInit)
//   +0x24c source rect {0,0,w,h} of the blitted canvas area, +0x25c destination rect in window coordinates
//   +0x270 an embedded cRZString returned by GetNodeText for unknown ids
//   +0x298 std::vector<IconImage> (a cIGZBuffer* plus its source cRZRect; the first four are the default images)
//
// TreeNode (0xc8 bytes in the original):
//   +0x00 id, +0x04 insert-after argument (the sibling id, -2 last child, -3 first child, -4 sorted),
//   +0x08 cRZString text, +0x30 user data, +0x38 font, +0x40..+0x4c four text colours in the order
//   [0x40]=0, [0x44]=2, [0x48]=1, [0x4c]=3 (a quirk: the memory order is not the enum order),
//   +0x50..+0x5c four image indices (-1 none), +0x60 expanded, +0x61 group node (no row of its own),
//   +0x62 selected, +0x63 visible, +0x64 expand box drawn, +0x65 text drawn, +0x66 image drawn,
//   +0x67 flagged for removal, +0x68 / +0x78 / +0x88 the expand box / text / image rectangles drawn last time,
//   +0x98 row index, +0xa0 parent, +0xa8 std::list<TreeNode*> children, +0xc0 child count.
//
// Node insertion: InsertNode(parent, insertAfter, ...). `insertAfter` is -2 (first child), -3 (last child),
// -4 (keep the children sorted by text) or a sibling id (insert after it); a missing sibling appends.
// Expansion is per node; visibility is the computed row range [topRow, topRow + rows per page).
//
// Seams: the graph/system services and the drag window are not recovered, see ITreeViewServices below.
#pragma once
#include <cstdint>
#include <list>
#include <map>
#include <vector>

#include "gz/Buffer.h"
#include "gz/Win.h"
#include "gz/Font.h"
#include "gz/WinScrollbar.h"
#include "rz/cRZString.h"

namespace gz {

class cGZWinTreeView;
class ITreeViewServices;

// RZ::GS() / RZ::WCM() / the drag window factory; the default implementation delegates to WinGraphics(),
// WinCtrlFactory() and WinFontSource().
ITreeViewServices* TreeViewServices();
void SetTreeViewServices(ITreeViewServices* services);

// cIGZWinTreeView (iid 0x3ae8bad2): the tree view's own interface, slot n at 8n (vtable 0x100ac27f0).
class cIGZWinTreeView : public cIGZUnknown {
public:
    // TV_Flag bits (0x200): the two low flag names are described from their use in the original.
    enum TV_Flag : std::uint32_t {
        kTVFlagShowRootRow = 0x1,      // unused by this class (the original keeps it in the enum)
        kTVFlagDrawGroupRow = 0x2,     // a group node also gets a row (otherwise only its children do)
        kTVFlagCentreIcons = 0x4,      // centre (and draw) the node images in the icon column
        kTVFlagLines = 0x8,            // draw the connection lines of the tree
        kTVFlagMultiSelect = 0x10,     // allow more than one selected node
        kTVFlagBackground = 0x20,      // paint the background / the canvas
    };
    enum TV_NodeIconIdx : std::uint32_t { kNodeIconCollapsed = 0, kNodeIconCollapsedSelected = 1, kNodeIconExpanded = 2, kNodeIconExpandedSelected = 3 };
    enum TV_NodeTextColorIdx : std::uint32_t { kNodeTextNormal = 0, kNodeTextNormalShadow = 1, kNodeTextSelected = 2, kNodeTextSelectedShadow = 3 };
    enum TV_DefaultImageHandle : std::uint32_t { kDefaultImageCollapsed = 0, kDefaultImageCollapsedSelected = 1, kDefaultImageExpanded = 2, kDefaultImageExpandedSelected = 3, kDefaultImageCount = 4 };

    // One node as the API exchanges it (the original TV_NodeInfo, 0x58 bytes: +0 cIGZString* text, +8 user data,
    // +0x10 icons[4], +0x20 font, +0x28..+0x34 colours in the order 0,2,1,3, +0x38 expanded / +0x39 group /
    // +0x3a visible / +0x3b selected, +0x40 std::list<int> child ids).
    struct TV_NodeInfo {
        cRZString text;                                             // +0x00
        void* userData = nullptr;                                   // +0x08
        std::int32_t icon[4] = {-1, -1, -1, -1};                    // +0x10
        cIGZFont* font = nullptr;                                   // +0x20
        std::uint32_t textColor[4] = {0, 0xffffffffu, 0xffffffffu, 0};   // +0x28 in the order 0,2,1,3
        bool expanded = false;                                      // +0x38
        bool groupNode = false;                                     // +0x39
        bool visible = false;                                       // +0x3a
        bool selected = false;                                      // +0x3b
        std::list<int> children;                                    // +0x40
    };

    // The InsertNode argument (the original passes a TV_NodeInfo with an 8-byte prefix, so the text is at +8).
    struct TV_NodeInsertInfo {
        int parent = -1;                                            // +0x00 node whose children receive the new node
        int insertAfter = -2;                                       // +0x04 placement (see the header comment)
        cRZString text;                                             // +0x08
        void* userData = nullptr;                                   // +0x10
        std::int32_t icon[4] = {-1, -1, -1, -1};                    // +0x18
        cIGZFont* font = nullptr;                                   // +0x28 (null = the tree's default font)
        std::uint32_t textColor[4] = {0xdeadbeefu, 0xdeadbeefu, 0xdeadbeefu, 0xdeadbeefu};   // +0x30 in the order 0,2,1,3
        bool expanded = false;                                      // +0x40
    };

    virtual ~cIGZWinTreeView() = default;

    virtual void SetTVFlag(TV_Flag flag, bool on) = 0;                                       // [3]  0x10051a4aa
    virtual bool GetTVFlag(TV_Flag flag) = 0;                                                // [4]  0x10051a4e6
    virtual void SetGutters(std::uint32_t x, std::uint32_t y) = 0;                           // [5]  0x10051a4f6
    virtual void GetGutters(std::uint32_t& x, std::uint32_t& y) = 0;                         // [6]  0x10051a52e
    virtual void SetIconGutter(std::uint32_t gutter) = 0;                                    // [7]  0x10051a544
    virtual std::uint32_t GetIconGutter() = 0;                                               // [8]  0x10051a56e
    virtual void SetLineProperties(std::uint32_t color, std::uint32_t thickness) = 0;        // [9]  0x10051a57a
    virtual void GetLineProperties(std::uint32_t& color, std::uint32_t& thickness) const = 0;   // [10] 0x10051a58c
    virtual void SetRowHeight(std::uint32_t height) = 0;                                     // [11] 0x10051a5a2
    virtual std::uint32_t GetRowHeight() = 0;                                                // [12] 0x10051a5cc
    virtual void SetMinMaxBoxSize(std::uint32_t size) = 0;                                   // [13] 0x10051a5d8
    virtual std::uint32_t GetMinMaxBoxSize() = 0;                                            // [14] 0x10051a60e
    virtual void SetBackgroundColor(std::uint32_t native) = 0;                               // [15] 0x10051a61a
    virtual std::uint32_t GetBackgroundColor() = 0;                                          // [16] 0x10051a660
    virtual void SetHighlightColor(std::uint32_t native) = 0;                                // [17] 0x10051a67c
    virtual std::uint32_t GetHighlightColor() = 0;                                           // [18] 0x10051a6b6
    virtual bool AddDefaultImage(TV_DefaultImageHandle handle, cIGZBuffer* image, const cRZRect& area) = 0;   // [19] 0x10051c2e6
    virtual int AddImage(cIGZBuffer* image, const cRZRect& area) = 0;                        // [20] 0x10051c49c
    virtual bool GetImage(int index, cIGZBuffer** image, cRZRect& area) = 0;                 // [21] 0x10051c5b4
    virtual void ClearImageList() = 0;                                                       // [22] 0x10051c60a
    virtual int InsertNode(TV_NodeInsertInfo* info) = 0;                                     // [23] 0x10051a77a
    virtual int InsertNode(int parent, int insertAfter, const cIGZString& text, void* userData) = 0;   // [24] 0x10051ac56
    virtual int InsertNode(int parent, int insertAfter, const cIGZString& text, int icon0, int icon1, int icon2, int icon3) = 0;   // [25] 0x10051ad40
    virtual void TransferNode(int node, int newParent, int insertAfter) = 0;                 // [26] 0x10051ae3a
    virtual bool RemoveNode(int node) = 0;                                                   // [27] 0x10051afda
    virtual void RemoveAllNodes() = 0;                                                       // [28] 0x10051afc6
    virtual bool GetNodeInfo(int node, TV_NodeInfo& info) = 0;                               // [29] 0x10051b202
    virtual bool SetNodeInfo(int node, const TV_NodeInfo& info) = 0;                         // [30] 0x10051b3de
    virtual cIGZString* GetNodeText(int node) = 0;                                           // [31] 0x10051b56a
    virtual bool SetNodeText(int node, const cIGZString& text) = 0;                          // [32] 0x10051b5c2
    virtual void* GetNodeUserData(int node) = 0;                                             // [33] 0x10051b63c
    virtual bool SetNodeUserData(int node, void* data) = 0;                                  // [34] 0x10051b68a
    virtual std::int32_t GetNodeIcon(int node, TV_NodeIconIdx which) = 0;                    // [35] 0x10051b6dc
    virtual bool SetNodeIcon(int node, TV_NodeIconIdx which, int image) = 0;                 // [36] 0x10051b764
    virtual void SetDefaultFont(cIGZFont* font) = 0;                                         // [37] 0x10051b800
    virtual cIGZFont* GetNodeFont(int node) = 0;                                             // [38] 0x10051b846
    virtual bool SetNodeFont(int node, cIGZFont* font) = 0;                                  // [39] 0x10051b894
    virtual std::uint32_t GetDefaultNodeTextColor(TV_NodeTextColorIdx which) const = 0;      // [40] 0x10051b95e
    virtual void SetDefaultNodeTextColor(TV_NodeTextColorIdx which, std::uint32_t native) = 0;   // [41] 0x10051b974
    virtual std::uint32_t GetNodeTextColor(int node, TV_NodeTextColorIdx which) = 0;         // [42] 0x10051b988
    virtual bool SetNodeTextColor(int node, TV_NodeTextColorIdx which, std::uint32_t native) = 0;   // [43] 0x10051ba0c
    virtual bool GetNodeExpanded(int node) = 0;                                              // [44] 0x10051baa8
    virtual bool SetNodeExpanded(int node, bool expanded) = 0;                               // [45] 0x10051bafa
    virtual bool GetNodeVisible(int node) = 0;                                               // [46] 0x10051bbdc
    virtual bool SetNodeVisible(int node) = 0;                                               // [47] 0x10051bc62
    virtual std::int32_t GetNodeChildrenCount(int node) = 0;                                 // [48] 0x10051bdaa
    virtual bool GetNodeChildren(int node, int* ids) = 0;                                    // [49] 0x10051bdfa
    virtual bool GetNodeSelected(int node) = 0;                                              // [50] 0x10051be72
    virtual int GetNodeParent(int node) = 0;                                                 // [51] 0x10051bec4
    virtual int GetNodeInsertAfter(int node) = 0;                                            // [52] 0x10051bf1e
    virtual std::uint32_t GetSelectionSize() = 0;                                            // [53] 0x10051bf6e
    virtual bool GetSelection(int* ids) = 0;                                                 // [54] 0x10051bf86
    virtual int GetSelection() = 0;                                                          // [55] 0x10051bfd4
    virtual bool AddSelection(int node, bool notify) = 0;                                    // [56] 0x10051bff6
    virtual bool RemoveSelection(int node) = 0;                                              // [57] 0x10051c1ac
    virtual bool ClearSelection() = 0;                                                       // [58] 0x10051c25a
    virtual cIGZWin* AsIGZWin() = 0;                                                         // [59] 0x1005198b4
};

constexpr std::uint32_t kGZIID_cIGZWinTreeView = 0x3ae8bad2;
constexpr std::uint32_t kGZCLSID_cGZWinTreeView = 0x3ae8bae1;
constexpr std::uint32_t kTreeViewHScrollId = 0x1756bac7, kTreeViewVScrollId = 0x1756bac6;
constexpr std::uint32_t kTreeViewDefaultImageColorId = 0x08963f6c;
// Messages the tree sends to its notification target (SendMsg slot [145] of cGZWin).
enum GZTreeViewMessage : std::uint32_t {
    kGZMsgTreeViewSelectionChanged = 0x1af5a13a,   // p1 window id
    kGZMsgTreeViewDoubleClick = 0x1af5a13b,        // p1 window id, p2 node id
    kGZMsgTreeViewExpand = 0x1af5a13c,             // p1 window id, p2 node id
    kGZMsgTreeViewCollapse = 0x1af5a13d,
    kGZMsgTreeViewRightClick = 0x1af5a13e,         // p1 window id, p2 node id
    kGZMsgTreeViewCommand = 0x1af5a13f,            // p1 window id, p2 node id
    kGZMsgTreeViewQueryDrag = 0x1af5a140,          // p1 window id, p2 = bool* (the target sets it to allow the drag)
    kGZMsgTreeViewDropped = 0x1af5a141,            // p1 new parent id (or the node id when not dropped above), p2 insert-after id
};
constexpr std::uint32_t kGZMsgTreeViewCommandSource = 0x887113a3;   // param2 of a kGZMsgCommand from a scrollbar

class cGZWinTreeView : public cIGZWinTreeView, public cGZWin {
public:
    // The original's destructor (0x10051f038) clears the child list and releases the text; both are members here,
    // so the compiler-generated destructor does the same work.
    struct GZWinTreeView_TreeNode {
        int id = 0;                                     // +0x00
        int insertAfter = 0;                            // +0x04
        cRZString text;                                 // +0x08
        void* userData = nullptr;                       // +0x30
        cIGZFont* font = nullptr;                       // +0x38
        std::uint32_t textColor[4] = {0, 0, 0, 0};      // +0x40..+0x4c in the order 0,2,1,3
        std::int32_t icon[4] = {-1, -1, -1, -1};        // +0x50..+0x5c
        bool expanded = false;                          // +0x60
        bool groupNode = false;                         // +0x61
        bool selected = false;                          // +0x62
        bool visible = false;                           // +0x63
        bool boxDrawn = false;                          // +0x64
        bool textDrawn = false;                         // +0x65
        bool iconDrawn = false;                         // +0x66
        bool removeFlag = false;                        // +0x67
        cRZRect boxRect;                                // +0x68
        cRZRect textRect;                               // +0x78
        cRZRect imageRect;                              // +0x88
        std::uint32_t rowIndex = 0;                     // +0x98
        GZWinTreeView_TreeNode* parent = nullptr;       // +0xa0
        std::list<GZWinTreeView_TreeNode*> children;    // +0xa8
        std::int32_t childCount = 0;                    // +0xc0
    };
    struct IconImage {
        cIGZBuffer* image = nullptr;                    // +0x00 (owned reference)
        cRZRect area;                                   // +0x08
    };

    explicit cGZWinTreeView(cIGZWinMgr* mgr);
    ~cGZWinTreeView() override;

    bool QueryInterface(std::uint32_t iid, void** out) override;      // 0x1005198be
    std::uint32_t AddRef() override { return cGZWin::AddRef(); }      // 0x100519820
    std::uint32_t Release() override { return cGZWin::Release(); }    // 0x10051983c
    cIGZWin* AsIGZWin() override { return static_cast<cGZWin*>(this); }   // 0x1005198b4

    bool Init() override;                                             // 0x100519906
    bool Shutdown() override;                                         // 0x10051a2e4
    void InternalInit();                                              // @ 0x10051993a
    void RecalculateAll();                                            // @ 0x100519ac4
    void InitializeImageList();                                       // @ 0x100519d70
    void CreateRootNode();                                            // @ 0x10051a192
    void SetArea(int l, int t, int r, int b) override;                // @ 0x10051de4e
    bool GZPaint() override;                                          // @ 0x10051cf06
    bool DoMessage(cGZMessage& message) override;                     // @ 0x10051de8a
    bool GZOnMouseDownL(int x, int y, std::uint32_t flags) override;  // @ 0x10051df34
    bool GZOnMouseDownR(int x, int y, std::uint32_t flags) override;  // @ 0x10051e4a0
    bool GZOnMouseMove(int x, int y, std::uint32_t flags) override;   // @ 0x10051e5c2
    bool GZOnKeyDown(std::uint32_t key, std::uint32_t modifiers) override;   // @ 0x10051eb22
    bool GZOnKeyUp(std::uint32_t key, std::uint32_t modifiers) override;     // @ 0x10051edaa
    bool GZOnMouseWheel(int x, int y, std::uint32_t flags, int delta) override;   // @ 0x10051edc2

    // Interface methods.
    void SetTVFlag(TV_Flag flag, bool on) override;                   // 0x10051a4aa
    bool GetTVFlag(TV_Flag flag) override { return (tvFlags_ & flag) != 0; }   // 0x10051a4e6
    void SetGutters(std::uint32_t x, std::uint32_t y) override;       // 0x10051a4f6
    void GetGutters(std::uint32_t& x, std::uint32_t& y) override;     // 0x10051a52e
    void SetIconGutter(std::uint32_t gutter) override;                // 0x10051a544
    std::uint32_t GetIconGutter() override { return iconGutter_; }    // 0x10051a56e
    void SetLineProperties(std::uint32_t color, std::uint32_t thickness) override;   // 0x10051a57a
    void GetLineProperties(std::uint32_t& color, std::uint32_t& thickness) const override;   // 0x10051a58c
    void SetRowHeight(std::uint32_t height) override;                 // 0x10051a5a2
    std::uint32_t GetRowHeight() override { return rowHeight_; }      // 0x10051a5cc
    void SetMinMaxBoxSize(std::uint32_t size) override;               // 0x10051a5d8
    std::uint32_t GetMinMaxBoxSize() override { return minMaxBoxSize_; }   // 0x10051a60e
    void SetBackgroundColor(std::uint32_t native) override;           // 0x10051a61a
    std::uint32_t GetBackgroundColor() override;                      // 0x10051a660
    void SetHighlightColor(std::uint32_t native) override;            // 0x10051a67c
    std::uint32_t GetHighlightColor() override { return defaultTextColor_[kNodeTextSelectedShadow]; }   // 0x10051a6b6
    bool AddDefaultImage(TV_DefaultImageHandle handle, cIGZBuffer* image, const cRZRect& area) override;   // 0x10051c2e6
    int AddImage(cIGZBuffer* image, const cRZRect& area) override;    // 0x10051c49c
    bool GetImage(int index, cIGZBuffer** image, cRZRect& area) override;   // 0x10051c5b4
    void ClearImageList() override;                                   // 0x10051c60a
    int InsertNode(TV_NodeInsertInfo* info) override;                 // 0x10051a77a
    int InsertNode(int parent, int insertAfter, const cIGZString& text, void* userData) override;   // 0x10051ac56
    int InsertNode(int parent, int insertAfter, const cIGZString& text, int icon0, int icon1, int icon2, int icon3) override;   // 0x10051ad40
    void TransferNode(int node, int newParent, int insertAfter) override;   // 0x10051ae3a
    bool RemoveNode(int node) override;                               // 0x10051afda
    void RemoveAllNodes() override;                                   // 0x10051afc6
    void FlagNodeForRemovalRecursively(GZWinTreeView_TreeNode* node);  // @ 0x10051b15c
    bool GetNodeInfo(int node, TV_NodeInfo& info) override;           // 0x10051b202
    bool SetNodeInfo(int node, const TV_NodeInfo& info) override;     // 0x10051b3de
    cIGZString* GetNodeText(int node) override;                       // 0x10051b56a
    bool SetNodeText(int node, const cIGZString& text) override;      // 0x10051b5c2
    void* GetNodeUserData(int node) override;                         // 0x10051b63c
    bool SetNodeUserData(int node, void* data) override;              // 0x10051b68a
    std::int32_t GetNodeIcon(int node, TV_NodeIconIdx which) override;   // 0x10051b6dc
    bool SetNodeIcon(int node, TV_NodeIconIdx which, int image) override;   // 0x10051b764
    void SetDefaultFont(cIGZFont* font) override;                     // 0x10051b800
    cIGZFont* GetNodeFont(int node) override;                         // 0x10051b846
    bool SetNodeFont(int node, cIGZFont* font) override;              // 0x10051b894
    std::uint32_t GetDefaultNodeTextColor(TV_NodeTextColorIdx which) const override;   // 0x10051b95e
    void SetDefaultNodeTextColor(TV_NodeTextColorIdx which, std::uint32_t native) override;   // 0x10051b974
    std::uint32_t GetNodeTextColor(int node, TV_NodeTextColorIdx which) override;   // 0x10051b988
    bool SetNodeTextColor(int node, TV_NodeTextColorIdx which, std::uint32_t native) override;   // 0x10051ba0c
    bool GetNodeExpanded(int node) override;                          // 0x10051baa8
    bool SetNodeExpanded(int node, bool expanded) override;           // 0x10051bafa
    bool GetNodeVisible(int node) override;                           // 0x10051bbdc
    bool SetNodeVisible(int node) override;                           // 0x10051bc62
    std::int32_t GetNodeChildrenCount(int node) override;             // 0x10051bdaa
    bool GetNodeChildren(int node, int* ids) override;                // 0x10051bdfa
    bool GetNodeSelected(int node) override;                          // 0x10051be72
    int GetNodeParent(int node) override;                             // 0x10051bec4
    int GetNodeInsertAfter(int node) override;                        // 0x10051bf1e
    std::uint32_t GetSelectionSize() override;                        // 0x10051bf6e
    bool GetSelection(int* ids) override;                             // 0x10051bf86
    int GetSelection() override;                                      // 0x10051bfd4
    bool AddSelection(int node, bool notify) override;                // 0x10051bff6
    bool RemoveSelection(int node) override;                          // 0x10051c1ac
    bool ClearSelection() override;                                   // 0x10051c25a

    // Non-virtual helpers of the original.
    void CreateHorizontalScrollbar();                                 // @ 0x10051c694
    void RemoveHorizontalScrollbar();                                 // @ 0x10051c790
    void CreateVerticalScrollbar();                                   // @ 0x10051c7ea
    void RemoveVerticalScrollbar();                                   // @ 0x10051c8e6
    void LayoutScrollbars();                                          // @ 0x10051c940
    void CheckForScrollbars();                                        // @ 0x10051ca18
    void UpdateScrollbarValues();                                     // @ 0x10051cb56
    void FindTreeInfo(GZWinTreeView_TreeNode* node, std::uint32_t& row, std::uint32_t indent, std::uint32_t& maxWidth);   // @ 0x10051cd08
    void UpdateSelectionList();                                       // @ 0x10051d0a6
    void DrawNode(GZWinTreeView_TreeNode* node, std::uint32_t& row, std::uint32_t indent, std::vector<bool>& lines, bool lastChild);   // @ 0x10051d142
    void DrawItem(cIGZBuffer* target, GZWinTreeView_TreeNode* node, int x, int y);   // @ 0x10051d9b0
    void UpdateTreeNodesOnExpandCollapse(GZWinTreeView_TreeNode* node, bool keepSelection, bool topLevel);   // @ 0x10051ddf0
    GZWinTreeView_TreeNode* GetNodeAtCursorPosition(int x, int y);   // @ 0x10051e432
    void NotifyDrag(std::vector<GZWinTreeView_TreeNode*>* dragList, int x, int y, bool unused);   // @ 0x10051ee1a

    // Test/diagnostics access to the internal state (not part of the original interface).
    GZWinTreeView_TreeNode* Root() const { return root_; }
    GZWinTreeView_TreeNode* FindNode(int id) const;
    const std::vector<GZWinTreeView_TreeNode*>& VisibleNodes() const { return visibleNodes_; }
    const std::vector<GZWinTreeView_TreeNode*>& SelectionList() const { return selection_; }
    cIGZBuffer* Canvas() const { return canvas_; }
    cIGZWinScrollbar* HScrollbar() const { return hScrollbar_; }
    cIGZWinScrollbar* VScrollbar() const { return vScrollbar_; }
    int TopRow() const { return topRow_; }
    int BottomRow() const { return bottomRow_; }
    int RowsPerPage() const { return rowsPerPage_; }
    int HScrollOffset() const { return hScrollOffset_; }
    int ScrollbarThickness() const { return scrollbarThickness_; }
    std::uint32_t VisibleRowCount() const { return visibleRowCount_; }
    bool NeedsRecalc() const { return needsRecalc_; }
    const std::vector<IconImage>& Images() const { return images_; }
    const cRZRect& SourceRect() const { return sourceRect_; }
    const cRZRect& DestRect() const { return destRect_; }
    // The canvas fill colour is the packed background (see the quirk in the header comment).
    std::uint32_t BackgroundPacked() const { return backgroundColor_; }
    // Drag preview state (0x1d9), also set by NotifyDrag.
    bool DragPreview() const { return dragPreview_; }
    void SetDragPreview(bool on) { dragPreview_ = on; }
    cIGZWin* NotificationTarget() const { return notify_; }

    // The original's statics (GZOnMouseDownL's function-local timer and last click / node / area).
    static void ResetClickState();

private:
    // The map lookup the original inlines in nearly every accessor (lower_bound + key comparison).
    cIGZFont* FontOf(GZWinTreeView_TreeNode* node) const { return node ? node->font : nullptr; }
    static int ColorSlot(TV_NodeTextColorIdx which) { return which == kNodeTextNormalShadow ? 2 : which == kNodeTextSelected ? 1 : which; }
    std::uint32_t& NodeColor(GZWinTreeView_TreeNode* node, TV_NodeTextColorIdx which) { return node->textColor[ColorSlot(which)]; }
    bool SendTreeMessage(std::uint32_t type, std::uint64_t p1, std::uint64_t p2);

    int nextNodeId_ = 0;                              // +0x158
    std::map<int, GZWinTreeView_TreeNode*> nodes_;    // +0x160
    std::vector<GZWinTreeView_TreeNode*> unusedRows_; // +0x178 (never used by the original either)
    std::vector<GZWinTreeView_TreeNode*> visibleNodes_;   // +0x190
    std::vector<GZWinTreeView_TreeNode*> selection_;  // +0x1a8
    GZWinTreeView_TreeNode* root_ = nullptr;          // +0x1c0
    cIGZFont* defaultFont_ = nullptr;                 // +0x1c8
    std::uint32_t iconSize_ = 11;                     // +0x1d0
    bool initialized_ = false;                        // +0x1d4
    bool needsRecalc_ = false;                        // +0x1d5
    bool selectionDirty_ = false;                     // +0x1d6
    bool hScrollVisible_ = false;                     // +0x1d7
    bool vScrollVisible_ = false;                     // +0x1d8
    bool dragPreview_ = false;                        // +0x1d9
    std::uint32_t backgroundColor_ = 0xdeadbeefu;     // +0x1dc (packed)
    std::uint32_t rowHeight_ = 16;                    // +0x1e0
    std::uint32_t maxIconWidth_ = 0;                  // +0x1e4
    std::uint32_t minMaxBoxSize_ = 9;                 // +0x1e8
    std::uint32_t iconGutter_ = 3;                    // +0x1ec
    std::uint32_t gutterX_ = 3;                       // +0x1f0
    std::uint32_t gutterY_ = 3;                       // +0x1f4
    int hScrollOffset_ = 0;                           // +0x1f8
    int scrollbarThickness_ = 0;                      // +0x1fc
    std::uint32_t tvFlags_ = 0x2c;                    // +0x200
    cIGZBuffer* canvas_ = nullptr;                    // +0x208
    cIGZWinScrollbar* hScrollbar_ = nullptr;          // +0x210
    cIGZWinScrollbar* vScrollbar_ = nullptr;          // +0x218
    int topRow_ = 0;                                  // +0x220
    int bottomRow_ = -1;                              // +0x224
    std::uint32_t visibleRowCount_ = 0;               // +0x228
    int rowsThatFit_ = 0;                             // +0x22c
    int rowsPerPage_ = 0;                             // +0x230
    std::uint32_t lineColor_ = 0;                     // +0x234
    std::uint32_t lineThickness_ = 0;                 // +0x238
    std::uint32_t defaultTextColor_[4] = {0xdeadbeefu, 0xdeadbeefu, 0xdeadbeefu, 0xdeadbeefu};   // +0x23c..+0x248 (logical order)
    cRZRect sourceRect_;                              // +0x24c
    cRZRect destRect_;                                // +0x25c
    cRZString emptyText_;                             // +0x270
    std::vector<IconImage> images_;                   // +0x298
};

// The services the control asks the engine for, plus the two pieces that go through this seam.
// The default object delegates to WinGraphics(), WinCtrlFactory() and WinFontSource(); tests replace it.
class ITreeViewServices {
public:
    virtual ~ITreeViewServices() = default;
    // RZ::GS() vslot 0x20: a new cIGZBuffer with one reference (the original asks the graphic system).
    virtual cIGZBuffer* CreateBuffer() = 0;
    // RZ::WCM() vslot 0x28: the font registered under style 0x68963c4c.
    virtual cIGZFont* DefaultFont() = 0;   // the full font interface (IWinFont::AsFont())
    // RZ::WCM() vslot 0x88: a scrollbar child window (direction 1 = horizontal, 2 = vertical).
    virtual cIGZWinScrollbar* CreateScrollbar(std::uint32_t id, int direction, int low, int high, int value) = 0;
    // RZ::WCM() vslot 0xf8: a stock colour.
    virtual std::uint32_t DefaultColor(std::uint32_t id) = 0;
    // RZ::WCM() vslot 0x140: the double click time in milliseconds.
    virtual std::uint32_t DoubleClickMilliseconds() = 0;
    // The free function DrawTriangle @0x1002b4207 (not recovered; drawn here with a scanline fill).
    virtual void DrawTriangle(cIGZBuffer* target, int x1, int y1, int x2, int y2, int x3, int y3, std::uint32_t color) = 0;
    // cGZWinTreeViewDrag (@0x10051f0ba..0x10051f348, not recovered) run through cIGZWinMgr::RunPopup ([41] @0x148).
    // `nodes` is the dragged selection, `image` the row image built by GZOnMouseMove. Returns whether the user
    // finished a drop; `outDragWindow` is the temporary window the caller must not free (the window manager owns it).
    virtual bool RunNodeDrag(cGZWinTreeView& tree, std::vector<cGZWinTreeView::GZWinTreeView_TreeNode*>& nodes,
                             cIGZBuffer* image, cIGZWin*& outDragWindow) {
        (void)tree; (void)nodes; (void)image; outDragWindow = nullptr; return false;
    }
};

}  // namespace gz
