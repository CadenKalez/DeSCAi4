#include "gz/PieGraph.h"

#include <climits>
#include <cmath>
#include <cstdio>

namespace gz {

namespace {
IPieDrawing* g_drawing = nullptr;
constexpr std::uint32_t kLockLine = 0x8004, kLockBlt = 0x8010, kLockEllipse = 0x8100, kLockCircle = 0x8200,
                        kLockFlood = 0x8400, kLockMaskRead = 0x8080;
constexpr std::uint32_t kAll = 0xffffffffu;
inline cIGZBuffer::eLockFlags Flags(std::uint32_t f) { return static_cast<cIGZBuffer::eLockFlags>(f); }
}  // namespace

IPieDrawing* PieDrawing() { return g_drawing; }
void SetPieDrawing(IPieDrawing* drawing) { g_drawing = drawing; }

cGZPieGraph::cGZPieGraph(cIGZWinMgr* mgr) : cGZGraph(mgr) {   // @ 0x100384614
    drawAxes_ = false;
    for (int a = 0; a < 2; ++a) {
        majorTicks_[a] = minorTicks_[a] = majorGrid_[a] = minorGrid_[a] = tickText_[a] = false;
    }
}

cGZPieGraph::~cGZPieGraph() { cGZPieGraph::Shutdown(); }   // @ 0x10038471e

bool cGZPieGraph::QueryInterface(std::uint32_t iid, void** out) {   // @ 0x1003848f8
    if (iid == kGZIID_cIGZGraph) {
        *out = static_cast<cIGZGraph*>(this);
    } else if (iid == kGZIID_cIGZPieGraph) {
        *out = static_cast<cIGZPieGraph*>(this);
    } else {
        return cGZWin::QueryInterface(iid, out);
    }
    AddRef();
    return true;
}

bool cGZPieGraph::Shutdown() {   // @ 0x10038481c
    auto drop = [](auto*& slot) { if (slot) { slot->Release(); slot = nullptr; } };
    drop(faceMask_);
    drop(sideMask_);
    drop(percentageFont_);
    return cGZGraph::Shutdown();
}

void cGZPieGraph::LoadDefaultFonts() {   // @ 0x100384994
    if (percentageFont_ == nullptr) {
        IGraphHost* host = GraphHost();
        if (host && host->FontsAvailable()) {
            cIGZFont* font = host->CreateFont("arial", 12);
            if (font == nullptr) font = host->CreateFont("helvetica", 12);
            if (font) {
                SetPercentageTextFont(font, kAutoColor);
                font->Release();
            }
        }
    }
    cGZGraph::LoadDefaultFonts();
}

void cGZPieGraph::SetPercentageTextFont(cIGZFont* font, std::uint32_t color) {   // @ 0x100384ae6
    ReplaceFont(percentageFont_, font);
    if (color != kAutoColor) percentageColor_ = color;
}

void cGZPieGraph::SetMinPercentageToDisplay(float percentage) {   // @ 0x100384b5a
    float value = 0.0f;
    if (0.0f <= percentage) value = percentage;
    const float fraction = 1.0f < value ? value / 100.0f : value;   // accepts a fraction or a percentage
    minFraction_ = fraction <= 1.0f ? fraction : 1.0f;
}

void cGZPieGraph::Set3DViewMaskBuffers(cIGZBuffer* face, cIGZBuffer* side) {   // @ 0x100384bd2
    if (face) face->AddRef();
    if (faceMask_) faceMask_->Release();
    faceMask_ = face;
    if (side) side->AddRef();
    if (sideMask_) sideMask_->Release();
    sideMask_ = side;
    if (faceMask_ && side) viewType_ = kPie3D;
    if (side == nullptr) return;
    if (!side->Lock(Flags(kLockMaskRead))) return;
    const int width = static_cast<int>(sideMask_->Width());
    const int height = static_cast<int>(sideMask_->Height());
    const std::uint32_t background = sideMask_->GetPixel(0, 0);
    maskBounds_ = cRZRect{INT_MAX, INT_MAX, INT_MIN, INT_MIN};
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (sideMask_->GetPixel(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y)) != background) {
                if (x < maskBounds_.left) maskBounds_.left = x;
                if (y < maskBounds_.top) maskBounds_.top = y;
                if (maskBounds_.right < x) maskBounds_.right = x;
                if (maskBounds_.bottom < y) maskBounds_.bottom = y;
            }
        }
    }
    if (height > 0 && maskBounds_.left != INT_MAX) {
        const int lastX = maskBounds_.right, lastY = maskBounds_.bottom;
        maskBounds_.right = lastX + 1;
        maskBounds_.bottom = lastY + 1;
        disk_.left = maskBounds_.left + graphArea_.left;
        disk_.top = maskBounds_.top + graphArea_.top;
        disk_.right = graphArea_.left + 1 + lastX;
        disk_.bottom = graphArea_.top + 1 + lastY;
        thickness_ = static_cast<int>(faceMask_->Height()) - maskBounds_.bottom;
    } else {
        thickness_ = 0;
        disk_ = cRZRect{0, 0, width, height};
    }
    sideMask_->Unlock(Flags(kLockMaskRead));
}

void cGZPieGraph::SetGraphArea(const cRZRect& area) {   // @ 0x100385fec
    graphArea_ = area;
    disk_.left = graphArea_.left + maskBounds_.left;
    disk_.top = graphArea_.top + maskBounds_.top;
    disk_.right = graphArea_.left + maskBounds_.right;
    disk_.bottom = graphArea_.top + maskBounds_.bottom;
}

// ---------------------------------------------------------------------------------------------------------------
// Series and data

void cGZPieGraph::SetSeriesCount(std::uint32_t count) {   // @ 0x100384e14
    values_.resize(count);
    series_.resize(count);
    // series without a name are given the (default-built) default entry
    for (std::uint32_t i = 0; i < count; ++i) {
        if (series_[i].name.Strlen() == 0) {
            const PieGraphSeriesInfo defaults;
            series_[i].color = defaults.color;
            series_[i].textColor = defaults.textColor;
            series_[i].name.AssignFromIGZString(defaults.name);
        }
    }
}

void cGZPieGraph::InsertSeries(std::uint32_t index) {   // @ 0x100384f52
    if (index == kAll) {
        values_.push_back(1.0f);
        series_.emplace_back();
    } else {
        if (values_.size() <= index) return;
        values_.insert(values_.begin() + index, 1.0f);
        series_.insert(series_.begin() + index, PieGraphSeriesInfo());
    }
}

void cGZPieGraph::RemoveSeries(std::uint32_t index) {   // @ 0x10038549a
    if (index == kAll) {
        values_.pop_back();
        series_.pop_back();
        return;
    }
    if (values_.size() <= index) return;
    values_.erase(values_.begin() + index);
    series_.erase(series_.begin() + index);
}

void cGZPieGraph::RemoveAllSeries() {   // @ 0x1003855ba
    for (std::size_t n = values_.size(); n != 0; --n) RemoveSeries(kAll);
}

void cGZPieGraph::SetSeriesColor(std::uint32_t color, std::uint32_t series, std::uint32_t textColor) {   // @ 0x100385630
    if (series == kAll) {
        for (PieGraphSeriesInfo& info : series_) { info.color = color; info.textColor = textColor; }
    } else if (series < series_.size()) {
        series_[series].color = color;
        series_[series].textColor = textColor;
    }
}

void cGZPieGraph::SetSeriesName(const cRZString& name, std::uint32_t series) {   // @ 0x1003856f0
    if (series == kAll) { for (PieGraphSeriesInfo& info : series_) info.name.AssignFromIGZString(name); }
    else if (series < series_.size()) series_[series].name.AssignFromIGZString(name);
}

void cGZPieGraph::SetSeriesInfo(PieGraphSeriesInfo& info, std::uint32_t series) {   // @ 0x1003857c2
    if (series < series_.size()) series_[series] = info;
}

void cGZPieGraph::InsertSeriesInfo(PieGraphSeriesInfo& info, std::uint32_t series) {   // @ 0x100385858
    if (series == kAll) series_.push_back(info);
    else if (series < series_.size()) series_.insert(series_.begin() + series, info);
}

void cGZPieGraph::RemoveSeriesInfo(std::uint32_t series) {   // @ 0x100385944
    if (series == kAll) series_.pop_back();
    else if (series < series_.size()) series_.erase(series_.begin() + series);
}

void cGZPieGraph::InsertData(float value, std::uint32_t index) {   // @ 0x100385a34
    const std::size_t size = values_.size();
    if (index != kAll && size <= index) return;
    if (index == kAll) {
        const std::uint32_t wanted = static_cast<std::uint32_t>(size) + 1;
        if (series_.size() < wanted) SetSeriesCount(wanted);
        if (values_.size() < wanted) values_.resize(wanted);
        index = static_cast<std::uint32_t>(size);
    }
    SetData(value, index);   // an explicit index overwrites that slice (sic)
}

void cGZPieGraph::RemoveData(std::uint32_t index) {   // @ 0x100385b12
    index = SingleIndex(index);
    if (index < values_.size()) values_.erase(values_.begin() + index);
}

void cGZPieGraph::SetData(float value, std::uint32_t index) {   // @ 0x100385bb4
    index = SingleIndex(index);
    if (index < values_.size()) values_[index] = value;
}

void cGZPieGraph::MakeNormalizedDataArray(std::vector<float>& in, std::vector<float>& out) {   // @ 0x100385f12
    if (in.empty()) return;
    float total = 0.0f;
    for (float v : in) total += std::fabs(v);
    const float scale = 1.0f / (0.0001f <= total ? total : 0.0001f);
    for (float v : in) out.push_back(std::fabs(v) * scale);
}

// ---------------------------------------------------------------------------------------------------------------
// Geometry

void cGZPieGraph::GetLineFromCenterOfDiskToEdge(float fraction, cRZPoint& center, cRZPoint& edge) {   // @ 0x100385c48
    center.x = (disk_.right + disk_.left) / 2;
    center.y = (disk_.bottom + disk_.top) / 2;
    const float angle = fraction * 6.2831855f + 1.5707964f;
    const float s = std::sin(angle), c = std::cos(angle);
    float rise;
    if (viewType_ == kPie3D) {
        const float width = static_cast<float>(disk_.right - disk_.left);
        edge.x = center.x + static_cast<int>(c * width * 0.5f);
        rise = (static_cast<float>(disk_.bottom - disk_.top) / width) * s * width * 0.5f;
    } else if (viewType_ == kPieFlat) {
        rise = static_cast<float>(disk_.bottom - disk_.top) * s * 0.5f;
        edge.x = center.x + static_cast<int>(static_cast<float>(disk_.right - disk_.left) * c * 0.5f);
    } else {
        return;
    }
    edge.y = center.y - static_cast<int>(rise);
}

void cGZPieGraph::GetPositionForPercentageText(std::uint32_t series, cRZPoint& out) {   // @ 0x100385dae
    std::vector<float> normalized;
    MakeNormalizedDataArray(values_, normalized);
    float start = 0.0f, end = 0.0f;
    for (std::uint32_t i = 0; i <= series; ++i) {
        start = end;
        end = (i < normalized.size() ? normalized[i] : 0.0f) + start;
    }
    GetPositionForPercentageText(start, end, out);
}

void cGZPieGraph::GetPositionForPercentageText(float start, float end, cRZPoint& out) {   // @ 0x100385e5c
    cRZPoint center, edge;
    GetLineFromCenterOfDiskToEdge((start + end) * 0.5f, center, edge);
    const float along = (0.1f < end - start) ? 0.6f : 0.8f;   // table @ 0x100a1dfe0 = {0.8, 0.6}
    out.x = static_cast<int>(static_cast<float>(edge.x - center.x) * along) + center.x;
    out.y = static_cast<int>(static_cast<float>(edge.y - center.y) * along) + center.y;
}

// ---------------------------------------------------------------------------------------------------------------
// Drawing

void cGZPieGraph::DrawPercentage(cIGZBuffer* buffer, std::size_t slice, float start, float fraction, bool checkMinimum,
                                 const std::vector<float>& normalized) {
    (void)normalized;
    if (!showPercentages_ || percentageFont_ == nullptr) return;
    if (checkMinimum && !(minFraction_ <= fraction)) return;
    char format[8] = {'%', '.', static_cast<char>(decimals_ + '0'), 'f', '%', '%', 0, 0};
    char text[256];
    std::snprintf(text, sizeof(text), format, static_cast<double>(fraction * 100.0f));
    const std::uint32_t length = static_cast<std::uint32_t>(std::char_traits<char>::length(text));
    cRZPoint position;
    GetPositionForPercentageText(start, start + fraction, position);
    const int height = percentageFont_->CharHeight(nullptr, 0);
    std::uint32_t visible = 0;
    const int width = percentageFont_->GetStringVisibleWidth(text, length, visible);
    const std::uint32_t color = series_[slice].textColor == kAutoColor ? kFontDefaultColor : series_[slice].textColor;
    percentageFont_->DrawTextLine(buffer, position.x - width / 2, position.y - height / 2, text, visible, color,
                                  kFontDefaultColor, nullptr, nullptr);
}

void cGZPieGraph::DrawGraphData(cIGZBuffer* buffer) {   // @ 0x100386100
    std::vector<float> normalized;
    MakeNormalizedDataArray(values_, normalized);
    IPieDrawing* draw = PieDrawing();
    const std::size_t count = normalized.size();
    auto radius = [&](float at, cRZPoint& p0, cRZPoint& p1) { GetLineFromCenterOfDiskToEdge(at, p0, p1); };

    if (viewType_ == kPieFlat) {
        disk_ = cRZRect{graphArea_.left + 1, graphArea_.top + 1, graphArea_.right - 1, graphArea_.bottom - 1};
        const int width = disk_.right - disk_.left, height = disk_.bottom - disk_.top;
        if (height < width) disk_.right = disk_.left + height;
        else if (width < height) disk_.bottom = disk_.top + width;
        if (buffer->Lock(Flags(kLockEllipse))) {
            draw->FillEllipse(buffer, disk_.left, disk_.top, disk_.right, disk_.bottom, 0xeeeeeeee);
            buffer->Unlock(Flags(kLockEllipse));
        }
        // pass 1: slice-coloured separators, then flood fill each slice
        cRZPoint firstEdge{0, 0};
        float cumulative = 0.0f;
        for (std::size_t i = 0; i < count; ++i) {
            const std::uint32_t color = series_[i].color;
            cRZPoint p0, p1{0, 0};
            if (buffer->Lock(Flags(kLockLine))) {
                radius(cumulative, p0, p1);
                if (i == 0) firstEdge = p1;
                DrawLine(buffer, p0.x, p0.y, p1.x, p1.y, static_cast<int>(color), 1);
                radius(normalized[i] + cumulative, p0, p1);
                if (i + 1 == count) p1 = firstEdge;
                DrawLine(buffer, p0.x, p0.y, p1.x, p1.y, static_cast<int>(color), 1);
                buffer->Unlock(Flags(kLockLine));
            }
            cRZPoint seed;
            GetPositionForPercentageText(cumulative, normalized[i] + cumulative, seed);
            if ((seed.x != p1.x || seed.y != p1.y) && buffer->Lock(Flags(kLockFlood))) {
                draw->FloodFill(buffer, seed.x, seed.y, color);
                buffer->Unlock(Flags(kLockFlood));
            }
            cumulative += normalized[i];
        }
        if (buffer->Lock(Flags(kLockCircle))) {
            draw->DrawCircle(buffer, disk_.left, disk_.top, disk_.right, disk_.bottom, 0, 1);
            buffer->Unlock(Flags(kLockCircle));
        }
        // pass 2: black separators and the percentage labels
        cumulative = 0.0f;
        for (std::size_t i = 0; i < count; ++i) {
            if (buffer->Lock(Flags(kLockLine))) {
                cRZPoint p0, p1;
                radius(cumulative, p0, p1);
                if (i == 0) firstEdge = p1;
                DrawLine(buffer, p0.x, p0.y, p1.x, p1.y, 0, 1);
                radius(normalized[i] + cumulative, p0, p1);
                if (i + 1 == count) p1 = firstEdge;
                DrawLine(buffer, p0.x, p0.y, p1.x, p1.y, 0, 1);
                buffer->Unlock(Flags(kLockLine));
            }
            DrawPercentage(buffer, i, cumulative, normalized[i], true, normalized);
            cumulative += normalized[i];
        }
    } else if (viewType_ == kPie3D) {
        if (buffer->Lock(Flags(kLockBlt))) {
            cRZRect rect = disk_;
            if (thickness_ != -1) {
                for (std::uint32_t layer = 0; layer < static_cast<std::uint32_t>(thickness_) + 1U; ++layer) {
                    Blt(sideMask_, buffer, *sideMask_->GetBufferArea(), rect);
                    ++rect.top;
                    ++rect.bottom;
                }
            }
            buffer->Unlock(Flags(kLockBlt));
        }
        cRZPoint firstEdge{0, 0};
        float cumulative = 0.0f;
        for (std::size_t i = 0; i < count; ++i) {
            const std::uint32_t color = series_[i].color;
            cRZPoint p0, p1{0, 0};
            if (buffer->Lock(Flags(kLockLine))) {
                radius(cumulative, p0, p1);
                if (i == 0) firstEdge = p1;
                DrawLine(buffer, p0.x, p0.y, p1.x, p1.y, static_cast<int>(color), 1);
                if (p0.y < p1.y) buffer->DrawLine(p1.x, p1.y, p1.x, thickness_ + p1.y, static_cast<int>(color), 1);
                radius(normalized[i] + cumulative, p0, p1);
                if (i + 1 == count) p1 = firstEdge;
                DrawLine(buffer, p0.x, p0.y, p1.x, p1.y, static_cast<int>(color), 1);
                if (p0.y < p1.y) DrawLine(buffer, p1.x, p1.y, p1.x, thickness_ + p1.y, static_cast<int>(color), 1);
                buffer->Unlock(Flags(kLockLine));
            }
            cRZPoint seed;
            GetPositionForPercentageText(cumulative, normalized[i] + cumulative, seed);
            if ((seed.x != p1.x || seed.y != p1.y) && buffer->Lock(Flags(kLockFlood))) {
                draw->FloodFill(buffer, seed.x, seed.y, color);
                buffer->Unlock(Flags(kLockFlood));
            }
            cumulative += normalized[i];
        }
        draw->SaturationMaskBlt(buffer, faceMask_, *faceMask_->GetBufferArea(), cRZPoint{graphArea_.left, graphArea_.top});
        cumulative = 0.0f;
        for (std::size_t i = 0; i < count; ++i) {
            DrawPercentage(buffer, i, cumulative, normalized[i], false, normalized);   // no minimum test in the 3D view
            cumulative += normalized[i];
        }
    }
}

}  // namespace gz
