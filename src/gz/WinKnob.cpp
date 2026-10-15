#include "gz/WinKnob.h"

#include <algorithm>

namespace gz {

namespace {
IWinStyleManager* g_styles = nullptr;
}
IWinStyleManager* WinStyleManager() { return g_styles; }
void SetWinStyleManager(IWinStyleManager* manager) { g_styles = manager; }

cGZWinKnob::~cGZWinKnob() {
    if (image_) image_->Release();
    if (style_) style_->Release();
}

bool cGZWinKnob::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZWinKnob) {
        *out = static_cast<cIGZWinKnob*>(this);
        AddRef();
        return true;
    }
    return cGZWin::QueryInterface(iid, out);
}

void cGZWinKnob::SetStyle(cIGZUnknown* style) {
    if (style_ == style) return;
    if (style) style->AddRef();
    cIGZUnknown* old = style_;
    style_ = style;
    if (old) old->Release();
}

bool cGZWinKnob::Init() {
    if (get_init_flag()) return true;
    SetFlag(kWinPrivateBuffer, false);
    SetFlag(kWinFlag4, true);
    cGZWin::Init();
    IWinStyleManager* styles = WinStyleManager();
    if (!styles) return true;
    SetStyle(styles->GetFontStyle(styleId_));
    if (!style_) SetStyle(styles->GetFontStyle(kKnobDefaultFontStyle));
    return true;
}

bool cGZWinKnob::Shutdown() {
    if (!get_init_flag()) return true;
    if (image_) {
        cIGZBuffer* image = image_;
        image_ = nullptr;
        image->Release();
    }
    if (style_) {
        cIGZUnknown* style = style_;
        style_ = nullptr;
        style->Release();
    }
    return cGZWin::Shutdown();
}

// The image is a horizontal strip of frameCount frames; magenta is its colour key.
void cGZWinKnob::SetKnobImage(cIGZBuffer* image) {
    if (image_ == image) return;
    if (image) image->AddRef();
    cIGZBuffer* old = image_;
    image_ = image;
    if (old) old->Release();
    if (image_) {
        IWinGraphics* graphics = WinGraphics();
        image_->SetTransparency(graphics ? graphics->MakeNativeColor(0xff, 0, 0xff) : 0xffff00ffu);
    }
    InvalidateSelfAndParents();
}

void cGZWinKnob::SetFontStyle(std::uint32_t styleId) {
    IWinStyleManager* styles = WinStyleManager();
    cIGZUnknown* style = styles ? styles->GetFontStyle(styleId) : nullptr;
    if (!style) return;
    styleId_ = styleId;
    SetStyle(style);
    InvalidateSelfAndParents();
}

void cGZWinKnob::SetFrameCount(std::uint32_t count) {
    if (frameCount_ == count) return;
    frameCount_ = count;
    RecalcCurrentFrame();
}

// frame 0 = disabled / no image; otherwise 1 + value's position along [min,max] scaled to frameCount-1 frames
void cGZWinKnob::RecalcCurrentFrame() {
    std::uint32_t frame = 0;
    if (image_ && frameCount_ != 0 && IsEnabled() && min_ < max_) {
        const std::uint32_t last = frameCount_ - 1;
        const std::uint32_t scaled = static_cast<std::uint32_t>(
                                         static_cast<std::int32_t>(static_cast<std::int64_t>(
                                             static_cast<float>(last) * ((value_ - min_) / (max_ - min_))))) + 1;
        frame = scaled == 0 ? 1 : std::min(scaled, last);
    }
    if (currentFrame_ == frame) return;
    currentFrame_ = frame;
    InvalidateSelfAndParents();
}

void cGZWinKnob::SetMinValue(float value) {
    if (min_ == value) return;
    min_ = value;
    const float capped = value_ <= max_ ? value_ : max_;
    value_ = value_ < value ? value : capped;
    RecalcCurrentFrame();
}

void cGZWinKnob::SetMaxValue(float value) {
    if (max_ == value) return;
    max_ = value;
    const float capped = value_ <= value ? value_ : value;
    value_ = value_ < min_ ? min_ : capped;
    RecalcCurrentFrame();
}

void cGZWinKnob::SetValue(float value) {
    const float capped = value <= max_ ? value : max_;
    const float clamped = value < min_ ? min_ : capped;
    if (value_ == clamped) return;
    value_ = clamped;
    RecalcCurrentFrame();
}

bool cGZWinKnob::GZPaint() {
    if (image_ && frameCount_ != 0) {
        const std::uint32_t height = image_->Height();
        const std::uint32_t frameWidth = image_->Width() / frameCount_;
        const std::uint32_t frame = IsEnabled() ? currentFrame_ : 0;
        const int left = static_cast<int>(frame * frameWidth);
        const cRZRect source{left, 0, left + static_cast<int>(frameWidth), static_cast<int>(height)};
        const cRZRect destination{0, 0, static_cast<int>(frameWidth), static_cast<int>(height)};
        static_cast<cIGZDrawContext*>(drawContext_)->Blt(image_, source, destination);
    }
    return true;
}

}  // namespace gz
