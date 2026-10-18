#include "sc4/WinAuraAlert.h"

#include <cmath>

namespace sc4 {

namespace {
IRenderProperties* g_properties = nullptr;
}
IRenderProperties* WinRenderProperties() { return g_properties; }
void SetWinRenderProperties(IRenderProperties* properties) { g_properties = properties; }

// ---------------------------------------------------------------------------------------------------- aura bar

cSC4WinAuraBar::~cSC4WinAuraBar() {
    if (image_) image_->Release();
}

bool cSC4WinAuraBar::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == 1 || iid == kGZIID_cISC4WinAuraBar) {
        *out = static_cast<cISC4WinAuraBar*>(this);
    } else if (iid == 0x22ba0121 || iid == 0xe98b2f57) {
        *out = static_cast<cIGZWin*>(this);
    } else {
        return false;
    }
    AddRef();
    return true;
}

bool cSC4WinAuraBar::GZPaint() {
    auto* context = static_cast<gz::cIGZDrawContext*>(drawContext_);
    if (!image_) {
        context->SetColor(GetFillColorRGB());
        cRZRect bar = drawArea_;
        bar.right = static_cast<int>((drawArea_.right - drawArea_.left) * value_ + 0.5) + drawArea_.left;
        context->Fill(bar);
        return true;
    }
    const cRZRect* area = image_->GetBufferArea();
    const int windowWidth = drawArea_.right - drawArea_.left;
    const int x = ((area->right - area->left) - windowWidth) >> 1;
    const int y = static_cast<int>(static_cast<double>((area->bottom - 1) - area->top) * value_ + 0.5);
    const cRZRect strip{x, y, x + windowWidth, y + 1};
    context->BltTiled(image_, strip, drawArea_, 0, 0);
    return true;
}

void cSC4WinAuraBar::SetImage(cIGZBuffer* image) {
    if (image_ != image) {
        if (image) image->AddRef();
        cIGZBuffer* old = image_;
        image_ = image;
        if (old) old->Release();
    }
    InvalidateSelfAndParents();
}

void cSC4WinAuraBar::SetValue(double value) {
    value_ = value < 0.0 ? 0.0 : (value > 1.0 ? 1.0 : value);
    InvalidateSelfAndParents();
}

// ------------------------------------------------------------------------------------------------ alert border

cSC4WinAlertBorder::cSC4WinAlertBorder(cIGZWinMgr* mgr) : cGZWin(mgr) {
    if (g_properties) {
        g_properties->AddRef();
        properties_ = g_properties;
    }
}

cSC4WinAlertBorder::~cSC4WinAlertBorder() {
    if (properties_) properties_->Release();
    if (image_) image_->Release();
}

bool cSC4WinAlertBorder::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == 0x22ba0121 || iid == 0xe98b2f57) {
        *out = static_cast<cIGZWin*>(this);
    } else if (iid == 1 || iid == kGZIID_cISC4WinAlertBorder) {
        *out = static_cast<cISC4WinAlertBorder*>(this);
    } else {
        return false;
    }
    AddRef();
    return true;
}

bool cSC4WinAlertBorder::GZPaint() {
    if (image_ && properties_ && properties_->AlertBordersEnabled()) {
        const cRZRect tile{0, 0, static_cast<int>(image_->Width()) / 3, static_cast<int>(image_->Height()) / 3};
        RZDrawBltEdge(static_cast<gz::cIGZDrawContext*>(drawContext_), image_, tile, drawArea_, false);
    }
    return true;
}

void cSC4WinAlertBorder::SetImage(cIGZBuffer* image) {
    if (image_ != image) {
        if (image) image->AddRef();
        cIGZBuffer* old = image_;
        image_ = image;
        if (old) old->Release();
    }
    if (image_) image_->SetTransparency(image_->ConvertRGBValueToNative(0xff, 0, 0xff));
    InvalidateSelfAndParents();
}

}  // namespace sc4
