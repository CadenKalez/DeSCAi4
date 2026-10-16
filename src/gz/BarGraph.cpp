#include "gz/BarGraph.h"

#include <algorithm>
#include <cfloat>

#include "gz/RZDraw.h"

namespace gz {

namespace {
constexpr std::uint32_t kLockFill = 0x8001, kLockLine = 0x8004, kLockOutline = 0x8008, kLockBlt = 0x8010;
constexpr std::uint32_t kAll = 0xffffffffu;
inline cIGZBuffer::eLockFlags Flags(std::uint32_t f) { return static_cast<cIGZBuffer::eLockFlags>(f); }
}  // namespace

cGZBarGraph::cGZBarGraph(cIGZWinMgr* mgr) : cGZGraph(mgr) {   // @ 0x10037f0de
    majorTicks_[0] = false; majorTicks_[1] = true;
    majorGrid_[0] = false; majorGrid_[1] = true;
    tickText_[0] = true;
}

cGZBarGraph::~cGZBarGraph() { cGZGraph::Shutdown(); }   // @ 0x10037f196

bool cGZBarGraph::QueryInterface(std::uint32_t iid, void** out) {   // @ 0x10037f2f2
    if (iid == kGZIID_cIGZGraph) {
        *out = static_cast<cIGZGraph*>(this);
    } else if (iid == kGZIID_cIGZBarGraph) {
        *out = static_cast<cIGZBarGraph*>(this);
    } else {
        return cGZWin::QueryInterface(iid, out);
    }
    AddRef();
    return true;
}

std::uint32_t cGZBarGraph::OuterIndex(std::uint32_t series) const {
    return (data_.size() == 1 && series == kAll) ? 0 : series;
}

// ---------------------------------------------------------------------------------------------------------------
// Series and categories

void cGZBarGraph::SetSeriesCount(std::uint32_t count) {   // @ 0x10037f37a
    data_.resize(count);
    series_.resize(count);
    // every series is reset to the defaults (the static default array holds eight default-built infos)
    for (std::uint32_t i = 0; i < count; ++i) series_[i] = BarGraphSeriesInfo();
}

void cGZBarGraph::InsertSeries(std::uint32_t index) {   // @ 0x10037f494
    if (index == kAll) {
        data_.emplace_back();
        series_.emplace_back();
    } else {
        if (data_.size() <= index) return;
        data_.insert(data_.begin() + index, std::vector<float>());
        series_.insert(series_.begin() + index, BarGraphSeriesInfo());
    }
}

void cGZBarGraph::RemoveSeries(std::uint32_t index) {   // @ 0x10037fa64
    if (index == kAll) {
        data_.pop_back();
        series_.pop_back();
        return;
    }
    if (data_.size() <= index) return;
    data_.erase(data_.begin() + index);
    series_.erase(series_.begin() + index);
}

void cGZBarGraph::SetSeriesName(const cRZString& name, std::uint32_t series) {   // @ 0x10037fbfe
    if (series == kAll) {
        for (BarGraphSeriesInfo& info : series_) info.name.AssignFromIGZString(name);
    } else if (series < series_.size()) {
        series_[series].name.AssignFromIGZString(name);
    }
}

void cGZBarGraph::SetSeriesInfo(BarGraphSeriesInfo& info, std::uint32_t series) {   // @ 0x10037fcae
    if (series < series_.size()) series_[series] = info;
}

void cGZBarGraph::InsertSeriesInfo(BarGraphSeriesInfo& info, std::uint32_t series) {   // @ 0x10037fd40
    if (series == kAll) series_.push_back(info);
    else if (series < series_.size()) series_.insert(series_.begin() + series, info);
}

void cGZBarGraph::RemoveSeriesInfo(std::uint32_t series) {   // @ 0x10037fe2c
    if (series == kAll) {
        series_.pop_back();
    } else if (series < series_.size()) {
        series_.erase(series_.begin() + series);
    }
}

void cGZBarGraph::InsertCategoryName(const cRZString& name, std::uint32_t index) {   // @ 0x10037ff1c
    if (index == kAll) index = static_cast<std::uint32_t>(categoryNames_.size());
    if (categoryNames_.size() <= index) categoryNames_.resize(static_cast<std::size_t>(index) + 1);
    categoryNames_[index].AssignFromIGZString(name);
}

void cGZBarGraph::RemoveCategoryName(std::uint32_t index) {   // @ 0x10037ffc2
    if (index == kAll) index = static_cast<std::uint32_t>(categoryNames_.size()) - 1;
    if (index < categoryNames_.size()) categoryNames_[index].AssignFromCString("");   // only the text goes
}

void cGZBarGraph::SetBarColorUse(Usage outline, Usage fill, std::uint32_t series) {   // @ 0x1003800c4
    if (series == kAll) {
        for (BarGraphSeriesInfo& info : series_) { info.outline = outline != 0; info.fill = fill != 0; }
    } else if (series < series_.size()) {
        series_[series].outline = outline != 0;
        series_[series].fill = fill != 0;
    }
}

void cGZBarGraph::SetBarFillColor(std::uint32_t color, std::uint32_t series) {   // @ 0x100380042
    if (series == kAll) { for (BarGraphSeriesInfo& info : series_) info.fillColor = color; }
    else if (series < series_.size()) series_[series].fillColor = color;
}

void cGZBarGraph::SetBarOutlineColor(std::uint32_t color, std::uint32_t series) {   // @ 0x10038016e
    if (series == kAll) { for (BarGraphSeriesInfo& info : series_) info.outlineColor = color; }
    else if (series < series_.size()) series_[series].outlineColor = color;
}

void cGZBarGraph::SetBarPattern(cIGZBuffer* pattern, std::uint32_t series) {   // @ 0x1003801f0
    if (series >= series_.size()) return;
    if (pattern) pattern->AddRef();
    if (series_[series].pattern) series_[series].pattern->Release();
    series_[series].pattern = pattern;
}

void cGZBarGraph::SetBarWidthPercentage(float fraction) {   // @ 0x10038026e
    float value = 0.1f;
    if (0.1f <= fraction) {
        value = fraction;
        if (1.0f < fraction) value = 1.0f;
    }
    widthFraction_ = value;
}

// ---------------------------------------------------------------------------------------------------------------
// Data

void cGZBarGraph::InsertData(float value, std::uint32_t series, std::uint32_t position) {   // @ 0x1003802ca
    const std::uint32_t outer = OuterIndex(series);
    if (outer >= data_.size()) return;
    std::vector<float>& values = data_[outer];
    if (position == kAll || position >= values.size()) values.push_back(value);
    else values.insert(values.begin() + position, value);
}

void cGZBarGraph::RemoveData(std::uint32_t position, std::uint32_t series) {   // @ 0x100380368
    const std::uint32_t outer = OuterIndex(series);
    if (outer >= data_.size()) return;
    std::vector<float>& values = data_[outer];
    if (position < values.size()) values.erase(values.begin() + position);
}

void cGZBarGraph::SetData(float value, std::uint32_t position, std::uint32_t series) {   // @ 0x1003803f6
    const std::uint32_t outer = OuterIndex(series);
    if (outer >= data_.size()) return;
    std::vector<float>& values = data_[outer];
    if (position == kAll) values.push_back(value);
    else if (position < values.size()) values[position] = value;
}

void cGZBarGraph::SetData(std::vector<float>& values, std::uint32_t series) {   // @ 0x10038048c
    const std::uint32_t outer = OuterIndex(series);
    if (outer < data_.size() && &data_[outer] != &values) data_[outer].assign(values.begin(), values.end());
}

void cGZBarGraph::AppendData(std::vector<float>& values, std::uint32_t series) {   // @ 0x1003804f6
    const std::uint32_t outer = OuterIndex(series);
    if (outer >= data_.size()) return;
    for (float v : values) data_[outer].push_back(v);
}

// ---------------------------------------------------------------------------------------------------------------
// Geometry

int cGZBarGraph::GetLeftOfBar(std::uint32_t category, std::uint32_t series) {   // @ 0x1003805a2
    float result;
    if (data_.empty() || (type_ != kBarClustered && type_ != kBarStacked)) {
        result = spacing_;
    } else {
        const int origin = orientation_ == kOrientationVertical ? graphArea_.left : graphArea_.top;
        float base = static_cast<float>(category + 1) * spacing_ + static_cast<float>(origin);
        float offset;
        if (type_ == kBarClustered) {
            base += static_cast<float>(category) * barWidth_ * static_cast<float>(data_.size());
            offset = barWidth_ * static_cast<float>(series);
        } else {
            offset = static_cast<float>(category) * barWidth_;
        }
        result = offset + base;
    }
    return static_cast<int>(result);
}

int cGZBarGraph::GetRightOfBar(std::uint32_t category, std::uint32_t series) {   // @ 0x1003806c2
    return static_cast<int>(static_cast<float>(GetLeftOfBar(category, series)) + barWidth_);
}

int cGZBarGraph::GetCenterOfBar(std::uint32_t category, std::uint32_t series) {   // @ 0x10038071e
    return static_cast<int>(barWidth_ * 0.5f + static_cast<float>(GetLeftOfBar(category, series)));
}

bool cGZBarGraph::GetRectOfBar(std::uint32_t category, std::uint32_t series, cRZRect& rect) {   // @ 0x100380792
    if (series >= data_.size() || category >= data_[series].size()) {
        rect = cRZRect{0, 0, 0, 0};
        return false;
    }
    const bool vertical = orientation_ == kOrientationVertical;
    const Axis valueAxis = vertical ? kAxisVertical : kAxisHorizontal;
    const int zero = zeroPosition_[vertical ? 1 : 0];
    const int barLeft = GetLeftOfBar(category, series);
    const float barRight = static_cast<float>(barLeft) + barWidth_;
    int first = 0, second = 0;
    if (type_ == kBarClustered) {
        const float value = data_[series][category];
        const int far = zero - ValueAmountToPixelAmount(value, valueAxis);
        if (value > 0.0f) { first = far; second = zero; }
        else { first = zero; second = far; }
    } else if (type_ == kBarStacked) {
        // accumulates the values of this series up to the category (sic)
        int positive = zero, negative = zero;
        first = second = zero;
        for (std::uint32_t k = 0; k <= category; ++k) {
            const float value = data_[series][k];
            if (value > 0.0f) {
                const int top = positive - ValueAmountToPixelAmount(value, valueAxis);
                first = top; second = positive; positive = top;
            } else if (value < 0.0f) {
                const int bottom = negative - ValueAmountToPixelAmount(value, valueAxis);
                first = negative; second = bottom; negative = bottom;
            }
        }
    }
    const int edge = static_cast<int>(barRight);
    if (vertical) rect = cRZRect{barLeft, first, edge, second};
    else rect = cRZRect{first, barLeft, second, edge};
    return true;
}

bool cGZBarGraph::GetRectOfBarsInCategory(std::uint32_t category, cRZRect& rect) {   // @ 0x100380994
    if (data_.empty() || data_[0].size() <= category) {
        rect = cRZRect{0, 0, 0, 0};
        return false;
    }
    for (std::uint32_t s = 0; s < data_.size(); ++s) {
        if (s == 0) {
            GetRectOfBar(category, 0, rect);
        } else {
            cRZRect other;
            GetRectOfBar(category, s, other);
            rect.left = std::min(rect.left, other.left);
            rect.top = std::min(rect.top, other.top);
            rect.right = std::max(rect.right, other.right);
            rect.bottom = std::max(rect.bottom, other.bottom);
        }
    }
    return true;
}

bool cGZBarGraph::DoHitTest(cRZPoint& point, std::uint32_t& result) {   // @ 0x100380ab2
    for (std::uint32_t s = 0; s < data_.size(); ++s) {
        for (std::uint32_t c = 0; c < data_[s].size(); ++c) {
            cRZRect rect;
            GetRectOfBar(c, s, rect);
            if (rect.left <= point.x && rect.top <= point.y && point.x <= rect.right && point.y <= rect.bottom) {
                AllocateHitTestData(12);
                std::fill(hitData_, hitData_ + 12, 0);
                reinterpret_cast<std::int32_t*>(hitData_)[1] = static_cast<std::int32_t>(s);
                reinterpret_cast<std::uint32_t*>(hitData_)[2] = c;
                result = kBarHitTestResult;
                return true;
            }
        }
    }
    return false;
}

float cGZBarGraph::GetHighestAxisValue(Axis) {   // @ 0x100381a12
    if (type_ != kBarStacked) {
        float best = -FLT_MAX;
        for (const std::vector<float>& values : data_)
            for (float v : values) best = v <= best ? best : v;
        return best;
    }
    if (data_.empty()) return 100.0f;
    std::size_t longest = 0;
    for (const std::vector<float>& values : data_) longest = std::max(longest, values.size());
    float best = -FLT_MAX;
    for (std::size_t i = 0; i < longest; ++i) {
        float sum = 0.0f;
        for (const std::vector<float>& values : data_)
            if (i < values.size() && 0.0f < values[i]) sum = sum + values[i];
        best = sum <= best ? best : sum;
    }
    return best;
}

float cGZBarGraph::GetLowestAxisValue(Axis) {   // @ 0x100381c50
    if (type_ != kBarStacked) {
        float best = FLT_MAX;
        for (const std::vector<float>& values : data_)
            for (float v : values) best = best <= v ? best : v;
        return best;
    }
    if (data_.empty()) return 100.0f;
    std::size_t longest = 0;
    for (const std::vector<float>& values : data_) longest = std::max(longest, values.size());
    float best = FLT_MAX;
    for (std::size_t i = 0; i < longest; ++i) {
        float sum = 0.0f;
        for (const std::vector<float>& values : data_)
            if (i < values.size() && values[i] < 0.0f) sum = sum + values[i];
        best = best <= sum ? best : sum;
    }
    return best;
}

int cGZBarGraph::CalculateMaximumBarWidth() {   // @ 0x100381e8e
    float width = static_cast<float>(graphArea_.right - graphArea_.left) * widthFraction_;
    if (type_ == kBarClustered && !data_.empty()) width = width / static_cast<float>(data_.size());
    return static_cast<int>(width);
}

void cGZBarGraph::CalculateBarWidthAndSpacing() {   // @ 0x100381f12
    const std::uint32_t extent = orientation_ == kOrientationVertical ? static_cast<std::uint32_t>(graphArea_.right - graphArea_.left)
                                                                      : static_cast<std::uint32_t>(graphArea_.bottom - graphArea_.top);
    std::uint32_t categories;
    float width;
    if (type_ == kBarClustered) {
        if (data_.empty() || data_[0].empty()) {
            width = static_cast<float>(CalculateMaximumBarWidth());
            categories = 1;
        } else {
            categories = static_cast<std::uint32_t>(data_[0].size());
            width = (static_cast<float>(extent) * widthFraction_) / static_cast<float>(data_.size() * categories);
        }
        barWidth_ = width;
        spacing_ = (static_cast<float>(extent) - static_cast<float>(categories) * width * static_cast<float>(data_.size())) /
                   static_cast<float>(static_cast<int>(categories) + 1);
    } else if (type_ == kBarStacked) {
        if (data_.empty() || data_[0].empty()) {
            width = static_cast<float>(CalculateMaximumBarWidth());
            categories = 1;
        } else {
            categories = static_cast<std::uint32_t>(data_[0].size());
            width = (static_cast<float>(extent) * widthFraction_) / static_cast<float>(categories);
        }
        barWidth_ = width;
        spacing_ = (static_cast<float>(extent) - static_cast<float>(categories) * width) /
                   static_cast<float>(static_cast<int>(categories) + 1);
    }
}

void cGZBarGraph::CompleteRedraw(cIGZBuffer* buffer) {   // @ 0x10038210c
    if (buffer == nullptr) {
        IGraphHost* host = GraphHost();
        buffer = host ? host->WindowBuffer(drawBuffer_) : nullptr;
        if (buffer == nullptr) return;
    }
    CalculateBarWidthAndSpacing();
    cGZGraph::CompleteRedraw(buffer);
}

// ---------------------------------------------------------------------------------------------------------------
// Drawing

void cGZBarGraph::DrawBar(cIGZBuffer* buffer, cRZRect& rect, std::uint32_t series, BarDirection) {   // @ 0x1003821d6
    const BarGraphSeriesInfo& info = series_[series];
    if (info.pattern == nullptr) {
        if (info.fill && buffer->Lock(Flags(kLockFill))) {
            buffer->Fill(info.fillColor, rect, nullptr);
            buffer->Unlock(Flags(kLockFill));
        }
        if (!info.outline) return;
        if (!buffer->Lock(Flags(kLockOutline))) return;
        DrawRectangleOutline(buffer, rect, info.outlineColor, 1);
        buffer->Unlock(Flags(kLockOutline));
    } else {
        if (!buffer->Lock(Flags(kLockBlt))) return;
        const cRZRect source{0, 0, static_cast<int>(info.pattern->Width()), static_cast<int>(info.pattern->Height())};
        RZDrawBltTiled(buffer, info.pattern, source, rect, 0, 0, nullptr);
        buffer->Unlock(Flags(kLockBlt));
    }
}

void cGZBarGraph::DrawAllBars(cIGZBuffer* buffer) {   // @ 0x100380bf2
    if (data_.empty()) return;
    const bool valueOnVertical = orientation_ == kOrientationVertical;
    const Axis valueAxis = valueOnVertical ? kAxisVertical : kAxisHorizontal;
    const std::uint32_t seriesCount = static_cast<std::uint32_t>(data_.size());
    if (type_ == kBarClustered) {
        const std::uint32_t categories = static_cast<std::uint32_t>(data_[0].size());
        if (categories == 0) return;
        float left = static_cast<float>(graphArea_.left) + spacing_;
        float right = left + barWidth_;
        for (std::uint32_t c = 0; c < categories; ++c) {
            for (std::uint32_t s = 0; s < seriesCount; ++s) {
                // note: the series vectors are indexed by the category without a length check, as in the binary
                const float value = data_[s][c];
                if (value != 0.0f) {
                    int top, bottom;
                    BarDirection direction;
                    if (value > 0.0f) {
                        top = ValueToPixel(value, valueAxis, true);
                        bottom = zeroPosition_[valueOnVertical ? 1 : 0];
                        direction = kBarUp;
                    } else {
                        top = zeroPosition_[valueOnVertical ? 1 : 0];
                        bottom = ValueToPixel(value, valueAxis, true);
                        direction = kBarDown;
                    }
                    cRZRect rect{static_cast<int>(left), static_cast<int>(static_cast<float>(top)), static_cast<int>(right),
                                 static_cast<int>(static_cast<float>(bottom))};
                    DrawBar(buffer, rect, s, direction);
                }
                left += barWidth_;
                right += barWidth_;
            }
            left += spacing_;
            right += spacing_;
        }
    } else if (type_ == kBarStacked) {
        std::uint32_t longest = 0;
        std::vector<std::uint32_t> lengths;
        for (const std::vector<float>& values : data_) {
            lengths.push_back(static_cast<std::uint32_t>(values.size()));
            longest = std::max(longest, lengths.back());
        }
        if (longest == 0) return;
        float left = static_cast<float>(graphArea_.left) + spacing_;
        float right = barWidth_ + left;
        for (std::uint32_t c = 0; c < longest; ++c) {
            const int zero = zeroPosition_[valueOnVertical ? 1 : 0];
            const int leftPixel = static_cast<int>(left);
            const int rightPixel = static_cast<int>(right);
            int positive = zero, negative = zero;
            for (std::uint32_t s = 0; s < seriesCount; ++s) {
                if (c >= lengths[s]) continue;
                const float value = data_[s][c];
                if (value == 0.0f) continue;
                int top, bottom;
                BarDirection direction;
                if (value > 0.0f) {
                    const int pixels = ValueAmountToPixelAmount(value, valueAxis);
                    bottom = positive;
                    positive = static_cast<int>(static_cast<float>(positive - pixels));
                    top = positive;
                    direction = kBarUp;
                } else if (value < 0.0f) {
                    const int pixels = ValueAmountToPixelAmount(value, valueAxis);
                    top = negative;
                    negative = static_cast<int>(static_cast<float>(negative - pixels));
                    bottom = negative;
                    direction = kBarDown;
                } else {
                    continue;
                }
                cRZRect rect{leftPixel, top, rightPixel, bottom};
                DrawBar(buffer, rect, s, direction);
            }
            left = left + (spacing_ + barWidth_);
            right = barWidth_ + left;
        }
    }
}

void cGZBarGraph::DrawTicks(cIGZBuffer* buffer, std::uint32_t axisMask) {   // @ 0x100381118
    if ((axisMask & 2) && orientation_ == kOrientationVertical) cGZGraph::DrawTicks(buffer, 2);
    if ((axisMask & 1) && orientation_ == kOrientationHorizontal) cGZGraph::DrawTicks(buffer, 1);
    if (!majorTicks_[0] && !minorTicks_[0] && !majorTicks_[1] && !minorTicks_[1]) return;
    if (data_.empty()) return;
    if (!buffer->Lock(Flags(kLockLine))) return;
    const int color = static_cast<int>(tickColor_);
    const std::uint32_t categories = static_cast<std::uint32_t>(data_[0].size());
    auto groupWidth = [this]() {
        return (type_ == kBarStacked || data_.size() == 1) ? barWidth_ : static_cast<float>(data_.size()) * barWidth_;
    };
    if ((axisMask & 1) && orientation_ == kOrientationVertical) {
        int majorEnd = 0, majorStart = 0, minorEnd = 0, minorStart = 0;
        if (majorTicks_[0]) {
            majorEnd = majorLength_[0] + graphArea_.bottom;
            majorStart = majorGrid_[0] ? graphArea_.top : graphArea_.bottom;
        }
        if (minorTicks_[0]) {
            minorEnd = minorLength_[0] + graphArea_.bottom;
            minorStart = minorGrid_[0] ? graphArea_.top : graphArea_.bottom;
        }
        if (majorTicks_[0] || minorTicks_[0]) {
            for (std::uint32_t i = 0; i < categories; ++i) {
                const int left = GetLeftOfBar(i, 0);
                if (majorTicks_[0]) {
                    const int x = static_cast<int>(spacing_ * -0.5f + static_cast<float>(left));
                    DrawLine(buffer, x, majorStart, x, majorEnd, color, 1);
                }
                if (minorTicks_[0]) {
                    const int x = static_cast<int>(groupWidth() * 0.5f + static_cast<float>(left));
                    DrawLine(buffer, x, minorStart, x, minorEnd, color, 1);
                }
            }
        }
    }
    if ((axisMask & 2) && orientation_ == kOrientationHorizontal) {
        int majorStart = 0, majorEnd = 0, minorStart = 0, minorEnd = 0;
        if (majorTicks_[1]) {
            majorStart = graphArea_.left - majorLength_[1];
            majorEnd = majorGrid_[1] ? graphArea_.right : graphArea_.left;
        }
        if (minorTicks_[1]) {
            minorStart = graphArea_.left - minorLength_[1];
            minorEnd = minorGrid_[1] ? graphArea_.right : graphArea_.left;
        }
        if (majorTicks_[1] || minorTicks_[1]) {
            int strayStart = 0, strayEnd = 0;   // the clustered minor ticks reuse leftovers of the previous call (sic)
            for (std::uint32_t i = 0; i < categories; ++i) {
                const int left = GetLeftOfBar(i, 0);
                if (majorTicks_[1]) {
                    const int y = static_cast<int>(spacing_ * -0.5f + static_cast<float>(left));
                    DrawLine(buffer, majorStart, y, majorEnd, y, color, 1);
                }
                if (minorTicks_[1]) {
                    const int y = static_cast<int>(groupWidth() * 0.5f + static_cast<float>(left));
                    if (type_ == kBarStacked || data_.size() == 1) {
                        DrawLine(buffer, minorStart, y, minorEnd, y, color, 1);
                        strayStart = strayEnd = y;
                    } else {
                        DrawLine(buffer, y, strayStart, y, strayEnd, color, 1);
                    }
                }
            }
        }
    }
    buffer->Unlock(Flags(kLockLine));
}

void cGZBarGraph::DrawTickText(cIGZBuffer* buffer, std::uint32_t axisMask) {   // @ 0x1003815b2
    auto colorOf = [](std::uint32_t c) { return c == kAutoColor ? kFontDefaultColor : c; };
    auto names = [this](auto&& place) {
        const std::size_t count = std::min(categoryNames_.size(), data_[0].size());
        for (std::size_t k = 0; k < count; ++k) {
            const cRZString& name = categoryNames_[k];
            if (name.Strlen() == 0) continue;
            cRZRect rect;
            GetRectOfBarsInCategory(static_cast<std::uint32_t>(k), rect);
            place(name, rect);
        }
    };
    if (orientation_ == kOrientationHorizontal) {
        if ((axisMask & 1) && tickText_[0]) cGZGraph::DrawTickText(buffer, 1);
        if ((axisMask & 2) && tickText_[1] && !data_.empty() && tickFont_[1]) {
            cIGZFont* font = tickFont_[1];
            const int height = font->CharHeight(nullptr, 0);
            const int margin = GetMaxTickLengthForAxis(kAxisVertical);
            names([&](const cRZString& name, cRZRect rect) {
                const int width = font->GetStringWidth(name.Data(), name.Strlen());
                rect.right = graphArea_.left - margin;
                rect.left = rect.right - width;
                rect.top = (rect.top - height / 2) + (rect.bottom - rect.top) / 2;
                rect.bottom = rect.top + height + 1;
                font->DrawTextParagraph(buffer, rect, name.Data(), name.Strlen(), 1, colorOf(tickFontColor_[1]),
                                        kFontDefaultColor, nullptr, nullptr);
            });
        }
    } else if (orientation_ == kOrientationVertical) {
        if ((axisMask & 2) && tickText_[1]) cGZGraph::DrawTickText(buffer, 2);
        if ((axisMask & 1) && tickText_[0] && !data_.empty() && tickFont_[0]) {
            cIGZFont* font = tickFont_[0];
            const int height = font->CharHeight(nullptr, 0);
            const int margin = GetMaxTickLengthForAxis(kAxisHorizontal);
            names([&](const cRZString& name, cRZRect rect) {
                const int width = font->GetStringWidth(name.Data(), name.Strlen());
                const int top = graphArea_.bottom + margin;
                const int half = (rect.right - rect.left) / 2;
                const int start = rect.left - width / 2;
                rect.left = half - 1 + start;
                rect.right = width + 1 + start + half;
                rect.top = top;
                rect.bottom = top + height;
                font->DrawTextParagraph(buffer, rect, name.Data(), name.Strlen(), 1, colorOf(tickFontColor_[0]),
                                        kFontDefaultColor, nullptr, nullptr);
            });
        }
    }
}

}  // namespace gz
