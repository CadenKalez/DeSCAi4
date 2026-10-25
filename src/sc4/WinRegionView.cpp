#include "sc4/WinRegionView.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "gz/GZCOMDirector.h"
#include "gz/RZDraw.h"
#include "gz/WinGen.h"
#include "sc4/SC4Utilities.h"
#include "sc4/SCPropertyHelp.h"
#include "sc4/SLBuffer.h"
#include "sc4/SmallWinProcs.h"
#include "sc4/WinAuraAlert.h"

namespace sc4 {

namespace {

IRegionViewHost* g_host = nullptr;

// cIGZWin flags the view toggles (cIGZWin::tWinFlag; the SetFlag slot is 0x220).
constexpr std::uint32_t kFlagPrivateBuffer = 0x10000;
constexpr std::uint32_t kFlagCanFocus = 0x8000;

// 12582912.0f = 1.5 * 2^23: the original's float->int rounding bias. The two forms below are both in the class:
//  * roundss $9 (0x10075b6c5, 0x10075dae2, 0x10075ddc0, 0x10075db8a) is round toward -infinity (verified on x86_64),
//  * the bias added as an int (0x10075d808 / 0x10075d815, `addl $0xb4c00000`) rounds to nearest.
constexpr float kRoundBias = 12582912.0f;      // 0x4b400000
constexpr std::uint32_t kRoundBiasBitsInverse = 0xb4c00000u;   // -0x4b400000: the `addl $0xb4c00000` in 0x10075d808
constexpr float kMapToScreenA = -0.03661165f;    // 0x100a1b8xx rotation constants of the region map
constexpr float kMapToScreenB = 0.088388346f;
constexpr float kMapToScreenC = 0.044194173f;
constexpr float kMapToScreenD = 0.018305825f;
constexpr float kMapToScreenE = -0.08285339f;

// (int)(bits(value + 12582912.0f) + 0xb4c00000): the original's round-to-nearest via the float bias bits.
int RoundToInt(float value) {
    const float biased = value + kRoundBias;
    std::uint32_t bits = 0;
    std::memcpy(&bits, &biased, sizeof bits);
    bits += kRoundBiasBitsInverse;
    return static_cast<int>(bits);
}

template <class T>
void ReleasePtr(T*& pointer) {
    if (pointer) {
        pointer->Release();
        pointer = nullptr;
    }
}

// (anonymous namespace)::SetupScript @ 0x10075e634: walk the signpost's children, clear flag 0x8000 and point every
// button's notification target (iid 0x8810) at the view's notification target, then recurse.
bool SetupScript(cIGZWin* parent, std::uint32_t id, void* child, void* context) {
    (void)parent;
    (void)id;
    if (!child) return true;
    auto* window = static_cast<cIGZWin*>(child);
    window->SetFlag(kFlagCanFocus, false);
    cIGZUnknown* button = nullptr;
    if (window->QueryInterface(gz::kGZIID_cIGZWinBtn, reinterpret_cast<void**>(&button))) {
        window->SetNotificationTarget(static_cast<cIGZWin*>(context));
        if (button) button->Release();
    }
    window->EnumChildren(kGZIID_cIGZWin, SetupScript, context);
    return true;
}

}  // namespace

IRegionViewHost* RegionViewHost() { return g_host; }
void SetRegionViewHost(IRegionViewHost* host) { g_host = host; }

// ===================================================================================================================
// Construction / destruction
// ===================================================================================================================

cSC4WinRegionView::cSC4WinRegionView(cIGZWinMgr* mgr) : cGZWin(mgr) {   // @ 0x10075afc6
    // The original's store sequence: +0x160..+0x1a0 (six 8-byte zeros), the two option dwords, +0x1b0 and the icon
    // vector. The second vtable pointer at +0x150 is the compiler's.
    regionIndex_ = 0;   // quirk: the original never writes +0x158 (see the header comment)
    backdrop_ = nullptr;
    selected_ = nullptr;
    scrollX_ = 0;                       // +0x170
    scrollY_ = 0;                       // +0x174
    signpostType_ = 0;                  // +0x178
    signpostSubType_ = 0;               // +0x17c
    labelColour1_ = 0;                  // +0x180
    labelColour2_ = 0;                  // +0x184
    // 0x10075aff2: movl $0x00010001, 0x1a9 and movw $0x0100, 0x1ad.
    signpostMode_ = true;               // +0x1a9
    forcedSignposts_ = false;           // +0x1aa
    grid_ = true;                       // +0x1ab
    labels_ = false;                    // +0x1ac
    alwaysLabels_ = false;              // +0x1ad
    tutorialArrow_ = true;              // +0x1ae
    mapMode_ = 0;                       // +0x1b0
    scrollIndicator_ = false;           // +0x1a8
}

cSC4WinRegionView::~cSC4WinRegionView() {   // @ 0x10075b066
    Shutdown();
    // The original then destroys the icon vector, releases the canvas, frees the city vector and releases the backdrop.
    iconBuffers_.clear();
    ReleasePtr(canvas_);
    cities_.clear();
    ReleasePtr(backdrop_);
}

// Both vtable groups forward to the same cGZWin reference count (the +0x150 thunks subtract 0x150, 0x10075b196..).
bool cSC4WinRegionView::QueryInterface(std::uint32_t iid, void** out) {   // @ 0x10075b18c
    return cGZWin::QueryInterface(iid, out);
}

std::uint32_t cSC4WinRegionView::AddRef() {   // @ 0x10075b1a8
    return cGZWin::AddRef();
}

std::uint32_t cSC4WinRegionView::Release() {   // @ 0x10075b1c4
    return cGZWin::Release();
}

// ===================================================================================================================
// Init / Shutdown / area
// ===================================================================================================================

bool cSC4WinRegionView::Init() {   // @ 0x10075b1e0
    if (get_init_flag()) return true;
    cGZWin::Init();
    SetID(kRegionViewWindowId);   // vslot 0x200
    IRegionViewHost* host = RegionViewHost();
    ITiledScrollingCanvas* canvas = host ? host->CreateTiledScrollingCanvas() : nullptr;
    if (canvas_ != canvas) {
        if (canvas) canvas->AddRef();
        ITiledScrollingCanvas* previous = canvas_;
        canvas_ = canvas;
        ReleasePtr(previous);
    }
    if (!canvas_) return false;
    if (!canvas_->Init(GetW(), GetH(), 0x100, 0x100, this)) return false;
    if (host) host->AddRenderLayer(canvas_);
    SetFlag(kFlagPrivateBuffer, false);   // vslot 0x220(0x10000, 0)
    SetFlag(kFlagCanFocus, false);        // vslot 0x220(0x8000, 0)
    {
        cSLBuffer buffer(kSLBitmapResourceType, kRegionViewBackdropGroup, kRegionViewBackdropInstance, 1, false);
        if (cIGZBuffer* loaded = buffer.GetBuffer()) {
            loaded->AddRef();
            if (backdrop_) backdrop_->Release();
            backdrop_ = loaded;
        }
    }
    iconBuffers_.resize(2);
    for (int i = 0; i < 2; ++i) {
        cSLBuffer icon(kSLBitmapResourceType, kRegionViewIconGroup, kRegionViewIconInstance[i], 1, false);
        if (cIGZBuffer* loaded = icon.GetBuffer()) iconBuffers_[i] = loaded;
    }
    return true;
}

bool cSC4WinRegionView::Shutdown() {   // @ 0x10075b4f0
    if (!get_init_flag()) return true;
    ClearCities();
    iconBuffers_.clear();
    if (canvas_) {
        if (IRegionViewHost* host = RegionViewHost()) host->RemoveRenderLayer(canvas_);
        canvas_->Shutdown();
        ReleasePtr(canvas_);
    }
    ReleasePtr(backdrop_);
    cGZWin::Shutdown();
    return true;
}

void cSC4WinRegionView::SetArea(int l, int t, int r, int b) {   // @ 0x10075b652
    cGZWin::SetArea(l, t, r, b);
    // The original re-initialises the canvas in place (no Shutdown first).
    if (canvas_) canvas_->Init(GetW(), GetH(), 0x100, 0x100, this);
}

bool cSC4WinRegionView::GZPaint() {   // @ 0x10075bb5e
    return true;
}

// ===================================================================================================================
// Cities
// ===================================================================================================================

void cSC4WinRegionView::ClearCityEffectList(cSC4RegionViewCity& city) {
    for (cRZAutoRefCount<IRegionViewEffect>& effect : city.effects) {
        if (effect.get()) effect->Stop(0);   // vslot 0x20(0)
    }
    city.effects.clear();
}

void cSC4WinRegionView::ClearCities() {   // @ 0x10075b5c6
    selected_ = nullptr;
    for (cSC4RegionViewCity* city : cities_) {
        if (city) ClearCityEffectList(*city);
    }
    cities_.clear();
    InvalidateAllRegions();
}

void cSC4WinRegionView::InvalidateAllRegions() {   // @ 0x10075bb02
    if (canvas_) canvas_->InvalidateAllTiles();
    for (cSC4RegionViewCity* city : cities_) {
        if (city) city->dirty = 0;   // +0x50
    }
    if (IRegionViewHost* host = RegionViewHost()) host->InvalidateRenderer();
}

void cSC4WinRegionView::InvalidateRegion(cSC4RegionViewCity& city) {   // @ 0x10075b6b0
    if (!city.image) return;
    const float fx = std::floor(city.mapX);   // roundss $9
    const float fy = std::floor(city.mapY);
    const int x = static_cast<int>(fx - static_cast<float>(scrollX_)) + scrollX_;
    const float localY = fy - static_cast<float>(scrollY_);
    const int y = static_cast<int>(localY - static_cast<float>(city.image->Height())) + scrollY_;
    const cRZRect* area = city.image->GetBufferArea();
    cRZRect rect;
    rect.left = x - area->left;
    rect.top = y - area->top;
    rect.right = rect.left + (area->right - area->left);
    rect.bottom = rect.top + (area->bottom - area->top);
    if (canvas_) canvas_->InvalidateRect(rect);
    if (IRegionViewHost* host = RegionViewHost()) host->InvalidateRenderer();
    UpdateSignpost(city);
}

void cSC4WinRegionView::PositionSignpost(cSC4RegionViewCity& city) {
    cIGZWin* signpost = city.signpost;
    if (!signpost) return;
    cRZRect rect;
    GetRegionScreenRect(city, rect);
    // Quirk: the original centres x with half the width but subtracts the whole height for y (0x10075d9dc).
    signpost->GZWinMoveTo((rect.right + rect.left) / 2 - signpost->GetW() / 2,
                          (rect.bottom + rect.top) / 2 - signpost->GetH());
}

void cSC4WinRegionView::AddCity(cSC4RegionViewCity* city) {   // @ 0x10075d58c
    if (!city) return;
    cities_.push_back(city);
    InvalidateRegion(*city);
    UpdateCity(city);
}

void cSC4WinRegionView::UpdateCity(cSC4RegionViewCity* city) {   // @ 0x10075d5e6
    if (!city) return;
    city->dirty = 0;                     // +0x50
    city->icons.clear();                 // +0xd8 end = begin
    ClearCityEffectList(*city);
    InvalidateAllRegions();
    IRegionViewHost* host = RegionViewHost();
    IRegionCityView* regional = host ? host->FindRegionalCity(regionIndex_, city->x, city->z) : nullptr;
    if (!regional) return;
    const float baseX = static_cast<float>(city->x << 10);
    const float baseY = static_cast<float>(city->z << 10);
    const float offsetX = baseY * kMapToScreenA + baseX * kMapToScreenB;
    const float offsetY = baseY * kMapToScreenC + baseX * kMapToScreenD;
    for (int type = 0; type < 2; ++type) {
        if (!iconBuffers_[type].get()) continue;
        std::vector<RegionalCityPointOfInterest> points;
        regional->GetPointsOfInterest(static_cast<int>(kRegionViewIconEffectType[type]), points);
        for (const RegionalCityPointOfInterest& point : points) {
            RegionViewCityIcon icon;
            icon.x = RoundToInt(point.z * kMapToScreenA + point.y * 0.0f + point.x * kMapToScreenB + offsetX);
            icon.y = RoundToInt(point.z * kMapToScreenC + point.y * kMapToScreenE + point.x * kMapToScreenD + offsetY);
            icon.image = iconBuffers_[type].get();
            city->icons.push_back(icon);
            IRegionViewEffect* effect = nullptr;
            if (host && host->CreateRegionEffect(kRegionViewIconName[type], &effect) && effect) {
                cS3DTransform transform;
                transform.changeCounter = 1;
                transform.flags = 1;
                transform.translation[0] = point.x + baseX;
                transform.translation[1] = point.y + 0.0f;
                transform.translation[2] = point.z + baseY;
                effect->SetEffectTransform(transform);   // vslot 0x38
                effect->Start(0);                        // vslot 0x18
                city->effects.emplace_back(effect);
                effect->Release();   // the list holds the counted reference
            }
        }
    }
}

// ===================================================================================================================
// Rendering
// ===================================================================================================================

void cSC4WinRegionView::RenderTile(cIGZBuffer* buffer, int x, int y) {   // @ 0x10075bb66 (PARTIAL)
    if (!buffer) return;
    if (backdrop_) {
        const cRZRect* source = backdrop_->GetBufferArea();
        const cRZRect* destination = buffer->GetBufferArea();
        RZDrawBltTiled(buffer, backdrop_, *source, *destination, -(scrollX_ + x), -(scrollY_ + y), nullptr);
    } else {
        buffer->Fill(buffer->ConvertRGBValueToNative(0, 0, 0), static_cast<const cRZRect*>(nullptr));
    }
    // The cities, back to front, each blended over the tile at its map position (the original's loop, 0x10075bbf0, clips the city's
    // rectangle against the tile first). PARTIAL: the original blends through the mask's transparent-run list (growing the
    // semi-transparent edge, +0x58) and then draws the selected city's grid, the label windows and the airport / seaport icons
    // (to 0x10075d4fc); here the picture's own alpha is blended and nothing else is drawn.
    if (cities_.empty() || !buffer->Lock(cIGZBuffer::kLockWrite)) return;
    auto* destination = static_cast<std::uint8_t*>(buffer->GetColorSurfaceBits());
    const std::uint32_t destinationPitch = buffer->GetColorSurfaceStride();
    const cRZRect tileArea = *buffer->GetBufferArea();
    for (cSC4RegionViewCity* city : cities_) {
        if (!city || !city->image) continue;
        cRZRect rect;
        GetRegionScreenRect(*city, rect);   // in view coordinates, i.e. canvas coordinates
        const int left = rect.left - x, top = rect.top - y;
        const int width = rect.right - rect.left, height = rect.bottom - rect.top;
        const int x0 = std::max(left, tileArea.left), y0 = std::max(top, tileArea.top);
        const int x1 = std::min(left + width, tileArea.right), y1 = std::min(top + height, tileArea.bottom);
        if (x0 >= x1 || y0 >= y1 || !city->image->Lock(cIGZBuffer::kLockRead)) continue;
        const auto* source = static_cast<const std::uint8_t*>(city->image->GetColorSurfaceBits());
        const std::uint32_t sourcePitch = city->image->GetColorSurfaceStride();
        for (int row = y0; row < y1; ++row) {
            const auto* from = reinterpret_cast<const std::uint32_t*>(source + static_cast<std::size_t>(row - top) * sourcePitch) + (x0 - left);
            auto* to = reinterpret_cast<std::uint32_t*>(destination + static_cast<std::size_t>(row) * destinationPitch) + x0;
            for (int col = x0; col < x1; ++col, ++from, ++to) {
                const std::uint32_t pixel = *from, alpha = pixel >> 24;
                if (alpha == 0) continue;
                if (alpha == 255) { *to = pixel | 0xff000000u; continue; }
                const std::uint32_t back = *to;
                std::uint32_t out = 0xff000000u;
                for (int shift = 0; shift <= 16; shift += 8)
                    out |= ((((pixel >> shift) & 0xff) * alpha + ((back >> shift) & 0xff) * (255 - alpha) + 127) / 255) << shift;
                *to = out;
            }
        }
        city->image->Unlock(cIGZBuffer::kLockRead);
    }
    buffer->Unlock(cIGZBuffer::kLockWrite);
}

void cSC4WinRegionView::GetRegionScreenRect(const cSC4RegionViewCity& city, cRZRect& out) const {   // @ 0x10075dae2
    const float fx = std::floor(city.mapX);
    out.left = static_cast<int>(fx - static_cast<float>(scrollX_));
    const float fy = std::floor(city.mapY);
    const std::uint32_t height = city.image ? city.image->Height() : 0;
    out.top = static_cast<int>((fy - static_cast<float>(scrollY_)) - static_cast<float>(height));
    const cRZRect* area = city.image ? city.image->GetBufferArea() : nullptr;
    const int width = area ? area->right - area->left : 0;
    const int areaHeight = area ? area->bottom - area->top : 0;
    out.right = out.left + width;
    out.bottom = out.top + areaHeight;
}

void cSC4WinRegionView::GetRegionPlayfieldRect(const cSC4RegionViewCity& city, cRZRect& out) const {   // @ 0x10075ddc0
    const float fx = std::floor(city.mapX);
    out.left = static_cast<int>(fx - static_cast<float>(scrollX_));
    const float fy = std::floor(city.mapY);
    const std::uint32_t height = city.image ? city.image->Height() : 0;
    int top = static_cast<int>((fy - static_cast<float>(scrollY_)) - static_cast<float>(height));
    const int left = out.left;
    out.left = left + scrollX_;
    out.top = top + scrollY_;
    const cRZRect* area = city.image ? city.image->GetBufferArea() : nullptr;
    const int width = area ? area->right - area->left : 0;
    const int areaHeight = area ? area->bottom - area->top : 0;
    out.right = left + scrollX_ + width;
    out.bottom = out.top + areaHeight;
}

// ===================================================================================================================
// Hit testing / selection
// ===================================================================================================================

cSC4RegionViewCity* cSC4WinRegionView::GetCityAtPoint(int x, int y) {   // @ 0x10075db8a
    for (std::size_t index = cities_.size(); index > 0; --index) {   // last city first (topmost)
        cSC4RegionViewCity* city = cities_[index - 1];
        if (!city || !city->image) continue;
        const int relX = x - static_cast<int>(std::floor(city->mapX) - static_cast<float>(scrollX_));
        const std::uint32_t height = city->image->Height();
        const int relY = y - static_cast<int>((std::floor(city->mapY) - static_cast<float>(scrollY_)) -
                                              static_cast<float>(height));
        const cRZRect* area = city->image->GetBufferArea();
        if (area->left > relX || relX >= area->right) continue;
        if (area->top > relY || relY >= area->bottom) continue;
        // lower_bound over the packed span list: inside when the found index is odd (a span start is even).
        const std::uint32_t key = static_cast<std::uint32_t>(relX + 1 + relY * 0x10000);
        const std::vector<std::uint32_t>& spans = city->hitSpans;
        const std::ptrdiff_t found =
            std::lower_bound(spans.begin(), spans.end(), key) - spans.begin();
        if ((found & 1) != 0) return city;
    }
    return nullptr;
}

void cSC4WinRegionView::ClearSelection() {   // @ 0x10075dce8
    cSC4RegionViewCity* previous = selected_;
    selected_ = nullptr;
    if (previous) InvalidateRegion(*previous);
}

void cSC4WinRegionView::SelectCity(int x, int y) {   // @ 0x10075dd66
    cSC4RegionViewCity* found = GetCityAtPoint(x, y);
    cSC4RegionViewCity* previous = selected_;
    if (found == previous) return;
    selected_ = found;
    if (found) InvalidateRegion(*found);
    if (previous) InvalidateRegion(*previous);
}

cSC4RegionViewCity* cSC4WinRegionView::GetSelectedCity() const {   // @ 0x10075ddb2
    return selected_;
}

// ===================================================================================================================
// Options / scrolling
// ===================================================================================================================

void cSC4WinRegionView::SetSignpostMode(bool mode, bool forced) {   // @ 0x10075dd0a
    signpostMode_ = mode;
    forcedSignposts_ = forced;
}

void cSC4WinRegionView::EnableLabels(bool labels, bool always) {   // @ 0x10075dd1e
    labels_ = labels;
    alwaysLabels_ = always;
    InvalidateAllRegions();
}

void cSC4WinRegionView::EnableGrid(bool on) {   // @ 0x10075dd36
    grid_ = on;
    InvalidateAllRegions();
}

void cSC4WinRegionView::EnableTutorialArrow(bool on) {   // @ 0x10075dd48
    tutorialArrow_ = on;
}

void cSC4WinRegionView::SetMapMode(int mode) {   // @ 0x10075dd56
    mapMode_ = mode;
    InvalidateAllRegions();
}

void cSC4WinRegionView::SetScrollPos(int x, int y) {   // @ 0x10075d9dc
    if (scrollX_ == x && scrollY_ == y) return;
    scrollX_ = x;
    scrollY_ = y;
    for (cSC4RegionViewCity* city : cities_) {
        if (!city || !city->signpost) continue;
        cRZRect rect;
        GetRegionScreenRect(*city, rect);
        cIGZWin* signpost = city->signpost;
        signpost->GZWinMoveTo((rect.right + rect.left) / 2 - signpost->GetW() / 2,
                              (rect.bottom + rect.top) / 2 - signpost->GetH());
    }
    if (canvas_) canvas_->ScrollTo(scrollX_, scrollY_);
}

void cSC4WinRegionView::SetSignpostType(std::uint32_t type, std::uint32_t sub) {   // @ 0x10075de92
    signpostType_ = type;
    signpostSubType_ = sub;
    for (cSC4RegionViewCity* city : cities_) {
        if (city) UpdateSignpost(*city);   // the original does not null-check
    }
}

bool cSC4WinRegionView::is_point_transparent(int x, int y) const {   // @ 0x10075e6ac
    int screenX = x;
    int screenY = y;
    WindowToScreenCoordinates(screenX, screenY);   // vslot 0x1e0
    IRegionViewHost* host = RegionViewHost();
    cIGZWin* child = host ? host->ChildWindowForCursorPos(const_cast<cSC4WinRegionView*>(this), screenX, screenY)
                          : nullptr;
    return child == nullptr;
}

// ===================================================================================================================
// Signposts
// ===================================================================================================================

void cSC4WinRegionView::filter_tutorial_mayor_name(IRegionCityView* city, cIGZString* name) const {   // @ 0x10075d4fc
    if (!city || !name) return;
    if (!city->IsTutorial()) return;
    IRegionViewHost* host = RegionViewHost();
    if (!host) return;
    const int guid = city->GetTutorialGUID();
    if (host->TutorialItemHidesMayorName(guid)) name->Resize(0);   // cIGZString vslot 0x88
}

void cSC4WinRegionView::UpdateSignpostChildText(cIGZWin* signpost, std::uint32_t id, const cIGZString& text) {
    if (!signpost) return;
    if (cIGZWin* child = signpost->GetChildWindowFromIDRecursive(id)) child->SetCaption(text);
}

void cSC4WinRegionView::UpdateSignpostChildFlag(cIGZWin* signpost, std::uint32_t id, int flag, bool on) {
    if (!signpost) return;
    if (cIGZWin* child = signpost->GetChildWindowFromIDRecursive(id)) child->SetFlag(static_cast<std::uint32_t>(flag), on);
}

void cSC4WinRegionView::UpdateSignpost(cSC4RegionViewCity& city) {   // @ 0x10075b7a6
    if (city.signpost) {
        ChildDelete(city.signpost);   // vslot 0x80: deletes the child window
        cIGZWin* previous = city.signpost;
        city.signpost = nullptr;
        ReleasePtr(previous);
    }
    int tutorialX = -1;
    int tutorialZ = -1;
    IRegionViewHost* host = RegionViewHost();
    if (host) {
        const int item = host->NextActiveTutorialCity();
        if (item >= 0) {
            if (IRegionCityView* tutorialCity = host->TutorialCity(item)) tutorialCity->GetPosition(tutorialX, tutorialZ);
        }
    }
    // The decompiled condition, including the `bVar3 = true` set inside the comma expression (0x10075b884..0x10075b8d4).
    bool tutorialGhost = false;
    const bool special = !tutorialArrow_ || tutorialX != city.x || (tutorialX | tutorialZ) < 0 || tutorialZ != city.z ||
                         selected_ == &city;
    if (special) {
        if (signpostType_ == 0) return;
        if (!signpostMode_ && selected_ != &city) return;
        tutorialGhost = false;
    } else {
        tutorialGhost = true;
    }
    IRegionCityView* regional = host ? host->FindRegionalCity(regionIndex_, city.x, city.z) : nullptr;
    if (!((regional && (tutorialGhost || regional->GetEstablished())) || forcedSignposts_)) return;
    cGZPersistResourceKey key{kRegionViewSignpostKeyType, 0, kRegionViewSignpostKeyInstance};
    std::uint32_t windowId = kRegionViewSignpostWindowId;
    if (!tutorialGhost) {
        key.instance = signpostType_;
        windowId = signpostSubType_;
    }
    // The original asks RZGetFramework for the window manager first, to save / restore the focus around the script.
    cIGZWinMgr* manager = nullptr;
    cIGZWin* oldFocus = nullptr;
    if (cIGZFrameWork* framework = RZGetFramework()) {
        // cGZWinMgrBase service id 0xa417445e, iid 0x5a4 (cIGZWinMgr), gz/WinMgrBase.h.
        if (framework->GetSystemService(0xa417445e, 0x5a4, reinterpret_cast<void**>(&manager)) && manager) {
            oldFocus = manager->GZGetFocus();
        }
    }
    cIGZWin* window = host ? host->CreateWindowFromScript(key, this, windowId) : nullptr;
    if (city.signpost != window) {
        if (window) window->AddRef();
        cIGZWin* previous = city.signpost;
        city.signpost = window;
        ReleasePtr(previous);
    }
    if (window) {
        window->Release();   // the local reference; the city holds its own
        InitSignpost(&city, regional);
        window = city.signpost;
        cIGZWin* context = GetNotificationTarget();
        if (window) {
            window->EnumChildren(kGZIID_cIGZWin, SetupScript, context);   // vslot 0x100
            gz::cIGZWinGen* gen = nullptr;
            if (window->QueryInterface(gz::kGZIID_cIGZWinGen, reinterpret_cast<void**>(&gen))) {
                auto* animator = new cSC4RegionSignpostAnimatorWinProc();
                gen->SetWinProc(animator);   // vslot 0x38; SetWinProc takes its own reference
                gen->Release();
            }
        }
    }
    if (manager && oldFocus && manager->IsWindowValid(oldFocus)) manager->GZSetFocus(oldFocus);
}

void cSC4WinRegionView::InitSignpost(cSC4RegionViewCity* city, IRegionCityView* regional) {   // @ 0x10075dee0
    if (!city || !city->signpost) return;
    cIGZWin* signpost = city->signpost;
    PositionSignpost(*city);
    double barValue = 0.5;
    bool done = false;
    if (regional && regional->GetEstablished()) {
        regional->AddRef();
        cRZString cityName;
        cRZString mayorName;
        regional->GetCityName(cityName);
        regional->GetMayorName(mayorName);
        filter_tutorial_mayor_name(regional, &mayorName);
        UpdateSignpostChildText(signpost, kSignpostCityNameId, cityName);
        UpdateSignpostChildText(signpost, kSignpostMayorNameId, mayorName);
        const int difficulty = regional->GetDifficultyLevel();
        UpdateSignpostChildFlag(signpost, kSignpostDifficultyLowId, 1, difficulty == 0);
        UpdateSignpostChildFlag(signpost, kSignpostDifficultyMediumId, 1, difficulty == 1);
        UpdateSignpostChildFlag(signpost, kSignpostDifficultyHighId, 1, difficulty == 2);
        cIGZWin* budgetWindow = signpost->GetChildWindowFromIDRecursive(kSignpostBudgetId);
        cIGZWin* negativeWindow = signpost->GetChildWindowFromIDRecursive(kSignpostBudgetNegativeId);
        cRZString money;
        const double budget = regional->GetBudget();
        SC4MakeMoneyString(static_cast<std::int64_t>(budget), money);
        if (!negativeWindow || budget >= 0.0) {
            if (negativeWindow) negativeWindow->HideWindow();
            if (budgetWindow) {
                budgetWindow->SetCaption(money);
                budgetWindow->ShowWindow();
            }
        } else {
            if (budgetWindow) budgetWindow->HideWindow();
            negativeWindow->SetCaption(money);
            negativeWindow->ShowWindow();
        }
        if (ILanguageFormatter* formatter = GetLanguageFormatter()) {
            cRZString text;
            if (signpost->GetChildWindowFromIDRecursive(kSignpostPopulationId)) {
                formatter->FormatNumber(regional->GetPopulation(), text);
                UpdateSignpostChildText(signpost, kSignpostPopulationId, text);
            }
            if (signpost->GetChildWindowFromIDRecursive(kSignpostCommercialId)) {
                formatter->FormatNumber(regional->GetCommercialJobs(), text);
                UpdateSignpostChildText(signpost, kSignpostCommercialId, text);
            }
            if (signpost->GetChildWindowFromIDRecursive(kSignpostIndustrialId)) {
                formatter->FormatNumber(regional->GetIndustrialJobs(), text);
                UpdateSignpostChildText(signpost, kSignpostIndustrialId, text);
            }
        }
        barValue = static_cast<double>(regional->GetMayorRating()) / 200.0 + 0.5;
        regional->Release();
        done = true;
    }
    if (!done) {
        std::int32_t budget = 10000;
        IRegionViewHost* host = RegionViewHost();
        if (host) {
            if (cISCPropertyHolder* holder = host->SignpostTuning()) {
                cSCPropertyHelp::GetPropertyValue(holder, kRegionViewSignpostBudgetProperty, budget);
            }
        }
        cRZString money;
        SC4MakeMoneyString(budget, money);
        UpdateSignpostChildText(signpost, kSignpostBudgetId, money);
        if (!regional) UpdateSignpostChildFlag(signpost, kSignpostEmptyBudgetId, 2, false);
        barValue = 0.5;
    }
    cISC4WinAuraBar* bar = nullptr;
    if (signpost->GetChildAsRecursive(kRegionViewAuraBarId, kGZIID_cISC4WinAuraBar, reinterpret_cast<void**>(&bar)) &&
        bar) {
        bar->SetValue(barValue);   // vslot 0x20
        cSLBuffer image(0, kRegionViewBarImageGroup, kRegionViewBarImageInstance, 9, false);
        if (cIGZBuffer* loaded = image.GetBuffer()) bar->SetImage(loaded);   // vslot 0x18
        bar->Release();
    }
}

// ===================================================================================================================
// Test / hook-up access
// ===================================================================================================================

void cSC4WinRegionView::SetBackdrop(cIGZBuffer* buffer) {
    if (buffer) buffer->AddRef();
    if (backdrop_) backdrop_->Release();
    backdrop_ = buffer;
}

void cSC4WinRegionView::SetCanvas(ITiledScrollingCanvas* canvas) {
    if (canvas) canvas->AddRef();
    if (canvas_) canvas_->Release();
    canvas_ = canvas;
}

}  // namespace sc4
