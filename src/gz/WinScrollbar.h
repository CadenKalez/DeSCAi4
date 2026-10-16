// cGZWinScrollbar — a scrollbar built on the slider (45 functions 0x10027a1b0..0x10027c786 share the file with the
// slider). Its thumb is the slider's; on top it adds the previous / next buttons at both ends of the track,
// page areas between the thumb and the buttons, repeat scrolling while a button is held (every 200 ms) and a line /
// page model: the value range is cut into `lineCount` lines and `pageCount` pages.
//
// The image is twelve cells side by side: 0 previous button, 1 pressed, 2 thumb, 3 track, 4 next button, 5 pressed;
// the second half (6..11) is used for vertical bars. Without an image the bar is drawn flat.
//
// Layout (0x1d8 bytes): cGZWinSlider (0x1a8, with the cIGZWinSlider view at +0x150), +0x1a8 cIGZWinScrollbar view
// (iid 0x61325a2d), +0x1b0 line count, +0x1b4 page count, +0x1b8 repeat timer, +0x1d0 pressed part, +0x1d4 repeat armed.
#pragma once
#include "gz/WinSlider.h"
#include "rz/cRZTimer.h"

namespace gz {

constexpr std::uint32_t kGZIID_cIGZWinScrollbar = 0x61325a2d;

enum tScrollHitTestResult : std::int32_t {
    kScrollNothing = 0, kScrollPreviousLine = 1, kScrollNextLine = 2, kScrollPreviousPage = 3, kScrollNextPage = 4, kScrollThumb = 5,
};

// cIGZWinScrollbar (iid 0x61325a2d): slot n at 8n.
class cIGZWinScrollbar : public cIGZUnknown {
public:
    virtual cIGZWin* AsIGZWin() = 0;                                                   // [3]
    virtual cIGZWinSlider* AsIGZWinSlider() = 0;                                       // [4]
    virtual bool SetValue(int value) = 0;                                              // [5]
    virtual int GetValue() const = 0;                                                  // [6]
    virtual bool SetMinMax(int minimum, int maximum) = 0;                              // [7]
    virtual bool GetMinMax(int& minimum, int& maximum) const = 0;                      // [8]
    virtual bool SetOrientation(int orientation) = 0;                                  // [9]
    virtual int GetOrientation() const = 0;                                            // [10]
    virtual bool SetPageSize(int size) = 0;                                            // [11]
    virtual std::uint32_t GetPageSize() const = 0;                                     // [12]
    virtual bool SetLineSize(int size) = 0;                                            // [13]
    virtual std::uint32_t GetLineSize() const = 0;                                     // [14]
    virtual void SetLineAndPageCount(std::uint32_t lines, std::uint32_t pages) = 0;    // [15]
    virtual void GetLineAndPageCount(std::uint32_t& lines, std::uint32_t& pages) const = 0;   // [16]
    virtual bool ImageSetup(const cRZRect& area, cIGZBuffer* image, int flags) = 0;    // [17]
    virtual bool GetImage(cRZRect& area, cIGZBuffer*& image, int& flags) const = 0;    // [18]
    virtual bool GetMinMaxInfo(cRZRect& limits) const = 0;                             // [19]
};

class cGZWinScrollbar : public cGZWinSlider, public cIGZWinScrollbar {
public:
    explicit cGZWinScrollbar(cIGZWinMgr* mgr);
    static cGZWinScrollbar* CreateNewObject(cIGZWinMgr* mgr) { return new cGZWinScrollbar(mgr); }

    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return cGZWin::AddRef(); }
    std::uint32_t Release() override { return cGZWin::Release(); }

    bool GZPaint() override;
    bool GZOnMouseDownL(int x, int y, std::uint32_t flags) override;
    bool GZOnMouseUpL(int x, int y, std::uint32_t flags) override;
    bool GZOnMouseMove(int x, int y, std::uint32_t flags) override;
    bool SetImage(cIGZBuffer* image, const cRZRect* area) override;
    void CalculateAllMetrics() override;

    // cIGZWinScrollbar (SetValue / GetValue are the slider's)
    cIGZWin* AsIGZWin() override { return this; }
    cIGZWinSlider* AsIGZWinSlider() override { return static_cast<cIGZWinSlider*>(this); }
    bool SetValue(int value) override { return cGZWinSlider::SetValue(value); }
    int GetValue() const override { return cGZWinSlider::GetValue(); }
    bool SetMinMax(int minimum, int maximum) override;
    bool GetMinMax(int& minimum, int& maximum) const override;
    bool SetOrientation(int orientation) override;
    int GetOrientation() const override;
    bool SetPageSize(int size) override;
    std::uint32_t GetPageSize() const override;
    bool SetLineSize(int size) override;
    std::uint32_t GetLineSize() const override;
    void SetLineAndPageCount(std::uint32_t lines, std::uint32_t pages) override;
    void GetLineAndPageCount(std::uint32_t& lines, std::uint32_t& pages) const override { lines = lineCount_; pages = pageCount_; }
    bool ImageSetup(const cRZRect& area, cIGZBuffer* image, int) override { return SetImage(image, &area); }
    bool GetImage(cRZRect& area, cIGZBuffer*& image, int& flags) const override;
    bool GetMinMaxInfo(cRZRect& limits) const override;

    virtual void MoveLinePrevious();
    virtual void MoveLineNext();
    virtual void MovePagePrevious();
    virtual void MovePageNext();
    virtual bool DoCursorPositionHitTest(tScrollHitTestResult& result, int x, int y);
    virtual int GetPrevNextButtonWidth() { return GetThumbWidth(); }
    virtual int GetPrevNextButtonHeight() { return GetThumbHeight(); }
    virtual long GetCurrentLine();
    virtual int GetValueOfGivenLine(std::uint32_t line);
    virtual long GetCurrentPage();
    virtual int GetValueOfGivenPage(std::uint32_t page);

    tScrollHitTestResult PressedPart() const { return static_cast<tScrollHitTestResult>(pressed_); }
    cRZTimer& RepeatTimer() { return repeat_; }
    using cGZWinSlider::GetImage;

protected:
    void PaintFlat();
    void PaintBmp();

    std::uint32_t lineCount_ = 0, pageCount_ = 0;   // +0x1b0, +0x1b4
    cRZTimer repeat_{cRZTimer::kMilliseconds};      // +0x1b8
    std::int32_t pressed_ = 0;                      // +0x1d0
    bool armed_ = true;                             // +0x1d4
};

}  // namespace gz
