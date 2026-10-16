// cGZWinSlider — a draggable thumb on a track (0x10027b750..0x10027c786, 52 functions). Layout: the 0x150-byte
// cGZWin, +0x150 the cIGZWinSlider view (iid 0x21325207), +0x158 direction (1 horizontal, 2 vertical; default 2),
// +0x15c / +0x160 minimum / maximum (0 / 0x20), +0x164 position as a fraction, +0x168 / +0x170 first / last thumb
// position along the track, +0x16c current thumb position, +0x174 track margin, +0x178..+0x184 thumb rectangle,
// +0x188 re-entrancy guard, +0x190 image (4 segments: end cap, tiled middle, end cap, thumb), +0x198..+0x1a4
// the segment rectangle.
//
// Messages sent to the notification target: command (3) with 0x887113a3 (value changed) and 0x887113a4
// (thumb released).
#pragma once
#include "gz/Buffer.h"
#include "gz/Win.h"

namespace gz {

constexpr std::uint32_t kGZIID_cIGZWinSlider = 0x21325207;
constexpr std::uint32_t kGZSliderValueChanged = 0x887113a3;
constexpr std::uint32_t kGZSliderReleased = 0x887113a4;
enum tSliderWindowDirection : std::int32_t { kSliderHorizontal = 1, kSliderVertical = 2 };

class cIGZWinSlider : public cIGZUnknown {
public:
    virtual cIGZWin* AsIGZWin() = 0;                                              // [3]
    virtual int GetValue() const = 0;                                             // [4]
    virtual bool SetValue(int value) = 0;                                         // [5]
    virtual void SetMinimumValue(int value) = 0;                                  // [6]
    virtual int GetMinimumValue() const = 0;                                      // [7]
    virtual void SetMaximumValue(int value) = 0;                                  // [8]
    virtual int GetMaximumValue() const = 0;                                      // [9]
    virtual void SetDirection(tSliderWindowDirection direction) = 0;             // [10]
    virtual void GetDirection(tSliderWindowDirection& direction) const = 0;      // [11]
    virtual bool SetImage(cIGZBuffer* image, const cRZRect* area) = 0;            // [12]
    virtual bool GetImage(cIGZBuffer*& image, cRZRect& area) const = 0;           // [13]
};

class cGZWinSlider : public cGZWin, public cIGZWinSlider {
public:
    explicit cGZWinSlider(cIGZWinMgr* mgr);
    ~cGZWinSlider() override;
    static cGZWinSlider* CreateNewObject(cIGZWinMgr* mgr) { return new cGZWinSlider(mgr); }

    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return cGZWin::AddRef(); }
    std::uint32_t Release() override { return cGZWin::Release(); }

    bool Init() override;
    bool Shutdown() override;
    void SetArea(int l, int t, int r, int b) override;
    void SetArea(const cRZRect& area) override { SetArea(area.left, area.top, area.right, area.bottom); }
    bool GZPaint() override;
    bool GZOnMouseDownL(int x, int y, std::uint32_t flags) override;
    bool GZOnMouseUpL(int x, int y, std::uint32_t flags) override;
    bool GZOnMouseMove(int x, int y, std::uint32_t flags) override;
    bool GZOnKeyDown(std::uint32_t key, std::uint32_t flags) override;
    bool GZOnKeyUp(std::uint32_t key, std::uint32_t flags) override;

    cIGZWin* AsIGZWin() override { return this; }
    int GetValue() const override;
    bool SetValue(int value) override;
    void SetMinimumValue(int value) override;
    int GetMinimumValue() const override { return min_; }
    void SetMaximumValue(int value) override;
    int GetMaximumValue() const override { return max_; }
    void SetDirection(tSliderWindowDirection direction) override { direction_ = direction; }
    void GetDirection(tSliderWindowDirection& direction) const override { direction = static_cast<tSliderWindowDirection>(direction_); }
    bool SetImage(cIGZBuffer* image, const cRZRect* area) override;
    bool GetImage(cIGZBuffer*& image, cRZRect& area) const override;

    virtual bool ConvertCursorPositionToPercentage(float& fraction, int x, int y);
    virtual void CalculateThumbPosition();
    virtual void CalculateAllMetrics();
    virtual int GetThumbWidth() const;
    virtual int GetThumbHeight() const;
    virtual bool SetPercentage(float fraction);

    const cRZRect& ThumbRect() const { return thumb_; }

protected:
    // (the scrollbar subclass reads and writes the slider's state directly)

private:
    bool PaintFlat();
    bool PaintBmp();

protected:
    std::int32_t direction_ = kSliderVertical;   // +0x158
    std::int32_t min_ = 0, max_ = 0x20;          // +0x15c, +0x160
    float fraction_ = 0.0f;                      // +0x164
    std::int32_t trackStart_ = 0;                // +0x168
    std::int32_t thumbPosition_ = 0;             // +0x16c
    std::int32_t trackEnd_ = 0;                  // +0x170
    std::int32_t margin_ = 0;                    // +0x174
    cRZRect thumb_;                              // +0x178
    bool busy_ = false;                          // +0x188
    cIGZBuffer* image_ = nullptr;                // +0x190
    cRZRect segment_{0, 0, 0, 0};                // +0x198
};

}  // namespace gz
