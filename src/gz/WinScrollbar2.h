// cGZWinScrollbar2 — the newer, image-skinned scrollbar (0x10069fd10..0x1006a1f1a, 45 functions). It is a plain
// window (not a slider): a value in [min, max - visibleRange] with a thumb whose length is the visible fraction of
// the range, arrow buttons at both ends, page clicks, and repeat scrolling driven by a callback timer that posts
// message 0x8b443261 to the window itself. The skin is three images: arrows (two cells side by side, four states
// stacked: disabled / normal / hover / pressed), container (end caps + tiled middle) and thumb (same layout as the
// arrows' states). Without skins it is drawn flat from raised rectangles and triangles.
//
// Orientation 0 is horizontal, 1 vertical (the default; only a value other than 1 is ever accepted by
// SetOrientation). Parts: 1 first arrow, 2 page before the thumb, 5 thumb, 3 page after, 4 last arrow.
//
// Layout (0x1f0 bytes): cGZWin (0x150), +0x150 cIGZWinScrollbar2 view (iid 0x4b42da53), +0x158 value, +0x15c min,
// +0x160 max, +0x164 visible range, +0x168 page overlap, +0x16c increment, +0x170 arrows / +0x178 container /
// +0x180 thumb images, +0x188 arrow length, +0x18c container cap, +0x190 container half, +0x194 thumb cap,
// +0x198 thumb half, +0x19c / +0x1a0 minimum width / height, +0x1a4 thumb offset, +0x1a8 thumb length, +0x1ac
// drag anchor, +0x1b0 dirty flags (1 images, 2 positions), +0x1b1 orientation, +0x1b2 hovered part, +0x1b3
// pressed part, +0x1b8 repeat timer.
#pragma once
#include "gz/Buffer.h"
#include "gz/DrawContext.h"
#include "gz/Win.h"
#include "rz/cRZCallbackTimer.h"

namespace gz {

constexpr std::uint32_t kGZIID_cIGZWinScrollbar2 = 0x4b42da53;
constexpr std::uint32_t kGZMsgScrollbarChanged = 0x4b42dcc7;       // sent to the notification target: p1 window, p2 id, p3 value
constexpr std::uint32_t kGZMsgScrollbarRepeat = 0x8b443261;        // the repeat timer's tick, posted to the window itself
constexpr std::uint32_t kScrollbar2ImageIds[2][3] = {              // arrows / container / thumb for orientation 0 and 1
    {0x4bd79360, 0x8bd79361, 0x0bd79363}, {0x0bd7935f, 0xcbd79361, 0x4bd79362}};

struct cRZSize { std::int32_t width = 0, height = 0; };

// RZ::WCM() vslot 0x38: the window colour manager's skin images (borrowed pointer, null when unknown).
class IWinImageSource {
public:
    virtual ~IWinImageSource() = default;
    virtual cIGZBuffer* GetImage(std::uint32_t imageId) = 0;
};
IWinImageSource* WinImageSource();
void SetWinImageSource(IWinImageSource* source);

// cIGZWinScrollbar2 (iid 0x4b42da53): slot n at 8n.
class cIGZWinScrollbar2 : public cIGZUnknown {
public:
    virtual cIGZWin* AsIGZWin() = 0;                                  // [3]
    virtual bool SetOrientation(int orientation) = 0;                 // [4]
    virtual bool GetOrientation(int& orientation) const = 0;          // [5]
    virtual bool SetValue(int value, bool notify) = 0;                // [6]
    virtual int GetValue() const = 0;                                 // [7]
    virtual bool GetValue(int& value) const = 0;                      // [8]
    virtual bool SetMinimumValue(int value) = 0;                      // [9]
    virtual bool GetMinimumValue(int& value) const = 0;               // [10]
    virtual bool SetMaximumValue(int value) = 0;                      // [11]
    virtual bool GetMaximumValue(int& value) const = 0;               // [12]
    virtual bool SetVisibleRange(int range) = 0;                      // [13]
    virtual bool GetVisibleRange(int& range) const = 0;               // [14]
    virtual bool SetPageOverlapRange(int range) = 0;                  // [15]
    virtual bool GetPageOverlapRange(int& range) const = 0;           // [16]
    virtual bool SetIncrementDelta(int delta) = 0;                    // [17]
    virtual bool GetIncrementDelta(int& delta) const = 0;             // [18]
    virtual bool SetArrowsImage(cIGZBuffer* image) = 0;               // [19]
    virtual cIGZBuffer* GetArrowsImage() const = 0;                   // [20]
    virtual bool SetContainerImage(cIGZBuffer* image) = 0;            // [21]
    virtual cIGZBuffer* GetContainerImage() const = 0;                // [22]
    virtual bool SetThumbImage(cIGZBuffer* image) = 0;                // [23]
    virtual cIGZBuffer* GetThumbImage() const = 0;                    // [24]
    virtual bool GetMinimumSize(cRZSize& size) const = 0;             // [25]
};

class cGZWinScrollbar2 : public cGZWin, public cIGZWinScrollbar2 {
public:
    explicit cGZWinScrollbar2(cIGZWinMgr* mgr);
    ~cGZWinScrollbar2() override;
    static cGZWinScrollbar2* CreateNewObject(cIGZWinMgr* mgr) { return new cGZWinScrollbar2(mgr); }

    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return cGZWin::AddRef(); }
    std::uint32_t Release() override { return cGZWin::Release(); }

    bool DoMessage(cGZMessage& message) override;
    bool Init() override;
    bool Shutdown() override;
    void SetArea(int l, int t, int r, int b) override;
    void SetArea(const cRZRect& area) override { SetArea(area.left, area.top, area.right, area.bottom); }
    bool GZPaint() override;
    bool GZOnMouseDownL(int x, int y, std::uint32_t flags) override;
    bool GZOnMouseUpL(int x, int y, std::uint32_t flags) override;
    bool GZOnMouseMove(int x, int y, std::uint32_t flags) override;
    bool GZOnMouseExit(cIGZWin* next) override;

    cIGZWin* AsIGZWin() override { return this; }
    bool SetOrientation(int orientation) override;
    bool GetOrientation(int& orientation) const override { orientation = orientation_; return true; }
    bool SetValue(int value, bool notify) override;
    int GetValue() const override { return value_; }
    bool GetValue(int& value) const override { value = value_; return true; }
    bool SetMinimumValue(int value) override;
    bool GetMinimumValue(int& value) const override { value = min_; return true; }
    bool SetMaximumValue(int value) override;
    bool GetMaximumValue(int& value) const override { value = max_; return true; }
    bool SetVisibleRange(int range) override;
    bool GetVisibleRange(int& range) const override { range = visible_; return true; }
    bool SetPageOverlapRange(int range) override { overlap_ = range < 0 ? 0 : range; return true; }
    bool GetPageOverlapRange(int& range) const override { range = overlap_; return true; }
    bool SetIncrementDelta(int delta) override { increment_ = delta < 0 ? 0 : delta; return true; }
    bool GetIncrementDelta(int& delta) const override { delta = increment_; return true; }
    bool SetArrowsImage(cIGZBuffer* image) override { return SetImage(arrows_, image); }
    cIGZBuffer* GetArrowsImage() const override { return arrows_; }
    bool SetContainerImage(cIGZBuffer* image) override { return SetImage(container_, image); }
    cIGZBuffer* GetContainerImage() const override { return container_; }
    bool SetThumbImage(cIGZBuffer* image) override { return SetImage(thumb_, image); }
    cIGZBuffer* GetThumbImage() const override { return thumb_; }
    bool GetMinimumSize(cRZSize& size) const override;

    // for tests
    int HoveredPart() const { return hovered_; }
    int PressedPart() const { return pressed_; }
    int ThumbOffset() { if (dirty_ & 2) CalcPositions(); return thumbOffset_; }
    int ThumbLength() { if (dirty_ & 2) CalcPositions(); return thumbLength_; }
    int ArrowLength() const { return arrowLength_; }
    cRZCallbackTimer& RepeatTimer() { return timer_; }
    int PickPart(int x, int y);

protected:
    void CalcImageSizes();
    void CalcPositions();

private:
    bool SetImage(cIGZBuffer*& slot, cIGZBuffer* image);
    int PageStep() const;
    static void TimerCallback(void* self);
    void PaintSkinned();
    void PaintFlat();

    std::int32_t value_ = 0, min_ = 0, max_ = 0x100, visible_ = 0x10;   // +0x158..+0x164
    std::int32_t overlap_ = 1, increment_ = 1;       // +0x168, +0x16c
    cIGZBuffer* arrows_ = nullptr;                   // +0x170
    cIGZBuffer* container_ = nullptr;                // +0x178
    cIGZBuffer* thumb_ = nullptr;                    // +0x180
    std::int32_t arrowLength_ = 0, containerCap_ = 0, containerHalf_ = 0, thumbCap_ = 0, thumbHalf_ = 0;   // +0x188..+0x198
    std::int32_t minWidth_ = 0, minHeight_ = 0;      // +0x19c, +0x1a0
    std::int32_t thumbOffset_ = 0, thumbLength_ = 1; // +0x1a4, +0x1a8
    std::int32_t dragAnchor_ = 0;                    // +0x1ac
    std::uint8_t dirty_ = 0;                         // +0x1b0
    std::uint8_t orientation_ = 1;                   // +0x1b1
    std::uint8_t hovered_ = 0, pressed_ = 0;         // +0x1b2, +0x1b3
    cRZCallbackTimer timer_;                         // +0x1b8
};

}  // namespace gz
