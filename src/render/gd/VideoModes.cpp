#include "render/gd/VideoModes.h"

namespace gd {

namespace {
// The twelve stock sizes (0x100a269c0 widths, 0x100a269f0 heights).
const std::uint32_t kStockWidth[12] = {0x320, 0x400, 0x500, 0x500, 0x500, 0x540, 0x540, 0x5a0, 0x5a0, 0x640, 0x640, 0x690};
const std::uint32_t kStockHeight[12] = {0x258, 0x300, 0x320, 0x3c0, 0x400, 0x2f4, 0x3f0, 0x384, 0x438, 0x384, 0x4b0, 0x41a};
constexpr int kUnboundedWindow = 9999;
}  // namespace

void cVideoModeList::AddFullScreen(int width, int height, int depth) {
    sGDMode m{};
    m.id = static_cast<std::int32_t>(modes_.size());
    m.width = static_cast<std::uint32_t>(width);
    m.height = static_cast<std::uint32_t>(height);
    m.bitsPerPixel = static_cast<std::uint32_t>(depth);
    m.flags[0] = kModeFullScreen;
    modes_.push_back(m);
    sGDMode second = m;
    if (static_cast<std::uint32_t>(width) < static_cast<std::uint32_t>(maxWidth_) &&
        static_cast<std::uint32_t>(height) < static_cast<std::uint32_t>(maxHeight_)) {
        second.id = static_cast<std::int32_t>(modes_.size());
        second.flags[0] = kModeWindowed;
    }
    modes_.push_back(second);   // otherwise an exact repeat, id included
}

void cVideoModeList::AddWindowed(int width, int height, int depth) {
    sGDMode m{};
    m.id = static_cast<std::int32_t>(modes_.size());
    m.width = static_cast<std::uint32_t>(width);
    m.height = static_cast<std::uint32_t>(height);
    m.bitsPerPixel = static_cast<std::uint32_t>(depth);
    m.flags[0] = kModeWindowed;
    modes_.push_back(m);
}

int cVideoModeList::CountVideoModes() {
    if (!modes_.empty()) return static_cast<int>(modes_.size());
    const int displayModes = source_->NumModes();
    bool covered[12] = {};
    source_->WindowMaxSizes(maxWidth_, maxHeight_);
    for (int i = 0; i < displayModes; ++i) {
        int w, h, d;
        source_->NthMode(i, w, h, d);
        AddFullScreen(w, h, d);
        for (int k = 0; k < 12; ++k) {
            if (static_cast<std::uint32_t>(w) == kStockWidth[k] && static_cast<std::uint32_t>(h) == kStockHeight[k]) {
                covered[k] = true;
                break;
            }
        }
    }
    for (int k = 0; k < 12; ++k) {
        if (!covered[k] && static_cast<int>(kStockWidth[k]) < maxWidth_ && static_cast<int>(kStockHeight[k]) < maxHeight_)
            AddWindowed(static_cast<int>(kStockWidth[k]), static_cast<int>(kStockHeight[k]), 0x20);
    }
    if (maxWidth_ != kUnboundedWindow) AddWindowed(maxWidth_, maxHeight_, 0x20);
    return static_cast<int>(modes_.size());
}

bool cVideoModeList::GetVideoModeInfo(std::uint32_t index, sGDMode& out) const {
    if (index >= modes_.size()) return false;
    out = modes_[index];
    return true;
}

}  // namespace gd
