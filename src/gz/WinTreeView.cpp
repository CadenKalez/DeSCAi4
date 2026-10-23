// cGZWinTreeView implementation — every function here corresponds to one address of the original (noted on the
// definition). The tree model is the original's: a std::map id -> node, nodes own a std::list of children, and the
// visible row list is rebuilt by RecalculateAll from the root (FindTreeInfo) and drawn into an offscreen canvas.
#include "gz/WinTreeView.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <vector>

#include "gz/DrawContext.h"
#include "gz/Graph.h"
#include "gz/WinCtrlFactory.h"
#include "gz/WinFont.h"
#include "rz/cRZKeyboard.h"
#include "rz/cRZTimer.h"

namespace gz {

namespace {

// The original's draw locks (cIGZBuffer::eLockFlags values it passes): 0x8001 fill, 0x8004 line, 0x8008 outline,
// 0x8010 image blit.
constexpr std::uint32_t kLockFill = 0x8001, kLockLine = 0x8004, kLockBox = 0x8008, kLockImage = 0x8010;

int RoundNearest(float value) { return static_cast<int>(std::nearbyint(value)); }

cRZRect UnionRect(const cRZRect& a, const cRZRect& b) {
    cRZRect r;
    r.left = std::min(a.left, b.left);
    r.top = std::min(a.top, b.top);
    r.right = std::max(a.right, b.right);
    r.bottom = std::max(a.bottom, b.bottom);
    return r;
}

bool EmptyRect(const cRZRect& r) { return r.left >= r.right || r.top >= r.bottom; }

// The four statics GZOnMouseDownL keeps in function-local statics in the original: the click timer (cRZTimer with
// millisecond units constructed on first use) and the last click time / node / hit rectangle.
cRZTimer g_clickTimer(cRZTimer::kMilliseconds);
std::uint32_t g_lastClickTime = 0;
int g_lastNodeSelected = 0;
cRZRect g_lastAreaClicked;

// The default services: the recovered engine seams (RZ::GS(), RZ::WCM()).
class DefaultTreeViewServices final : public ITreeViewServices {
public:
    cIGZBuffer* CreateBuffer() override {
        auto* buffer = new cGZBuffer();
        buffer->AddRef();
        return buffer;
    }
    cIGZFont* DefaultFont() override {
        // RZ::WCM() vslot 0x28 (style 0x68963c4c); the controls' font view exposes the full cIGZFont interface.
        IWinFontSource* source = WinFontSource();
        IWinFont* font = source ? source->Font(kWinDefaultFontStyle) : nullptr;
        return font ? font->AsFont() : nullptr;
    }
    cIGZWinScrollbar* CreateScrollbar(std::uint32_t id, int direction, int low, int high, int value) override {
        IWinCtrlFactory* factory = WinCtrlFactory();   // RZ::WCM() vslot 0x88
        return factory ? factory->CreateScrollbar(id, direction, low, high, value) : nullptr;
    }
    std::uint32_t DefaultColor(std::uint32_t id) override {
        IWinCtrlFactory* factory = WinCtrlFactory();   // RZ::WCM() vslot 0xf8
        return factory ? factory->DefaultColor(static_cast<int>(id)) : 0;
    }
    std::uint32_t DoubleClickMilliseconds() override {
        IWinCtrlFactory* factory = WinCtrlFactory();   // RZ::WCM() vslot 0x140
        return factory ? factory->DoubleClickMilliseconds() : 0;
    }
    // Not recovered (DrawTriangle @0x1002b4207): a solid scanline triangle, enough for the expand / collapse
    // images. The original takes six floats and a native colour.
    void DrawTriangle(cIGZBuffer* target, int x1, int y1, int x2, int y2, int x3, int y3, std::uint32_t color) override {
        if (!target) return;
        const int minY = std::min({y1, y2, y3});
        const int maxY = std::max({y1, y2, y3});
        const float fx[3] = {static_cast<float>(x1), static_cast<float>(x2), static_cast<float>(x3)};
        const float fy[3] = {static_cast<float>(y1), static_cast<float>(y2), static_cast<float>(y3)};
        for (int y = minY; y <= maxY; ++y) {
            float left = 1e30f, right = -1e30f;
            for (int e = 0; e < 3; ++e) {
                const int a = e, b = (e + 1) % 3;
                if ((fy[a] <= static_cast<float>(y) && fy[b] >= static_cast<float>(y)) ||
                    (fy[b] <= static_cast<float>(y) && fy[a] >= static_cast<float>(y))) {
                    const float dy = fy[b] - fy[a];
                    const float t = dy == 0.0f ? 0.0f : (static_cast<float>(y) - fy[a]) / dy;
                    const float x = fx[a] + t * (fx[b] - fx[a]);
                    left = std::min(left, x);
                    right = std::max(right, x);
                }
            }
            if (left <= right) {
                const int l = RoundNearest(left), r = RoundNearest(right);
                target->Fill(color, l, y, r + 1, y + 1, nullptr);
            }
        }
    }
};

DefaultTreeViewServices g_defaultServices;
ITreeViewServices* g_services = nullptr;

}  // namespace

ITreeViewServices* TreeViewServices() { return g_services ? g_services : &g_defaultServices; }
void SetTreeViewServices(ITreeViewServices* services) { g_services = services; }

// ---- construction -------------------------------------------------------------------------------------------------

cGZWinTreeView::cGZWinTreeView(cIGZWinMgr* mgr)   // @ 0x100519550 (the C1/C2 pair is 0x1005196fc in the binary)
    : cGZWin(mgr) {
    // The original sets the secondary vtable of cGZWin and then initialises the derived fields in this order.
    backgroundColor_ = 0xdeadbeefu;      // +0x1dc, resolved by InternalInit
    rowHeight_ = 16;                     // +0x1e0
    maxIconWidth_ = 0;                   // +0x1e4
    minMaxBoxSize_ = 9;                  // +0x1e8
    iconGutter_ = 3;                     // +0x1ec
    gutterX_ = 3;                        // +0x1f0
    gutterY_ = 3;                        // +0x1f4
    hScrollOffset_ = 0;                  // +0x1f8
    scrollbarThickness_ = 0;             // +0x1fc
    tvFlags_ = 0x2c;                     // +0x200
    topRow_ = 0;                         // +0x220
    bottomRow_ = -1;                     // +0x224
    visibleRowCount_ = 0;                // +0x228
    rowsThatFit_ = 0;                    // +0x22c
    rowsPerPage_ = 0;                    // +0x230
    for (int i = 0; i < 4; ++i) defaultTextColor_[i] = 0xdeadbeefu;   // +0x23c..+0x248
    emptyText_.AssignFromCString("");    // +0x270
}

cGZWinTreeView::~cGZWinTreeView() {   // @ 0x100519706 (D2/D1/D0 variants 0x1005197cc / 0x1005197e4)
    // The original destructor only frees the storage of the four vectors and the node map; the tree nodes are freed
    // by Shutdown (a tree destroyed without Shutdown leaks them, as in the original).
    nodes_.clear();
    visibleNodes_.clear();
    selection_.clear();
    unusedRows_.clear();
    images_.clear();
}

bool cGZWinTreeView::QueryInterface(std::uint32_t iid, void** out) {   // @ 0x1005198be
    if (iid == kGZIID_cIGZWinTreeView) {
        if (!out) return false;
        *out = static_cast<cIGZWinTreeView*>(this);
        AddRef();
        return true;
    }
    return cGZWin::QueryInterface(iid, out);
}

// ---- initialisation -----------------------------------------------------------------------------------------------

bool cGZWinTreeView::Init() {   // @ 0x100519906
    cGZWin::Init();
    if (!initialized_) InternalInit();
    RecalculateAll();
    return true;
}

void cGZWinTreeView::InternalInit() {   // @ 0x10051993a
    if (initialized_) return;
    initialized_ = true;
    if (backgroundColor_ == 0xdeadbeefu) backgroundColor_ = 0xffffffffu;
    static const std::uint8_t kDefaultColors[4][3] = {{0, 0, 0}, {0xff, 0xff, 0xff}, {0xff, 0xff, 0xff}, {0, 0, 0}};
    for (int i = 0; i < 4; ++i)
        if (defaultTextColor_[i] == 0xdeadbeefu)
            defaultTextColor_[i] = MakeFillColor(kDefaultColors[i][0], kDefaultColors[i][1], kDefaultColors[i][2]);
    if (!defaultFont_) {
        // RZ::WCM() vslot 0x28 for style 0x68963c4c, taken with a reference.
        defaultFont_ = TreeViewServices()->DefaultFont();
        if (defaultFont_) defaultFont_->AddRef();
    }
    InitializeImageList();
    if (!root_) CreateRootNode();
    if (!canvas_) {
        // RZ::GS() vslot 0x20: a 1x1 display buffer, resized by RecalculateAll.
        canvas_ = TreeViewServices()->CreateBuffer();
        if (canvas_) {
            cGZBufferColorType type;
            type.type = kBufferColorDisplay;
            type.bitsPerPixel = 0;
            if (!canvas_->Init(1, 1, type)) {
                canvas_->Release();
                canvas_ = nullptr;
            }
        }
    }
}

void cGZWinTreeView::InitializeImageList() {   // @ 0x100519d70
    ITreeViewServices* services = TreeViewServices();
    if (!services) return;
    const std::uint32_t fillCollapsed = MakeFillColor(0, 0, 0x96);      // cGZWin vslot 0x360
    const std::uint32_t fillExpanded = MakeFillColor(0xdc, 0, 0);
    const std::uint32_t keyColor = services->DefaultColor(kTreeViewDefaultImageColorId);   // RZ::WCM() vslot 0xf8
    cIGZBuffer* image = nullptr;
    auto initImage = [&](cIGZBuffer*& out) {
        out = services->CreateBuffer();
        if (!out) return false;
        cGZBufferColorType type;
        type.type = kBufferColorDisplay;
        type.bitsPerPixel = 0;
        if (!out->Init(iconSize_, iconSize_, type)) return false;
        return true;
    };
    auto destroyImage = [](cIGZBuffer*& buffer) {
        if (buffer) {
            buffer->Release();
            buffer = nullptr;
        }
    };
    const cRZRect noArea{0, 0, 0, 0};
    if (images_.empty() || images_[0].image == nullptr) {
        if (initImage(image)) {
            if (image->Lock(static_cast<cIGZBuffer::eLockFlags>(kLockFill))) {
                image->Fill(fillCollapsed, nullptr);
                image->Unlock(static_cast<cIGZBuffer::eLockFlags>(kLockFill));
            }
            AddDefaultImage(kDefaultImageCollapsed, image, noArea);
        }
        destroyImage(image);
    }
    if (images_.size() < 2 || images_[1].image == nullptr) {
        if (initImage(image)) {
            if (image->Lock(static_cast<cIGZBuffer::eLockFlags>(kLockFill))) {
                image->Fill(fillExpanded, nullptr);
                image->Unlock(static_cast<cIGZBuffer::eLockFlags>(kLockFill));
            }
            AddDefaultImage(kDefaultImageCollapsedSelected, image, noArea);
        }
        destroyImage(image);
    }
    if (images_.size() < 3 || images_[2].image == nullptr) {
        if (initImage(image)) {
            image->SetTransparency(keyColor);
            if (image->Lock(static_cast<cIGZBuffer::eLockFlags>(kLockFill))) {
                image->Fill(keyColor, nullptr);
                image->Unlock(static_cast<cIGZBuffer::eLockFlags>(kLockFill));
            }
            const float size = static_cast<float>(iconSize_);
            services->DrawTriangle(image, 0, 0, static_cast<int>(size * 0.5f), iconSize_, iconSize_, 0, fillCollapsed);
            AddDefaultImage(kDefaultImageExpanded, image, noArea);
        }
        destroyImage(image);
    }
    if (images_.size() < 4 || images_[3].image == nullptr) {
        if (initImage(image)) {
            image->SetTransparency(keyColor);
            if (image->Lock(static_cast<cIGZBuffer::eLockFlags>(kLockFill))) {
                image->Fill(keyColor, nullptr);
                image->Unlock(static_cast<cIGZBuffer::eLockFlags>(kLockFill));
            }
            const float size = static_cast<float>(iconSize_);
            services->DrawTriangle(image, 0, 0, static_cast<int>(size * 0.5f), iconSize_, iconSize_, 0, fillExpanded);
            AddDefaultImage(kDefaultImageExpandedSelected, image, noArea);
        }
        destroyImage(image);
    }
}

void cGZWinTreeView::CreateRootNode() {   // @ 0x10051a192
    if (root_) return;
    root_ = new GZWinTreeView_TreeNode();
    root_->id = nextNodeId_++;            // 0: the root is the first node of a fresh view
    root_->insertAfter = -3;
    root_->font = defaultFont_;
    root_->textColor[0] = defaultTextColor_[kNodeTextNormal];
    root_->textColor[1] = defaultTextColor_[kNodeTextSelected];
    root_->textColor[2] = defaultTextColor_[kNodeTextNormalShadow];
    root_->textColor[3] = defaultTextColor_[kNodeTextSelectedShadow];
    root_->icon[0] = kDefaultImageCollapsed;
    root_->icon[1] = kDefaultImageCollapsedSelected;
    root_->icon[2] = kDefaultImageExpanded;
    root_->icon[3] = kDefaultImageExpandedSelected;
    root_->expanded = true;
    root_->groupNode = true;
    nodes_[root_->id] = root_;
}

bool cGZWinTreeView::Shutdown() {   // @ 0x10051a2e4
    selection_.clear();      // +0x1b0 = +0x1a8
    visibleNodes_.clear();   // +0x198 = +0x190
    for (auto& entry : nodes_) delete entry.second;
    nodes_.clear();
    for (IconImage& image : images_)
        if (image.image) image.image->Release();
    images_.clear();
    if (defaultFont_) {
        defaultFont_->Release();
        defaultFont_ = nullptr;
    }
    if (canvas_) {
        canvas_->Release();
        canvas_ = nullptr;
    }
    if (hScrollbar_) {
        hScrollbar_->Release();
        hScrollbar_ = nullptr;
    }
    if (vScrollbar_) {
        vScrollbar_->Release();
        vScrollbar_ = nullptr;
    }
    root_ = nullptr;   // the original leaves +0x1c0 dangling here; reset so a second Init cannot use freed nodes
    cGZWin::Shutdown();
    return true;
}

// ---- metrics ------------------------------------------------------------------------------------------------------

void cGZWinTreeView::SetTVFlag(TV_Flag flag, bool on) {   // @ 0x10051a4aa
    const std::uint32_t before = tvFlags_;
    if (!on) {
        if ((before & flag) == 0) return;
        tvFlags_ &= ~static_cast<std::uint32_t>(flag);
    } else {
        if ((before & flag) != 0) return;
        tvFlags_ |= flag;
    }
    needsRecalc_ = true;
    InvalidateSelfAndParents();
}

void cGZWinTreeView::SetGutters(std::uint32_t x, std::uint32_t y) {   // @ 0x10051a4f6
    if (gutterX_ == x && gutterY_ == y) return;
    gutterX_ = x;
    gutterY_ = y;
    needsRecalc_ = true;
    InvalidateSelfAndParents();
}

void cGZWinTreeView::GetGutters(std::uint32_t& x, std::uint32_t& y) {   // @ 0x10051a52e
    x = gutterX_;
    y = gutterY_;
}

void cGZWinTreeView::SetIconGutter(std::uint32_t gutter) {   // @ 0x10051a544
    if (iconGutter_ == gutter) return;
    iconGutter_ = gutter;
    needsRecalc_ = true;
    InvalidateSelfAndParents();
}

void cGZWinTreeView::SetLineProperties(std::uint32_t color, std::uint32_t thickness) {   // @ 0x10051a57a
    lineColor_ = color;
    lineThickness_ = thickness;
}

void cGZWinTreeView::GetLineProperties(std::uint32_t& color, std::uint32_t& thickness) const {   // @ 0x10051a58c
    color = lineColor_;
    thickness = lineThickness_;
}

void cGZWinTreeView::SetRowHeight(std::uint32_t height) {   // @ 0x10051a5a2
    if (rowHeight_ == height) return;
    rowHeight_ = height;
    needsRecalc_ = true;
    InvalidateSelfAndParents();
}

void cGZWinTreeView::SetMinMaxBoxSize(std::uint32_t size) {   // @ 0x10051a5d8
    if (iconSize_ < size) size = iconSize_;
    if (minMaxBoxSize_ == size) return;
    minMaxBoxSize_ = size;
    needsRecalc_ = true;
    InvalidateSelfAndParents();
}

void cGZWinTreeView::SetBackgroundColor(std::uint32_t native) {   // @ 0x10051a61a
    const std::uint32_t packed = ConvertNativeToPackedRGB(native);   // cGZWin vslot 0x400
    if (backgroundColor_ == packed) return;
    backgroundColor_ = packed;
    needsRecalc_ = true;
    InvalidateSelfAndParents();
}

std::uint32_t cGZWinTreeView::GetBackgroundColor() {   // @ 0x10051a660
    return ConvertPackedRGBToNative(backgroundColor_);   // cGZWin vslot 0x3f8
}

void cGZWinTreeView::SetHighlightColor(std::uint32_t native) {   // @ 0x10051a67c
    // +0x248 is shared by the highlight colour and default text colour 3 (a quirk of the original).
    if (defaultTextColor_[kNodeTextSelectedShadow] == native) return;
    defaultTextColor_[kNodeTextSelectedShadow] = native;
    if (selection_.empty()) return;   // the original only repaints when something is selected
    needsRecalc_ = true;
    InvalidateSelfAndParents();
}

// ---- image list ---------------------------------------------------------------------------------------------------

bool cGZWinTreeView::AddDefaultImage(TV_DefaultImageHandle handle, cIGZBuffer* image, const cRZRect& area) {   // @ 0x10051c2e6
    if (static_cast<std::uint32_t>(handle) > 3) return false;
    const std::size_t index = static_cast<std::size_t>(handle);
    if (images_.size() <= index) images_.resize(index + 1);
    if (image) image->AddRef();
    if (images_[index].image) images_[index].image->Release();
    images_[index].image = image;
    images_[index].area = area;
    if (image) {
        cRZRect& stored = images_[index].area;
        if (EmptyRect(stored)) {   // a degenerate area means "the whole buffer"
            stored.left = 0;
            stored.top = 0;
            stored.right = static_cast<int>(image->Width());
            stored.bottom = static_cast<int>(image->Height());
        }
        const std::uint32_t width = static_cast<std::uint32_t>(stored.right - stored.left);
        if (maxIconWidth_ < width) maxIconWidth_ = width;
        const std::uint32_t height = static_cast<std::uint32_t>(stored.bottom - stored.top);
        if (rowHeight_ < height) rowHeight_ = height + 2;
    }
    return true;
}

int cGZWinTreeView::AddImage(cIGZBuffer* image, const cRZRect& area) {   // @ 0x10051c49c
    if (!image) return -1;
    image->AddRef();
    cRZRect stored = area;
    if (EmptyRect(stored)) {
        stored.left = 0;
        stored.top = 0;
        stored.right = static_cast<int>(image->Width());
        stored.bottom = static_cast<int>(image->Height());
    }
    const std::uint32_t width = static_cast<std::uint32_t>(stored.right - stored.left);
    if (maxIconWidth_ < width) maxIconWidth_ = width;
    const std::uint32_t height = static_cast<std::uint32_t>(stored.bottom - stored.top);
    if (rowHeight_ < height) rowHeight_ = height + 2;
    IconImage entry;
    entry.image = image;
    entry.area = stored;
    images_.push_back(entry);
    return static_cast<int>(images_.size()) - 1;
}

bool cGZWinTreeView::GetImage(int index, cIGZBuffer** image, cRZRect& area) {   // @ 0x10051c5b4
    if (index < 0 || static_cast<std::size_t>(index) >= images_.size()) return false;
    const IconImage& entry = images_[static_cast<std::size_t>(index)];
    *image = entry.image;
    area = entry.area;
    return true;
}

void cGZWinTreeView::ClearImageList() {   // @ 0x10051c60a
    if (images_.size() <= kDefaultImageCount) return;
    for (std::size_t i = kDefaultImageCount; i < images_.size(); ++i)
        if (images_[i].image) images_[i].image->Release();
    images_.resize(kDefaultImageCount);
}

// ---- tree model ---------------------------------------------------------------------------------------------------

cGZWinTreeView::GZWinTreeView_TreeNode* cGZWinTreeView::FindNode(int id) const {
    auto it = nodes_.find(id);
    return it == nodes_.end() ? nullptr : it->second;
}

int cGZWinTreeView::InsertNode(TV_NodeInsertInfo* info) {   // @ 0x10051a77a
    if (!info) return -1;
    if (!initialized_) InternalInit();
    GZWinTreeView_TreeNode* parent = FindNode(info->parent);   // the original requires a non-empty map before this
    if (!parent) return -1;
    auto* node = new GZWinTreeView_TreeNode();
    node->id = nextNodeId_++;
    node->insertAfter = info->insertAfter;
    node->text.Copy(info->text);
    node->userData = info->userData;
    node->textColor[0] = info->textColor[0];
    node->textColor[1] = info->textColor[1];
    node->textColor[2] = info->textColor[2];
    node->textColor[3] = info->textColor[3];
    node->icon[0] = info->icon[0];
    node->icon[1] = info->icon[1];
    node->icon[2] = info->icon[2];
    node->icon[3] = info->icon[3];
    node->expanded = info->expanded;
    node->font = info->font ? info->font : defaultFont_;
    if (node->font) {   // the original grows the row height to the font's glyph height
        const std::uint32_t height = static_cast<std::uint32_t>(node->font->CharHeight(nullptr, 0));   // vslot 0x128
        if (rowHeight_ < height) rowHeight_ = height;
    }
    node->parent = parent;
    // Placement: -2 first child, -3 last child, -4 sorted by text, >= 0 after the sibling with that id.
    std::list<GZWinTreeView_TreeNode*>& children = parent->children;
    std::list<GZWinTreeView_TreeNode*>::iterator position = children.end();
    switch (info->insertAfter) {
        case -2:
            position = children.begin();
            break;
        case -3:
            position = children.end();
            break;
        case -4: {
            position = children.end();
            for (auto it = children.begin(); it != children.end(); ++it) {
                const cRZString& other = (*it)->text;
                const std::size_t length = std::min(other.Strlen(), node->text.Strlen());
                const int cmp = length ? std::memcmp(other.Data(), node->text.Data(), length) : 0;
                if (cmp > 0 || (cmp == 0 && other.Strlen() > node->text.Strlen())) {
                    position = it;
                    break;
                }
            }
            break;
        }
        default:
            if (info->insertAfter >= 0) {
                position = children.end();
                for (auto it = children.begin(); it != children.end(); ++it) {
                    if ((*it)->id == info->insertAfter) {
                        position = std::next(it);
                        break;
                    }
                }
            } else {
                position = children.end();
            }
            break;
    }
    children.insert(position, node);
    ++parent->childCount;
    needsRecalc_ = true;
    InvalidateSelfAndParents();
    nodes_[node->id] = node;
    return node->id;
}

int cGZWinTreeView::InsertNode(int parent, int insertAfter, const cIGZString& text, void* userData) {   // @ 0x10051ac56
    if (!initialized_) InternalInit();
    TV_NodeInsertInfo info;
    info.parent = parent;
    info.insertAfter = insertAfter;
    info.text.AssignFromIGZString(text);
    info.userData = userData;
    info.icon[0] = kDefaultImageCollapsed;
    info.icon[1] = kDefaultImageCollapsedSelected;
    info.icon[2] = kDefaultImageExpanded;
    info.icon[3] = kDefaultImageExpandedSelected;
    info.font = defaultFont_;
    info.textColor[0] = defaultTextColor_[kNodeTextNormal];
    info.textColor[1] = defaultTextColor_[kNodeTextSelected];
    info.textColor[2] = defaultTextColor_[kNodeTextNormalShadow];
    info.textColor[3] = defaultTextColor_[kNodeTextSelectedShadow];
    info.expanded = false;
    return InsertNode(&info);
}

int cGZWinTreeView::InsertNode(int parent, int insertAfter, const cIGZString& text, int icon0, int icon1, int icon2, int icon3) {   // @ 0x10051ad40
    if (!initialized_) InternalInit();
    TV_NodeInsertInfo info;
    info.parent = parent;
    info.insertAfter = insertAfter;
    info.text.AssignFromIGZString(text);
    info.userData = nullptr;
    info.icon[0] = icon0;
    info.icon[1] = icon1;
    info.icon[2] = icon2;
    info.icon[3] = icon3;
    info.font = defaultFont_;
    info.textColor[0] = defaultTextColor_[kNodeTextNormal];
    info.textColor[1] = defaultTextColor_[kNodeTextSelected];
    info.textColor[2] = defaultTextColor_[kNodeTextNormalShadow];
    info.textColor[3] = defaultTextColor_[kNodeTextSelectedShadow];
    info.expanded = false;
    return InsertNode(&info);
}

void cGZWinTreeView::TransferNode(int nodeId, int newParentId, int insertAfter) {   // @ 0x10051ae3a
    GZWinTreeView_TreeNode* node = FindNode(nodeId);
    GZWinTreeView_TreeNode* newParent = FindNode(newParentId);
    if (!node || !newParent || !node->parent) return;
    GZWinTreeView_TreeNode* oldParent = node->parent;
    std::list<GZWinTreeView_TreeNode*>& source = oldParent->children;
    auto element = std::find(source.begin(), source.end(), node);
    if (element == source.end()) return;
    std::list<GZWinTreeView_TreeNode*>& destination = newParent->children;
    auto position = destination.end();
    if (insertAfter == -2) {
        position = destination.begin();
    } else if (insertAfter == -3) {
        position = destination.end();
    } else if (insertAfter > 0) {
        position = destination.end();
        for (auto it = destination.begin(); it != destination.end(); ++it) {
            if ((*it)->id == insertAfter) {
                position = std::next(it);
                break;
            }
        }
    }
    destination.splice(position, source, element);
    --oldParent->childCount;
    ++newParent->childCount;
    node->parent = newParent;
    needsRecalc_ = true;
    InvalidateSelfAndParents();
}

bool cGZWinTreeView::RemoveNode(int id) {   // @ 0x10051afda
    GZWinTreeView_TreeNode* node = FindNode(id);
    if (!node) return false;
    if (node->parent) {
        std::list<GZWinTreeView_TreeNode*>& siblings = node->parent->children;
        auto it = std::find(siblings.begin(), siblings.end(), node);
        if (it != siblings.end()) {
            siblings.erase(it);
            --node->parent->childCount;
        }
    }
    FlagNodeForRemovalRecursively(node);
    for (auto it = nodes_.begin(); it != nodes_.end();) {
        if (it->second && it->second->removeFlag) {
            delete it->second;
            it = nodes_.erase(it);
        } else {
            ++it;
        }
    }
    needsRecalc_ = true;
    InvalidateSelfAndParents();
    return true;
}

void cGZWinTreeView::FlagNodeForRemovalRecursively(GZWinTreeView_TreeNode* node) {   // @ 0x10051b15c
    if (!node) return;
    while (!node->children.empty()) {
        GZWinTreeView_TreeNode* child = node->children.back();
        FlagNodeForRemovalRecursively(child);
        node->children.pop_back();
        node->childCount = node->childCount > 0 ? node->childCount - 1 : 0;
    }
    if (node->selected) RemoveSelection(node->id);
    if (!node->groupNode) node->removeFlag = true;
}

void cGZWinTreeView::RemoveAllNodes() {   // @ 0x10051afc6
    // The original forwards to RemoveNode(0): the root (id 0) stays, its subtree goes.
    RemoveNode(0);
}

// ---- node accessors -----------------------------------------------------------------------------------------------

bool cGZWinTreeView::GetNodeInfo(int nodeId, TV_NodeInfo& info) {   // @ 0x10051b202
    GZWinTreeView_TreeNode* node = FindNode(nodeId);
    if (!node) return false;
    info.expanded = node->expanded;
    info.groupNode = node->groupNode;
    info.visible = GetNodeVisible(nodeId);
    info.selected = node->selected;
    info.icon[0] = node->icon[0];
    info.icon[1] = node->icon[1];
    info.icon[2] = node->icon[2];
    info.icon[3] = node->icon[3];
    info.font = node->font;
    info.text.AssignFromIGZString(node->text);
    info.userData = node->userData;
    info.textColor[0] = node->textColor[0];
    info.textColor[1] = node->textColor[2];   // memory slot 1 is logical colour 2
    info.textColor[2] = node->textColor[1];   // memory slot 2 is logical colour 1
    info.textColor[3] = node->textColor[3];
    info.children.clear();
    for (GZWinTreeView_TreeNode* child : node->children) info.children.push_back(child->id);
    return true;
}

bool cGZWinTreeView::SetNodeInfo(int nodeId, const TV_NodeInfo& info) {   // @ 0x10051b3de
    GZWinTreeView_TreeNode* node = FindNode(nodeId);
    if (!node) return false;
    if (info.expanded != node->expanded) {
        node->expanded = info.expanded;
        SendTreeMessage(info.expanded ? kGZMsgTreeViewExpand : kGZMsgTreeViewCollapse, static_cast<std::uint64_t>(node->id), 0);
    }
    if (info.visible) SetNodeVisible(nodeId);
    if (info.selected != node->selected) {
        if (!info.selected) RemoveSelection(nodeId);
        else AddSelection(nodeId, false);
    }
    node->textColor[2] = info.textColor[1];
    node->textColor[3] = info.textColor[3];
    node->textColor[0] = info.textColor[0];
    node->textColor[1] = info.textColor[2];
    SetNodeFont(nodeId, info.font);
    node->text.AssignFromIGZString(info.text);
    node->userData = info.userData;
    node->icon[2] = info.icon[2];
    node->icon[3] = info.icon[3];
    node->icon[0] = info.icon[0];
    node->icon[1] = info.icon[1];
    needsRecalc_ = true;
    InvalidateSelfAndParents();
    return true;
}

cIGZString* cGZWinTreeView::GetNodeText(int nodeId) {   // @ 0x10051b56a
    GZWinTreeView_TreeNode* node = FindNode(nodeId);
    return node ? static_cast<cIGZString*>(&node->text) : static_cast<cIGZString*>(&emptyText_);
}

bool cGZWinTreeView::SetNodeText(int nodeId, const cIGZString& text) {   // @ 0x10051b5c2
    GZWinTreeView_TreeNode* node = FindNode(nodeId);
    if (!node) return false;
    node->text.AssignFromIGZString(text);
    needsRecalc_ = true;
    InvalidateSelfAndParents();
    return true;
}

void* cGZWinTreeView::GetNodeUserData(int nodeId) {   // @ 0x10051b63c
    GZWinTreeView_TreeNode* node = FindNode(nodeId);
    return node ? node->userData : nullptr;
}

bool cGZWinTreeView::SetNodeUserData(int nodeId, void* data) {   // @ 0x10051b68a
    GZWinTreeView_TreeNode* node = FindNode(nodeId);
    if (!node) return false;
    node->userData = data;
    return true;
}

std::int32_t cGZWinTreeView::GetNodeIcon(int nodeId, TV_NodeIconIdx which) {   // @ 0x10051b6dc
    GZWinTreeView_TreeNode* node = FindNode(nodeId);
    if (!node || static_cast<std::uint32_t>(which) > 3) return -1;
    return node->icon[static_cast<std::size_t>(which)];
}

bool cGZWinTreeView::SetNodeIcon(int nodeId, TV_NodeIconIdx which, int image) {   // @ 0x10051b764
    GZWinTreeView_TreeNode* node = FindNode(nodeId);
    if (!node || static_cast<std::uint32_t>(which) > 3) return false;
    node->icon[static_cast<std::size_t>(which)] = image;
    needsRecalc_ = true;
    InvalidateSelfAndParents();
    return true;
}

void cGZWinTreeView::SetDefaultFont(cIGZFont* font) {   // @ 0x10051b800
    if (defaultFont_ == font) return;
    if (font) font->AddRef();
    if (defaultFont_) defaultFont_->Release();
    defaultFont_ = font;
}

cIGZFont* cGZWinTreeView::GetNodeFont(int nodeId) {   // @ 0x10051b846
    GZWinTreeView_TreeNode* node = FindNode(nodeId);
    return node ? node->font : nullptr;
}

bool cGZWinTreeView::SetNodeFont(int nodeId, cIGZFont* font) {   // @ 0x10051b894
    GZWinTreeView_TreeNode* node = FindNode(nodeId);
    if (!node) return false;
    if (!font) font = defaultFont_;
    if (!font) return false;   // the original would dereference the null default font
    if (node->font != font) {
        node->font = font;
        const std::uint32_t height = static_cast<std::uint32_t>(font->CharHeight(nullptr, 0));   // vslot 0x128
        if (rowHeight_ < height) rowHeight_ = height;
        needsRecalc_ = true;
        InvalidateSelfAndParents();
    }
    return true;
}

std::uint32_t cGZWinTreeView::GetDefaultNodeTextColor(TV_NodeTextColorIdx which) const {   // @ 0x10051b95e
    return static_cast<int>(which) < 4 ? defaultTextColor_[static_cast<std::size_t>(which)] : 0;
}

void cGZWinTreeView::SetDefaultNodeTextColor(TV_NodeTextColorIdx which, std::uint32_t native) {   // @ 0x10051b974
    if (static_cast<int>(which) < 4) defaultTextColor_[static_cast<std::size_t>(which)] = native;
}

std::uint32_t cGZWinTreeView::GetNodeTextColor(int nodeId, TV_NodeTextColorIdx which) {   // @ 0x10051b988
    GZWinTreeView_TreeNode* node = FindNode(nodeId);
    if (!node || static_cast<std::uint32_t>(which) > 3) return 0;
    return NodeColor(node, which);
}

bool cGZWinTreeView::SetNodeTextColor(int nodeId, TV_NodeTextColorIdx which, std::uint32_t native) {   // @ 0x10051ba0c
    GZWinTreeView_TreeNode* node = FindNode(nodeId);
    if (!node || static_cast<std::uint32_t>(which) > 3) return false;
    NodeColor(node, which) = native;
    needsRecalc_ = true;
    InvalidateSelfAndParents();
    return true;
}

bool cGZWinTreeView::GetNodeExpanded(int nodeId) {   // @ 0x10051baa8
    GZWinTreeView_TreeNode* node = FindNode(nodeId);
    return node ? node->expanded : false;
}

bool cGZWinTreeView::SetNodeExpanded(int nodeId, bool expanded) {   // @ 0x10051bafa
    GZWinTreeView_TreeNode* node = FindNode(nodeId);
    if (!node) return false;
    if (node->expanded != expanded) {
        node->expanded = expanded;
        needsRecalc_ = true;
        InvalidateSelfAndParents();
        SendTreeMessage(expanded ? kGZMsgTreeViewExpand : kGZMsgTreeViewCollapse, static_cast<std::uint64_t>(node->id), 0);
    }
    return true;
}

bool cGZWinTreeView::GetNodeVisible(int nodeId) {   // @ 0x10051bbdc
    GZWinTreeView_TreeNode* node = FindNode(nodeId);
    if (!node) return false;
    if (needsRecalc_) RecalculateAll();
    if (!node->visible) return false;
    return static_cast<std::uint32_t>(topRow_) <= node->rowIndex &&
           node->rowIndex < static_cast<std::uint32_t>(topRow_) + static_cast<std::uint32_t>(rowsPerPage_);
}

bool cGZWinTreeView::SetNodeVisible(int nodeId) {   // @ 0x10051bc62
    GZWinTreeView_TreeNode* node = FindNode(nodeId);
    if (!node) return false;
    if (needsRecalc_) RecalculateAll();
    if (!node->visible) {
        // Expand every ancestor, notifying the target of each expansion.
        for (GZWinTreeView_TreeNode* parent = node->parent; parent; parent = parent->parent) {
            if (!parent->expanded) {
                parent->expanded = true;
                SendTreeMessage(kGZMsgTreeViewExpand, static_cast<std::uint64_t>(parent->id), 0);
            }
        }
        RecalculateAll();
        if (!node->visible) return false;
    }
    const std::uint32_t row = node->rowIndex;
    if (row < static_cast<std::uint32_t>(topRow_)) {
        if (vScrollbar_) vScrollbar_->SetValue(static_cast<int>(row));
    } else if (static_cast<std::uint32_t>(topRow_) + static_cast<std::uint32_t>(rowsPerPage_) <= row) {
        if (vScrollbar_) vScrollbar_->SetValue(static_cast<int>(row) + 1 - rowsPerPage_);
    }
    return true;
}

std::int32_t cGZWinTreeView::GetNodeChildrenCount(int nodeId) {   // @ 0x10051bdaa
    GZWinTreeView_TreeNode* node = FindNode(nodeId);
    return node ? node->childCount : 0;
}

bool cGZWinTreeView::GetNodeChildren(int nodeId, int* ids) {   // @ 0x10051bdfa
    GZWinTreeView_TreeNode* node = FindNode(nodeId);
    if (!node || !ids) return false;
    for (GZWinTreeView_TreeNode* child : node->children) *ids++ = child->id;
    return true;
}

bool cGZWinTreeView::GetNodeSelected(int nodeId) {   // @ 0x10051be72
    GZWinTreeView_TreeNode* node = FindNode(nodeId);
    return node ? node->selected : false;
}

int cGZWinTreeView::GetNodeParent(int nodeId) {   // @ 0x10051bec4
    GZWinTreeView_TreeNode* node = FindNode(nodeId);
    return node && node->parent ? node->parent->id : -1;
}

int cGZWinTreeView::GetNodeInsertAfter(int nodeId) {   // @ 0x10051bf1e
    GZWinTreeView_TreeNode* node = FindNode(nodeId);
    return node ? node->insertAfter : -1;
}

// ---- selection ----------------------------------------------------------------------------------------------------

std::uint32_t cGZWinTreeView::GetSelectionSize() {   // @ 0x10051bf6e
    return static_cast<std::uint32_t>(selection_.size());
}

bool cGZWinTreeView::GetSelection(int* ids) {   // @ 0x10051bf86
    for (GZWinTreeView_TreeNode* node : selection_) *ids++ = node->id;
    return true;
}

int cGZWinTreeView::GetSelection() {   // @ 0x10051bfd4
    return selection_.empty() ? -1 : selection_.front()->id;
}

bool cGZWinTreeView::AddSelection(int nodeId, bool notify) {   // @ 0x10051bff6
    GZWinTreeView_TreeNode* node = FindNode(nodeId);
    if (!node) return false;
    const bool multi = GetTVFlag(kTVFlagMultiSelect);
    bool cleared = false;
    const bool many = selection_.size() > 1;
    if ((!node->selected && !selection_.empty()) || (node->selected && many)) {
        if (notify || !multi) {
            for (GZWinTreeView_TreeNode* selected : selection_) selected->selected = false;
            selection_.clear();
            cleared = true;
        }
    }
    if (!node->selected) {
        SetNodeVisible(node->id);
        node->selected = true;
        selection_.push_back(node);
    } else if (!cleared) {
        return true;
    }
    InvalidateSelfAndParents();
    SendTreeMessage(kGZMsgTreeViewSelectionChanged, 0, 0);
    return true;
}

bool cGZWinTreeView::RemoveSelection(int nodeId) {   // @ 0x10051c1ac
    for (auto it = selection_.begin(); it != selection_.end(); ++it) {
        if ((*it)->id != nodeId) continue;
        (*it)->selected = false;
        selection_.erase(it);
        InvalidateSelfAndParents();
        SendTreeMessage(kGZMsgTreeViewSelectionChanged, 0, 0);
        return true;
    }
    return false;
}

bool cGZWinTreeView::ClearSelection() {   // @ 0x10051c25a
    if (selection_.empty()) return true;
    for (GZWinTreeView_TreeNode* node : selection_) node->selected = false;
    selection_.clear();
    InvalidateSelfAndParents();
    SendTreeMessage(kGZMsgTreeViewSelectionChanged, 0, 0);
    return true;
}

void cGZWinTreeView::ResetClickState() {
    g_clickTimer.Reset();
    g_clickTimer.Restart();
    g_lastClickTime = 0;
    g_lastNodeSelected = 0;
    g_lastAreaClicked = cRZRect{0, 0, 0, 0};
}

bool cGZWinTreeView::SendTreeMessage(std::uint32_t type, std::uint64_t p1, std::uint64_t p2) {
    if (!notify_) return false;
    SendMsg(notify_, type, GetID(), p1, p2);
    return true;
}

// ---- scrollbars ---------------------------------------------------------------------------------------------------

void cGZWinTreeView::CreateHorizontalScrollbar() {   // @ 0x10051c694
    if (GetChildWindowFromID(kTreeViewHScrollId)) return;
    ITreeViewServices* services = TreeViewServices();
    cIGZWinScrollbar* bar = services ? services->CreateScrollbar(kTreeViewHScrollId, 1, 0, 0, 0) : nullptr;   // WCM 0x88
    hScrollbar_ = bar;
    if (!bar) return;
    cRZRect area;
    cIGZBuffer* image = nullptr;
    int flags = 0;
    if (!bar->GetImage(area, image, flags)) {   // vslot 0x90
        scrollbarThickness_ = 0x10;
        bar->AsIGZWin()->SetH(scrollbarThickness_);
    } else {
        scrollbarThickness_ = bar->AsIGZWin()->GetH();
    }
    ChildAdd(bar->AsIGZWin());
}

void cGZWinTreeView::RemoveHorizontalScrollbar() {   // @ 0x10051c790
    if (!hScrollbar_) return;
    ChildDelete(hScrollbar_->AsIGZWin());
    hScrollbar_->Release();
    hScrollbar_ = nullptr;
}

void cGZWinTreeView::CreateVerticalScrollbar() {   // @ 0x10051c7ea
    if (GetChildWindowFromID(kTreeViewVScrollId)) return;
    ITreeViewServices* services = TreeViewServices();
    cIGZWinScrollbar* bar = services ? services->CreateScrollbar(kTreeViewVScrollId, 2, 0, 0, 0) : nullptr;
    vScrollbar_ = bar;
    if (!bar) return;
    cRZRect area;
    cIGZBuffer* image = nullptr;
    int flags = 0;
    if (!bar->GetImage(area, image, flags)) {
        scrollbarThickness_ = 0x10;
        bar->AsIGZWin()->SetW(scrollbarThickness_);
    } else {
        scrollbarThickness_ = bar->AsIGZWin()->GetW();
    }
    ChildAdd(bar->AsIGZWin());
}

void cGZWinTreeView::RemoveVerticalScrollbar() {   // @ 0x10051c8e6
    if (!vScrollbar_) return;
    ChildDelete(vScrollbar_->AsIGZWin());
    vScrollbar_->Release();
    vScrollbar_ = nullptr;
}

void cGZWinTreeView::LayoutScrollbars() {   // @ 0x10051c940
    const int width = drawArea_.right - drawArea_.left;
    const int height = drawArea_.bottom - drawArea_.top;
    if (vScrollbar_) {
        int barHeight = height;
        if (hScrollVisible_) barHeight -= scrollbarThickness_;
        vScrollbar_->AsIGZWin()->SetH(barHeight);                                    // cIGZWin [52]
        vScrollbar_->AsIGZWin()->GZWinMoveTo(width - scrollbarThickness_, 0);       // cIGZWin [56]
    }
    if (hScrollbar_) {
        int barWidth = width;
        if (vScrollVisible_) barWidth -= scrollbarThickness_;
        hScrollbar_->AsIGZWin()->SetW(barWidth);                                    // cIGZWin [51]
        hScrollbar_->AsIGZWin()->GZWinMoveTo(0, height - scrollbarThickness_);
    }
}

void cGZWinTreeView::CheckForScrollbars() {   // @0x10051ca18
    hScrollVisible_ = false;
    vScrollVisible_ = false;
    const int width = drawArea_.right - drawArea_.left;
    const int height = drawArea_.bottom - drawArea_.top;
    int canvasWidth = canvas_ ? static_cast<int>(canvas_->Width()) : 0;
    int needWidth = -width + canvasWidth + static_cast<int>(gutterX_) * 2;
    int rowsWithoutBars = static_cast<int>(visibleRowCount_) -
                          RoundNearest(static_cast<float>(height + static_cast<int>(gutterY_) * -2) /
                                       static_cast<float>(rowHeight_));
    if (needWidth > 0) {
        CreateHorizontalScrollbar();
        hScrollVisible_ = true;
    }
    bool noVerticalBar = true;
    if (rowsWithoutBars >= 1) {
        CreateVerticalScrollbar();
        vScrollVisible_ = true;
        noVerticalBar = false;
    }
    if (!hScrollVisible_) {
        int sum;
        const bool overflow = __builtin_add_overflow(needWidth, scrollbarThickness_, &sum);
        if (noVerticalBar || sum == 0 || overflow) {
            RemoveHorizontalScrollbar();
            goto vertical;
        }
        hScrollVisible_ = true;
    } else if (noVerticalBar) {
        int sum;
        const bool overflow = __builtin_add_overflow(
            rowsWithoutBars, RoundNearest(static_cast<float>(scrollbarThickness_) / static_cast<float>(rowHeight_)), &sum);
        if (sum != 0 && !overflow) vScrollVisible_ = true;
    }
    CreateHorizontalScrollbar();
vertical:
    if (!vScrollVisible_) RemoveVerticalScrollbar();
    else CreateVerticalScrollbar();
    LayoutScrollbars();
}

void cGZWinTreeView::UpdateScrollbarValues() {   // @0x10051cb56
    const int width = drawArea_.right - drawArea_.left;
    const int height = drawArea_.bottom - drawArea_.top;
    if (hScrollVisible_ && hScrollbar_) {
        std::uint32_t extent = static_cast<std::uint32_t>(-width + static_cast<int>(canvas_ ? canvas_->Width() : 0) +
                                                         static_cast<int>(gutterX_) * 2);
        if (vScrollVisible_) extent += static_cast<std::uint32_t>(scrollbarThickness_);
        int minimum = 0, maximum = 0;
        hScrollbar_->GetMinMax(minimum, maximum);   // vslot 0x40
        if (maximum != static_cast<int>(extent)) {
            hScrollbar_->SetMinMax(0, static_cast<int>(extent));
            hScrollbar_->SetLineAndPageCount(extent, extent);
            if (hScrollbar_->GetValue() > static_cast<int>(extent)) hScrollbar_->SetValue(static_cast<int>(extent));
        }
    }
    if (vScrollVisible_ && vScrollbar_) {
        const std::uint32_t rowHeight = rowHeight_;
        const std::uint32_t viewHeight = static_cast<std::uint32_t>(height + static_cast<int>(gutterY_) * -2);
        const std::uint32_t rowsFit = viewHeight / rowHeight;
        int maximum = static_cast<int>(visibleRowCount_) - static_cast<int>(rowsFit);
        if (hScrollVisible_)
            maximum += RoundNearest(static_cast<float>(scrollbarThickness_) / static_cast<float>(rowHeight));
        int minimum = 0, currentMaximum = 0;
        vScrollbar_->GetMinMax(minimum, currentMaximum);
        if (currentMaximum != maximum) {
            vScrollbar_->SetMinMax(0, maximum);
            std::uint32_t page = static_cast<std::uint32_t>(maximum);
            if (rowHeight <= viewHeight && rowsFit != 0)
                page = (rowsFit - 1 + visibleRowCount_) / rowsFit;
            vScrollbar_->SetLineAndPageCount(static_cast<std::uint32_t>(maximum), page);
            if (vScrollbar_->GetValue() > maximum) vScrollbar_->SetValue(maximum);
        }
    }
}

// ---- layout / painting --------------------------------------------------------------------------------------------

void cGZWinTreeView::RecalculateAll() {   // @ 0x100519ac4
    visibleNodes_.clear();   // +0x198 = +0x190
    for (auto& entry : nodes_)
        if (entry.second) entry.second->visible = false;
    visibleRowCount_ = 0;
    std::uint32_t maxWidth = 0;
    FindTreeInfo(root_, visibleRowCount_, 0, maxWidth);
    const int width = drawArea_.right - drawArea_.left;
    const int height = drawArea_.bottom - drawArea_.top;
    // The canvas is viewport-sized (the view draws only the rows in view): one extra row when the content is taller
    // than the window, otherwise exactly the content height.
    const std::uint32_t contentHeight = static_cast<std::uint32_t>(visibleRowCount_ * rowHeight_);
    std::uint32_t canvasHeight = static_cast<std::uint32_t>(rowHeight_ + height);
    if (contentHeight <= static_cast<std::uint32_t>(height)) canvasHeight = contentHeight;
    if (canvas_) {
        if (canvas_->Width() != maxWidth || canvas_->Height() != canvasHeight) {
            canvas_->Shutdown();   // vslot 0x38
            std::uint32_t initWidth = maxWidth, initHeight = canvasHeight;
            if (visibleRowCount_ == 0) {
                initWidth = 1;
                initHeight = 1;
            }
            cGZBufferColorType type;
            type.type = kBufferColorDisplay;
            type.bitsPerPixel = 0;
            canvas_->Init(initWidth, initHeight, type);
        }
    }
    CheckForScrollbars();
    UpdateScrollbarValues();
    if (!hScrollVisible_) hScrollOffset_ = 0;
    if (!vScrollVisible_) {
        topRow_ = 0;
        bottomRow_ = static_cast<int>(visibleRowCount_) - 1;
    }
    int reduceWidth = 0, reduceHeight = 0;
    if (vScrollVisible_) reduceWidth = -scrollbarThickness_;
    if (hScrollVisible_) reduceHeight = -scrollbarThickness_;
    std::uint32_t viewWidth = static_cast<std::uint32_t>(width + static_cast<int>(gutterX_) * -2 + reduceWidth);
    std::uint32_t viewRectHeight = static_cast<std::uint32_t>(height + static_cast<int>(gutterY_) * -2 + reduceHeight);
    if (canvas_ && canvas_->Width() <= viewWidth) viewWidth = canvas_->Width();
    if (canvas_ && canvas_->Height() <= viewRectHeight) viewRectHeight = canvas_->Height();
    sourceRect_.left = 0;
    sourceRect_.top = 0;
    sourceRect_.right = static_cast<int>(viewWidth);
    sourceRect_.bottom = static_cast<int>(viewRectHeight);
    destRect_.left = static_cast<int>(gutterX_) + drawArea_.left;
    destRect_.top = static_cast<int>(gutterY_) + drawArea_.top;
    destRect_.right = destRect_.left + static_cast<int>(viewWidth);
    destRect_.bottom = destRect_.top + static_cast<int>(viewRectHeight);
    rowsPerPage_ = static_cast<int>(viewRectHeight / rowHeight_);
    rowsThatFit_ = RoundNearest(static_cast<float>(viewRectHeight) / static_cast<float>(rowHeight_));
    bottomRow_ = rowsThatFit_ - 1 + topRow_;
    needsRecalc_ = false;
}

void cGZWinTreeView::FindTreeInfo(GZWinTreeView_TreeNode* node, std::uint32_t& row, std::uint32_t indent,
                                  std::uint32_t& maxWidth) {   // @ 0x10051cd08
    if (!node) return;
    if (!node->groupNode || GetTVFlag(kTVFlagDrawGroupRow)) {
        node->rowIndex = row;
        node->visible = true;
        visibleNodes_.push_back(node);
    }
    std::uint32_t halfIcon = 0;
    if (GetTVFlag(kTVFlagCentreIcons) && maxIconWidth_ >= minMaxBoxSize_ && maxIconWidth_ - minMaxBoxSize_ != 0)
        halfIcon = (maxIconWidth_ - minMaxBoxSize_) >> 1;
    int x;
    if (!node->groupNode) {
        const int step = static_cast<int>(iconGutter_ + minMaxBoxSize_ + halfIcon);
        x = step * static_cast<int>(indent) + 3 + step;
    } else {
        if (!GetTVFlag(kTVFlagDrawGroupRow)) {
            for (GZWinTreeView_TreeNode* child : node->children)
                FindTreeInfo(child, row, indent, maxWidth);
            return;
        }
        x = 3;
    }
    GetTVFlag(kTVFlagCentreIcons);
    const int textWidth = node->font ? node->font->GetStringWidth(node->text.Data(), node->text.Strlen()) : 0;   // vslot 0x180
    const std::uint32_t extent = static_cast<std::uint32_t>(x + static_cast<int>(maxIconWidth_) +
                                                            static_cast<int>(iconGutter_) + textWidth);
    if (maxWidth < extent) maxWidth = extent;
    ++row;
    if (node->expanded) {
        const std::uint32_t childIndent = (node->groupNode ? 0u : 1u) + indent;
        for (GZWinTreeView_TreeNode* child : node->children) FindTreeInfo(child, row, childIndent, maxWidth);
    }
}

void cGZWinTreeView::UpdateSelectionList() {   // @ 0x10051d0a6
    bool changed = false;
    for (auto it = selection_.begin(); it != selection_.end();) {
        if (!(*it)->selected) {
            it = selection_.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }
    if (changed) SendTreeMessage(kGZMsgTreeViewSelectionChanged, 0, 0);
}

void cGZWinTreeView::DrawNode(GZWinTreeView_TreeNode* node, std::uint32_t& row, std::uint32_t indent,
                              std::vector<bool>& lines, bool lastChild) {   // @ 0x10051d142
    if (!node) return;
    node->iconDrawn = false;
    node->boxDrawn = false;
    node->textDrawn = false;
    std::uint32_t halfIcon = 0;
    if (GetTVFlag(kTVFlagCentreIcons)) halfIcon = (maxIconWidth_ - minMaxBoxSize_) >> 1;
    int x = 0, y = 0;
    if (!node->groupNode) {
        x = static_cast<int>((minMaxBoxSize_ + halfIcon + iconGutter_) * indent + halfIcon);
        y = static_cast<int>((row - static_cast<std::uint32_t>(topRow_)) * rowHeight_);
        if (row >= static_cast<std::uint32_t>(topRow_)) {
            if (GetTVFlag(kTVFlagLines) && canvas_) {
                for (std::size_t i = 0; i < lines.size(); ++i) {
                    if (static_cast<std::uint32_t>(i) == indent || !lines[i]) continue;
                    if (canvas_->Lock(static_cast<cIGZBuffer::eLockFlags>(kLockLine))) {
                        const int lineX = static_cast<int>((minMaxBoxSize_ + halfIcon + iconGutter_) * i +
                                                           (minMaxBoxSize_ >> 1) + halfIcon);
                        canvas_->DrawLine(lineX, y, lineX, static_cast<int>(rowHeight_) + y, lineColor_, lineThickness_);
                        canvas_->Unlock(static_cast<cIGZBuffer::eLockFlags>(kLockLine));
                    }
                }
            }
            const int boxTop = static_cast<int>(((rowHeight_ - minMaxBoxSize_) >> 1) + static_cast<std::uint32_t>(y));
            const bool drawUpper = GetTVFlag(kTVFlagDrawGroupRow) || row != 0;
            if (node->childCount == 0) {
                if (canvas_ && canvas_->Lock(static_cast<cIGZBuffer::eLockFlags>(kLockLine))) {
                    const int half = static_cast<int>(minMaxBoxSize_ >> 1);
                    const int mid = boxTop + half;
                    const int centerX = half + x;
                    const int box = static_cast<int>(minMaxBoxSize_);
                    canvas_->DrawLine(centerX, mid, x + box, mid, lineColor_, lineThickness_);
                    if (drawUpper) canvas_->DrawLine(centerX, boxTop, centerX, mid, lineColor_, lineThickness_);
                    if (!lastChild) canvas_->DrawLine(centerX, mid, centerX, box + boxTop, lineColor_, lineThickness_);
                    int branchEnd = static_cast<int>(iconGutter_) + x + box;
                    if (!GetTVFlag(kTVFlagCentreIcons)) branchEnd += box;
                    canvas_->DrawLine(x + box, mid, branchEnd, mid, lineColor_, lineThickness_);
                    if (drawUpper) canvas_->DrawLine(centerX, y, centerX, boxTop, lineColor_, lineThickness_);
                    if (!lastChild)
                        canvas_->DrawLine(centerX, boxTop + box, centerX, static_cast<int>(rowHeight_) + y,
                                          lineColor_, lineThickness_);
                    canvas_->Unlock(static_cast<cIGZBuffer::eLockFlags>(kLockLine));
                }
            } else if (canvas_) {
                if (canvas_->Lock(static_cast<cIGZBuffer::eLockFlags>(kLockBox))) {
                    cRZRect boxRect;
                    boxRect.left = x;
                    boxRect.top = boxTop;
                    boxRect.right = x + static_cast<int>(minMaxBoxSize_);
                    boxRect.bottom = boxTop + static_cast<int>(minMaxBoxSize_);
                    DrawRectangleOutline(canvas_, boxRect, lineColor_, lineThickness_);   // @ 0x1002b3deb
                    canvas_->Unlock(static_cast<cIGZBuffer::eLockFlags>(kLockBox));
                    node->boxRect = boxRect;
                    node->boxDrawn = true;
                }
                const int box = static_cast<int>(minMaxBoxSize_);
                const int half = box >> 1;
                // The plus / minus arm length: bit 1 clear -> half-1, then bit 0 clear -> back to half-1.
                int length = half;
                if ((box & 2) == 0) length = half - 1;
                if (box & 1) length = (box & 2) ? half - 1 : half;
                if (canvas_->Lock(static_cast<cIGZBuffer::eLockFlags>(kLockLine))) {
                    const int centreX = x + (box - length) / 2;
                    const int centreY = boxTop + half;
                    canvas_->DrawLine(centreX, centreY, centreX + length, centreY, lineColor_, lineThickness_);
                    canvas_->Unlock(static_cast<cIGZBuffer::eLockFlags>(kLockLine));
                }
                if (!node->expanded && canvas_->Lock(static_cast<cIGZBuffer::eLockFlags>(kLockLine))) {
                    const int centreX = x + half;
                    const int centreY = boxTop + (box - length) / 2;
                    canvas_->DrawLine(centreX, centreY, centreX, centreY + length, lineColor_, lineThickness_);
                    canvas_->Unlock(static_cast<cIGZBuffer::eLockFlags>(kLockLine));
                }
            }
        }
    } else {
        if (!GetTVFlag(kTVFlagDrawGroupRow)) {
            const std::size_t count = node->children.size();
            std::size_t i = 0;
            for (GZWinTreeView_TreeNode* child : node->children) {
                lines.push_back(i + 1 != count);
                DrawNode(child, row, indent, lines, i + 1 == count);
                lines.pop_back();
                ++i;
            }
            return;
        }
    }
    if (row >= static_cast<std::uint32_t>(topRow_)) {
        if (GetTVFlag(kTVFlagLines) && canvas_ && node->childCount != 0 && node->expanded &&
            canvas_->Lock(static_cast<cIGZBuffer::eLockFlags>(kLockLine))) {
            const int lineX = static_cast<int>(halfIcon + static_cast<std::uint32_t>(x) + iconGutter_ + minMaxBoxSize_ +
                                               (minMaxBoxSize_ >> 1));
            const int midY = static_cast<int>((rowHeight_ >> 1) + static_cast<std::uint32_t>(y));
            canvas_->DrawLine(lineX, midY, lineX, static_cast<int>(rowHeight_ >> 1) + midY, lineColor_, lineThickness_);
            canvas_->Unlock(static_cast<cIGZBuffer::eLockFlags>(kLockLine));
        }
        DrawItem(canvas_, node, x + static_cast<int>(minMaxBoxSize_) + static_cast<int>(iconGutter_), y);
    }
    ++row;
    if (row > static_cast<std::uint32_t>(bottomRow_) || !node->expanded || node->childCount == 0) return;
    const std::uint32_t childIndent = (node->groupNode ? 0u : 1u) + indent;
    const std::size_t count = node->children.size();
    std::size_t i = 0;
    for (GZWinTreeView_TreeNode* child : node->children) {
        lines.push_back(i + 1 != count);
        DrawNode(child, row, childIndent, lines, i + 1 == count);
        lines.pop_back();
        ++i;
    }
}

void cGZWinTreeView::DrawItem(cIGZBuffer* target, GZWinTreeView_TreeNode* node, int x, int y) {   // @ 0x10051d9b0
    if (!target || !node) return;
    // Quirk: images are only drawn when the centre-icons flag is set (the same flag centres them in the column).
    if (GetTVFlag(kTVFlagCentreIcons)) {
        std::int32_t imageIndex;
        if (!node->expanded || node->childCount == 0) {
            imageIndex = node->icon[0];
            if (node->selected && node->icon[1] != -1) imageIndex = node->icon[1];
        } else {
            imageIndex = node->icon[2];
            if (node->selected && node->icon[3] != -1) imageIndex = node->icon[3];
        }
        if (imageIndex != -1 && static_cast<std::size_t>(imageIndex) < images_.size()) {
            cIGZBuffer* image = nullptr;
            cRZRect area;
            if (GetImage(imageIndex, &image, area)) {
                const int imageWidth = area.right - area.left;
                const int imageHeight = area.bottom - area.top;
                const int dstX = static_cast<int>((maxIconWidth_ - static_cast<std::uint32_t>(imageWidth)) >> 1) + x;
                const int dstY = static_cast<int>((rowHeight_ - static_cast<std::uint32_t>(imageHeight)) >> 1) + y;
                cRZRect destination;
                destination.left = dstX;
                destination.top = dstY;
                destination.right = dstX + imageWidth;
                destination.bottom = dstY + imageHeight;
                if (target->Lock(static_cast<cIGZBuffer::eLockFlags>(kLockImage))) {
                    target->Blt(image, area, destination, nullptr);   // vslot 0xf8
                    target->Unlock(static_cast<cIGZBuffer::eLockFlags>(kLockImage));
                    node->imageRect = destination;
                    node->iconDrawn = true;
                }
            }
        }
    }
    const int textX = x + static_cast<int>(maxIconWidth_) + static_cast<int>(iconGutter_);
    cRZRect textRect;
    textRect.left = textX;
    textRect.top = y;
    textRect.right = textX + 3 + (node->font ? node->font->GetStringWidth(node->text.Data(), node->text.Strlen()) : 0);
    textRect.bottom = static_cast<int>(rowHeight_) + y;
    if (!node->selected) {
        if (node->font)
            node->font->DrawTextLine(target, textX + 3, y + 1, node->text.Data(), node->text.Strlen(),
                                     node->textColor[0], node->textColor[2], nullptr, nullptr);   // vslot 0x198
    } else {
        if (target->Lock(static_cast<cIGZBuffer::eLockFlags>(kLockFill))) {
            const std::uint32_t highlight = defaultTextColor_[kNodeTextSelectedShadow];   // +0x248
            if (!dragPreview_) {
                target->Fill(highlight, textRect, nullptr);
            } else {
                // Drag preview: blend the highlight with the normal shadow colour, then paint three quarters of the
                // row across the whole canvas with the highlight.
                std::uint8_t r1 = 0, g1 = 0, b1 = 0, r2 = 0, g2 = 0, b2 = 0;
                target->ConvertNativeValueToRGB(defaultTextColor_[kNodeTextNormalShadow], r1, g1, b1);
                target->ConvertNativeValueToRGB(highlight, r2, g2, b2);
                const std::uint32_t blended = target->ConvertRGBValueToNative(
                    static_cast<std::uint8_t>((r1 + r2) >> 1), static_cast<std::uint8_t>((g1 + g2) >> 1),
                    static_cast<std::uint8_t>((b1 + b2) >> 1));
                target->Fill(blended, textRect, nullptr);
                cRZRect band;
                band.left = target->GetBufferArea()->left;
                band.top = textRect.top;
                band.right = target->GetBufferArea()->right;
                band.bottom = (textRect.bottom + 2 + textRect.top * 3) >> 2;
                target->Fill(highlight, band, nullptr);
            }
            target->Unlock(static_cast<cIGZBuffer::eLockFlags>(kLockFill));
        }
        if (node->font)
            node->font->DrawTextLine(target, textX + 3, y + 1, node->text.Data(), node->text.Strlen(),
                                     node->textColor[1], node->textColor[3], nullptr, nullptr);
    }
    if (target == canvas_) {
        node->textRect = textRect;
        node->textDrawn = true;
    }
}

bool cGZWinTreeView::GZPaint() {   // @ 0x10051cf06
    if (needsRecalc_) RecalculateAll();
    auto* context = static_cast<cIGZDrawContext*>(drawContext_);
    if (GetTVFlag(kTVFlagBackground) && context) {
        context->SetColor(backgroundColor_);   // packed colour
        context->Fill(drawArea_);
    }
    if (visibleRowCount_ != 0) {
        if (selectionDirty_) {
            UpdateSelectionList();
            selectionDirty_ = false;
        }
        if (GetTVFlag(kTVFlagBackground) && canvas_ &&
            canvas_->Lock(static_cast<cIGZBuffer::eLockFlags>(kLockFill))) {
            // Quirk: the packed background colour is passed to cIGZBuffer::Fill, which wants a native colour.
            canvas_->Fill(backgroundColor_, *canvas_->GetBufferArea(), nullptr);
            canvas_->Unlock(static_cast<cIGZBuffer::eLockFlags>(kLockFill));
        }
        std::uint32_t row = 0;
        std::vector<bool> lines;
        DrawNode(root_, row, 0, lines, false);
        if (context && canvas_) {
            cRZRect source = sourceRect_;
            source.left += hScrollOffset_;
            source.right += hScrollOffset_;
            context->Blt(canvas_, source, destRect_);   // vslot 0x130
        }
    }
    return true;
}

void cGZWinTreeView::UpdateTreeNodesOnExpandCollapse(GZWinTreeView_TreeNode* node, bool keepSelection, bool topLevel) {   // @ 0x10051ddf0
    if (!node) return;
    if (!keepSelection && !topLevel) node->selected = false;
    for (GZWinTreeView_TreeNode* child : node->children) UpdateTreeNodesOnExpandCollapse(child, keepSelection, false);
}

void cGZWinTreeView::SetArea(int l, int t, int r, int b) {   // @ 0x10051de4e
    cGZWin::SetArea(l, t, r, b);
    needsRecalc_ = true;
}

bool cGZWinTreeView::DoMessage(cGZMessage& message) {   // @ 0x10051de8a
    if (message.type == kGZMsgCommand && message.param1 == kGZMsgTreeViewCommandSource) {
        if (message.param2 == kTreeViewHScrollId) {
            if (hScrollbar_) hScrollOffset_ = hScrollbar_->GetValue();
        } else if (message.param2 == kTreeViewVScrollId) {
            if (vScrollbar_) {
                topRow_ = vScrollbar_->GetValue();
                int bottom = topRow_ - 1 + rowsThatFit_;
                const int last = static_cast<int>(visibleRowCount_) - 1;
                if (bottom > last) bottom = last;
                bottomRow_ = bottom;
            }
        }
    }
    return cGZWin::DoMessage(message);
}

// ---- mouse ---------------------------------------------------------------------------------------------------------

cGZWinTreeView::GZWinTreeView_TreeNode* cGZWinTreeView::GetNodeAtCursorPosition(int x, int y) {   // @ 0x10051e432
    const int px = x - static_cast<int>(gutterX_);
    const int py = y - static_cast<int>(gutterY_);
    if (sourceRect_.left <= px && sourceRect_.top <= py && px <= sourceRect_.right && py <= sourceRect_.bottom) {
        const std::uint32_t row = static_cast<std::uint32_t>(py) / rowHeight_;
        if (row <= static_cast<std::uint32_t>(bottomRow_ - topRow_)) {
            const std::size_t index = static_cast<std::size_t>(topRow_) + row;
            if (index < visibleNodes_.size()) return visibleNodes_[index];
        }
    }
    return nullptr;
}

bool cGZWinTreeView::GZOnMouseDownL(int x, int y, std::uint32_t) {   // @ 0x10051df34
    const bool control = cRZKeyboard::IsKeyDownNowStatic(0x11);   // cRZKeyboard @ 0x100241b76
    GZWinTreeView_TreeNode* node = GetNodeAtCursorPosition(x, y);
    if (!node) return false;
    SetNodeVisible(node->id);
    // Double click: same node and same hit rectangle within RZ::WCM()->GetDoubleClickTimeMilliseconds().
    bool doubleClick = false;
    if (g_lastClickTime == 0) {
        g_clickTimer.Restart();
        g_lastClickTime = 1;
        g_lastNodeSelected = node->id;
    } else {
        if (g_lastNodeSelected == node->id) {
            const std::uint32_t elapsed = g_clickTimer.GetElapsedTimeU32();
            const std::uint32_t delta = elapsed - g_lastClickTime;
            doubleClick = delta < TreeViewServices()->DoubleClickMilliseconds();
        }
        g_lastNodeSelected = node->id;
        g_lastClickTime = g_clickTimer.GetElapsedTimeU32();
    }
    const int px = hScrollOffset_ - static_cast<int>(gutterX_) + x;
    const int py = y - static_cast<int>(gutterY_);
    auto inside = [px, py](const cRZRect& r) {
        return r.left <= px && r.top <= py && px <= r.right && py <= r.bottom;
    };
    if (node->boxDrawn && inside(node->boxRect)) {
        g_lastAreaClicked = node->boxRect;
        const bool wasExpanded = node->expanded;
        node->expanded = !wasExpanded;
        if (wasExpanded) {
            UpdateTreeNodesOnExpandCollapse(node, false, true);
            selectionDirty_ = true;
        }
        needsRecalc_ = true;
        InvalidateSelfAndParents();
        SendTreeMessage(node->expanded ? kGZMsgTreeViewExpand : kGZMsgTreeViewCollapse,
                        static_cast<std::uint64_t>(node->id), 0);
        SendTreeMessage(kGZMsgTreeViewCommand, static_cast<std::uint64_t>(node->id), 0);
        return true;
    }
    bool onImage = node->iconDrawn && inside(node->imageRect);
    if (!onImage && node->textDrawn && inside(node->textRect)) {
        if (!doubleClick || !(g_lastAreaClicked.left == node->textRect.left &&
                              g_lastAreaClicked.top == node->textRect.top &&
                              g_lastAreaClicked.right == node->textRect.right &&
                              g_lastAreaClicked.bottom == node->textRect.bottom)) {
            g_lastAreaClicked = node->textRect;
            goto select;
        }
        goto doubleClickHit;
    }
    if (onImage) {
        if (!doubleClick || !(g_lastAreaClicked.left == node->imageRect.left &&
                              g_lastAreaClicked.top == node->imageRect.top &&
                              g_lastAreaClicked.right == node->imageRect.right &&
                              g_lastAreaClicked.bottom == node->imageRect.bottom)) {
            g_lastAreaClicked = node->imageRect;
            goto select;
        }
        goto doubleClickHit;
    }
select:
    if (!control || !GetTVFlag(kTVFlagMultiSelect)) {
        AddSelection(node->id, true);
    } else if (node->selected) {
        RemoveSelection(node->id);
    } else {
        AddSelection(node->id, false);
    }
    SendTreeMessage(kGZMsgTreeViewCommand, static_cast<std::uint64_t>(node->id), 0);
    return true;
doubleClickHit:
    SendTreeMessage(kGZMsgTreeViewDoubleClick, static_cast<std::uint64_t>(node->id), 0);
    if (node->boxDrawn || node->groupNode) {
        const bool wasExpanded = node->expanded;
        node->expanded = !wasExpanded;
        if (wasExpanded) {
            UpdateTreeNodesOnExpandCollapse(node, false, true);
            selectionDirty_ = true;
        }
        needsRecalc_ = true;
        InvalidateSelfAndParents();
        SendTreeMessage(node->expanded ? kGZMsgTreeViewExpand : kGZMsgTreeViewCollapse,
                        static_cast<std::uint64_t>(node->id), 0);
    }
    SendTreeMessage(kGZMsgTreeViewCommand, static_cast<std::uint64_t>(node->id), 0);
    return true;
}

bool cGZWinTreeView::GZOnMouseDownR(int x, int y, std::uint32_t) {   // @ 0x10051e4a0
    GZWinTreeView_TreeNode* node = GetNodeAtCursorPosition(x, y);
    if (!node) return false;
    SetNodeVisible(node->id);
    const int px = hScrollOffset_ - static_cast<int>(gutterX_) + x;
    const int py = y - static_cast<int>(gutterY_);
    auto inside = [px, py](const cRZRect& r) {
        return r.left <= px && r.top <= py && px <= r.right && py <= r.bottom;
    };
    if ((node->iconDrawn && inside(node->imageRect)) || (node->textDrawn && inside(node->textRect))) {
        AddSelection(node->id, true);
        SendTreeMessage(kGZMsgTreeViewRightClick, static_cast<std::uint64_t>(node->id), 0);
        SendTreeMessage(kGZMsgTreeViewCommand, static_cast<std::uint64_t>(node->id), 0);
        return true;
    }
    return false;
}

bool cGZWinTreeView::GZOnMouseMove(int x, int y, std::uint32_t flags) {   // @ 0x10051e5c2
    if ((flags & 0x10) == 0 || selection_.empty()) return true;
    GZWinTreeView_TreeNode* underCursor = GetNodeAtCursorPosition(x, y);
    if (underCursor && underCursor->selected) return true;
    // Ask the notification target whether a drag may start (message 0x1af5a140 carries a bool* in param2).
    bool allow = false;
    cGZMessage query;
    query.type = kGZMsgTreeViewQueryDrag;
    query.param1 = GetID();
    query.param2 = reinterpret_cast<std::uint64_t>(&allow);
    query.param3 = 0;
    const bool handled = notify_ ? notify_->DoMessage(query) : false;
    if (!allow || !handled) return true;
    // The image is the bounding box of the selected rows, drawn by DrawItem into a fresh buffer.
    int left = 0, right = 0;
    bool haveRect = false;
    cRZRect imageArea{0, 0, 0, 0};
    for (GZWinTreeView_TreeNode* node : selection_) {
        cRZRect r{0, 0, 0, 0};
        if (node->boxDrawn) r = node->boxRect;
        if (node->iconDrawn) r = haveRect ? UnionRect(r, node->imageRect) : node->imageRect;
        if (node->textDrawn) r = haveRect ? UnionRect(r, node->textRect) : node->textRect;
        if (node->boxDrawn || node->iconDrawn || node->textDrawn) {
            if (!haveRect) {
                imageArea = r;
                haveRect = true;
            } else {
                imageArea = UnionRect(imageArea, r);
            }
        }
    }
    left = imageArea.left;
    right = imageArea.right;
    (void)haveRect;
    const int imageWidth = std::max(right - left, 1);
    const int imageHeight = static_cast<int>(selection_.size() * rowHeight_);
    cIGZBuffer* image = TreeViewServices()->CreateBuffer();
    if (!image) return true;
    cGZBufferColorType type;
    type.type = kBufferColorDisplay;
    type.bitsPerPixel = 0;
    if (!image->Init(static_cast<std::uint32_t>(imageWidth), static_cast<std::uint32_t>(imageHeight), type)) {
        image->Release();
        return true;
    }
    if (image->Lock(static_cast<cIGZBuffer::eLockFlags>(kLockFill))) {
        image->Fill(0xffffffffu, nullptr);
        image->Unlock(static_cast<cIGZBuffer::eLockFlags>(kLockFill));
    }
    std::vector<GZWinTreeView_TreeNode*> dragged(selection_);
    ClearSelection();
    int offsetY = 0;
    for (GZWinTreeView_TreeNode* node : dragged) {
        DrawItem(image, node, 0, offsetY);
        offsetY += static_cast<int>(rowHeight_);
    }
    cIGZWin* dragWindow = nullptr;
    const bool dropped = TreeViewServices()->RunNodeDrag(*this, dragged, image, dragWindow);
    if (dropped && !selection_.empty()) {
        // The drop target is the node the drag window selected through NotifyDrag.
        const int first = selection_.front()->id;
        int parentOrNode = first;
        int insertAfter = -2;
        if (dragPreview_) {
            parentOrNode = GetNodeParent(first);
            GZWinTreeView_TreeNode* node = FindNode(first);
            GZWinTreeView_TreeNode* parent = FindNode(parentOrNode);
            if (parent && node) {
                for (auto it = parent->children.begin(); it != parent->children.end(); ++it) {
                    if (*it != node) continue;
                    if (it != parent->children.begin()) insertAfter = (*std::prev(it))->id;
                    break;
                }
            }
        }
        ClearSelection();
        for (GZWinTreeView_TreeNode* node : dragged) AddSelection(node->id, false);
        SendTreeMessage(kGZMsgTreeViewDropped, static_cast<std::uint64_t>(parentOrNode),
                        static_cast<std::uint64_t>(insertAfter));
    } else if (!dropped) {
        // The drag window is not available (the seam did not run it): restore the selection the original had
        // cleared for the drag image.
        ClearSelection();
        for (GZWinTreeView_TreeNode* node : dragged) AddSelection(node->id, false);
    }
    dragPreview_ = false;
    InvalidateSelfAndParents();
    image->Release();
    return true;
}

void cGZWinTreeView::NotifyDrag(std::vector<GZWinTreeView_TreeNode*>* dragList, int x, int y, bool unused) {   // @ 0x10051ee1a
    (void)unused;   // the fourth argument of the original is dead
    const int rowHeight = static_cast<int>(rowHeight_);
    const int hitY = (rowHeight >> 2) + y;
    GZWinTreeView_TreeNode* node = GetNodeAtCursorPosition(x, hitY);
    const std::uint32_t remainder = static_cast<std::uint32_t>(hitY - static_cast<int>(gutterY_)) /
                                    static_cast<std::uint32_t>(rowHeight);
    const bool upperHalf = remainder < static_cast<std::uint32_t>(rowHeight >> 1);
    GZWinTreeView_TreeNode* first = selection_.empty() ? nullptr : selection_.front();
    if (node == first && dragPreview_ == upperHalf) return;
    bool inDraggedTree = false;
    if (node && dragList) {
        for (GZWinTreeView_TreeNode* p = node; p && !inDraggedTree; p = p->parent)
            for (GZWinTreeView_TreeNode* dragged : *dragList)
                if (p == dragged) {
                    inDraggedTree = true;
                    break;
                }
    }
    if (!node || inDraggedTree) {
        dragPreview_ = upperHalf;
    } else {
        dragPreview_ = upperHalf;
        AddSelection(node->id, true);
    }
    InvalidateSelfAndParents();
}

// ---- keys ----------------------------------------------------------------------------------------------------------

bool cGZWinTreeView::GZOnKeyDown(std::uint32_t key, std::uint32_t) {   // @ 0x10051eb22
    if (key == 0xd) {   // Return: expand / collapse every selected node
        for (GZWinTreeView_TreeNode* node : selection_) {
            SendTreeMessage(kGZMsgTreeViewDoubleClick, static_cast<std::uint64_t>(node->id), 0);
            if (node->boxDrawn || node->groupNode) {
                const bool wasExpanded = node->expanded;
                node->expanded = !wasExpanded;
                if (wasExpanded) {
                    UpdateTreeNodesOnExpandCollapse(node, false, true);
                    selectionDirty_ = true;
                }
                needsRecalc_ = true;
                InvalidateSelfAndParents();
                SendTreeMessage(node->expanded ? kGZMsgTreeViewExpand : kGZMsgTreeViewCollapse,
                                static_cast<std::uint64_t>(node->id), 0);
            }
        }
        return true;
    }
    if (key == 0x1b) {   // Escape
        ClearSelection();
        return true;
    }
    int row = 0;
    if (!selection_.empty()) row = static_cast<int>(selection_.back()->rowIndex);
    switch (key) {
        case 0x21: row -= rowsPerPage_; break;                            // Page up
        case 0x22: row += rowsPerPage_; break;                            // Page down
        case 0x23: row = static_cast<int>(visibleRowCount_) - 1; break;   // End (the original falls into Up)
        case 0x24: break;                                                 // Home
        case 0x26: --row; break;                                          // Up
        case 0x28: ++row; break;                                          // Down
        case 0x25: {                                                      // Left: collapse or go to the parent
            if (!selection_.empty()) {
                GZWinTreeView_TreeNode* node = selection_.back();
                if (!node->expanded) {
                    if (node->parent) row = static_cast<int>(node->parent->rowIndex);
                } else {
                    SetNodeExpanded(node->id, false);
                }
            }
            break;
        }
        case 0x27: {                                                      // Right: expand or go to the next row
            if (!selection_.empty()) {
                GZWinTreeView_TreeNode* node = selection_.back();
                if (!node->expanded) SetNodeExpanded(node->id, true);
                else ++row;
            }
            break;
        }
        default:
            return true;
    }
    if (row < 0) row = 0;
    const int last = static_cast<int>(visibleRowCount_) - 1;
    if (row > last) row = last;
    if (row >= 0 && static_cast<std::size_t>(row) < visibleNodes_.size())
        AddSelection(visibleNodes_[static_cast<std::size_t>(row)]->id, true);
    return true;
}

bool cGZWinTreeView::GZOnKeyUp(std::uint32_t key, std::uint32_t modifiers) {   // @ 0x10051edaa
    return cGZWin::GZOnKeyUp(key, modifiers);
}

bool cGZWinTreeView::GZOnMouseWheel(int, int, std::uint32_t, int delta) {   // @ 0x10051edc2
    if (!vScrollbar_) return true;
    const int top = topRow_;
    int newTop = top;
    if (delta < 0) {
        const int page = rowsPerPage_ + top;
        if (static_cast<std::uint32_t>(visibleRowCount_) <= static_cast<std::uint32_t>(page)) return true;
        newTop = top + 1;
    } else {
        if (top == 0) return true;
        newTop = top - 1;
    }
    vScrollbar_->SetValue(newTop);
    return true;
}

}  // namespace gz
