// cGZWinKnob — a rotary control drawn from a horizontal strip of frames (0x10075897c..0x100759124, 34
// functions). Object layout: +0 the cIGZWinKnob view (iid 0x0bcbf1df), +8 the cGZWin (0x150 bytes), +0x158 the
// frame strip, +0x160 font style, +0x170 frame count, +0x174 min (0), +0x178 max (1), +0x17c value, +0x180 current
// frame, +0x184 font style id (default 0x68963c4c), +0x188 knob flags. The window ignores the cursor.
#pragma once
#include "gz/Buffer.h"
#include "gz/Win.h"
#include "gz/WinStyle.h"

namespace gz {

constexpr std::uint32_t kGZIID_cIGZWinKnob = 0x0bcbf1df;
constexpr std::uint32_t kKnobDefaultFontStyle = 0x68963c4c;

class cIGZWinKnob : public cIGZUnknown {
public:
    virtual cIGZWin* AsIGZWin() = 0;                                  // [3]
    virtual void SetKnobImage(cIGZBuffer* image) = 0;                 // [4]
    virtual cIGZBuffer* GetKnobImage() const = 0;                     // [5]
    virtual void SetFrameCount(std::uint32_t count) = 0;              // [6]
    virtual std::uint32_t GetFrameCount() const = 0;                  // [7]
    virtual void SetMinValue(float value) = 0;                        // [8]
    virtual float GetMinValue() const = 0;                            // [9]
    virtual void SetMaxValue(float value) = 0;                        // [10]
    virtual float GetMaxValue() const = 0;                            // [11]
    virtual void SetValue(float value) = 0;                           // [12]
    virtual float GetValue() const = 0;                               // [13]
    virtual void SetFontStyle(std::uint32_t styleId) = 0;             // [14]
    virtual std::uint32_t GetFontStyle() const = 0;                   // [15]
    virtual void SetKnobFlag(std::uint32_t flag, bool on) = 0;        // [16]
    virtual bool GetKnobFlag(std::uint32_t flag) = 0;                 // [17]
};

class cGZWinKnob : public cIGZWinKnob, public cGZWin {
public:
    explicit cGZWinKnob(cIGZWinMgr* mgr) : cGZWin(mgr) { flags_ |= kWinIgnoreCursor; }
    ~cGZWinKnob() override;

    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return cGZWin::AddRef(); }
    std::uint32_t Release() override { return cGZWin::Release(); }

    bool Init() override;
    bool Shutdown() override;
    bool GZPaint() override;

    cIGZWin* AsIGZWin() override { return this; }
    void SetKnobImage(cIGZBuffer* image) override;
    cIGZBuffer* GetKnobImage() const override { return image_; }
    void SetFrameCount(std::uint32_t count) override;
    std::uint32_t GetFrameCount() const override { return frameCount_; }
    void SetMinValue(float value) override;
    float GetMinValue() const override { return min_; }
    void SetMaxValue(float value) override;
    float GetMaxValue() const override { return max_; }
    void SetValue(float value) override;
    float GetValue() const override { return value_; }
    void SetFontStyle(std::uint32_t styleId) override;
    std::uint32_t GetFontStyle() const override { return styleId_; }
    void SetKnobFlag(std::uint32_t flag, bool on) override {
        if (on) knobFlags_ |= flag;
        else knobFlags_ &= ~flag;
    }
    bool GetKnobFlag(std::uint32_t flag) override { return (knobFlags_ & flag) != 0; }

    std::uint32_t CurrentFrame() const { return currentFrame_; }

private:
    void RecalcCurrentFrame();
    void SetStyle(cIGZUnknown* style);

    cIGZBuffer* image_ = nullptr;            // +0x158
    cIGZUnknown* style_ = nullptr;           // +0x160
    std::uint32_t frameCount_ = 0;           // +0x170
    float min_ = 0.0f, max_ = 1.0f, value_ = 0.0f;   // +0x174..+0x17c
    std::uint32_t currentFrame_ = 0;         // +0x180
    std::uint32_t styleId_ = kKnobDefaultFontStyle;  // +0x184
    std::uint32_t knobFlags_ = 0;            // +0x188
};

}  // namespace gz
