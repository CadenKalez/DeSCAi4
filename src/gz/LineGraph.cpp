#include "gz/LineGraph.h"

#include <algorithm>
#include <cfloat>

namespace gz {

namespace {
constexpr std::uint32_t kLockLine = 0x8004;
constexpr std::uint32_t kAll = 0xffffffffu;
inline cIGZBuffer::eLockFlags Flags(std::uint32_t f) { return static_cast<cIGZBuffer::eLockFlags>(f); }
}  // namespace

cGZLineGraph::cGZLineGraph(cIGZWinMgr* mgr) : cGZGraph(mgr) {   // @ 0x10037c342
    majorTicks_[0] = false; majorTicks_[1] = true;
    majorGrid_[0] = false; majorGrid_[1] = true;
}

cGZLineGraph::~cGZLineGraph() {   // @ 0x10037c496
    if (symbolFont_) { symbolFont_->Release(); symbolFont_ = nullptr; }
    cGZGraph::Shutdown();
}

bool cGZLineGraph::QueryInterface(std::uint32_t iid, void** out) {   // @ 0x10037c63c
    if (iid == kGZIID_cIGZGraph) {
        *out = static_cast<cIGZGraph*>(this);
    } else if (iid == kGZIID_cIGZLineGraph) {
        *out = static_cast<cIGZLineGraph*>(this);
    } else {
        return cGZWin::QueryInterface(iid, out);
    }
    AddRef();
    return true;
}

bool cGZLineGraph::Shutdown() {   // @ 0x10037c5ac
    if (symbolFont_) { symbolFont_->Release(); symbolFont_ = nullptr; }
    return cGZGraph::Shutdown();
}

void cGZLineGraph::LoadDefaultFonts() {   // @ 0x10037c698
    if (symbolFont_ == nullptr) {
        IGraphHost* host = GraphHost();
        if (host && host->FontsAvailable()) {
            cIGZFont* font = host->CreateFont("windings", 12);   // sic
            if (font == nullptr) font = host->CreateFont("dingbats", 12);
            if (font) {
                SetSymbolFont(font, kAutoColor);
                font->Release();
            }
        }
    }
    cGZGraph::LoadDefaultFonts();
}

void cGZLineGraph::SetSymbolFont(cIGZFont* font, std::uint32_t color) {   // @ 0x10037c7fe
    ReplaceFont(symbolFont_, font);
    if (color != kAutoColor) symbolFontColor_ = color;
}

std::uint32_t cGZLineGraph::OuterIndex(std::uint32_t series) const {
    return (data_.size() == 1 && series == kAll) ? 0 : series;
}

void cGZLineGraph::SetSeriesCount(std::uint32_t count) {   // @ 0x10037c888
    data_.resize(count);
    series_.resize(count);
    for (std::uint32_t i = 0; i < count; ++i) series_[i] = LineGraphSeriesInfo();
}

void cGZLineGraph::InsertSeries(std::uint32_t index) {   // @ 0x10037c9aa
    if (index == kAll) {
        data_.emplace_back();
        series_.emplace_back();
    } else {
        if (data_.size() <= index) return;
        data_.insert(data_.begin() + index, std::vector<float>());
        series_.insert(series_.begin() + index, LineGraphSeriesInfo());
    }
}

void cGZLineGraph::RemoveSeries(std::uint32_t index) {   // @ 0x10037d2da
    if (index == kAll) {
        data_.pop_back();
        series_.pop_back();
        return;
    }
    if (data_.size() <= index) return;
    data_.erase(data_.begin() + index);
    series_.erase(series_.begin() + index);
}

void cGZLineGraph::RemoveAllSeries() {   // @ 0x10037d258
    for (std::size_t n = data_.size(); n != 0; --n) RemoveSeries(kAll);
}

void cGZLineGraph::SetSymbolUse(Usage symbol, Usage line, std::uint32_t series) {   // @ 0x10037d472
    if (series == kAll) {
        for (LineGraphSeriesInfo& info : series_) { info.symbolUse = symbol != 0; info.lineUse = line != 0; }
    } else if (series < series_.size()) {
        series_[series].symbolUse = symbol != 0;
        series_[series].lineUse = line != 0;
    }
}

void cGZLineGraph::SetSymbolCharacter(char symbol, std::uint32_t series) {   // @ 0x10037d51c
    if (series == kAll) { for (LineGraphSeriesInfo& info : series_) info.symbol = symbol; }
    else if (series < series_.size()) series_[series].symbol = symbol;
}

void cGZLineGraph::SetSymbolSize(std::uint32_t size, std::uint32_t series) {   // @ 0x10037d59e
    if (series == kAll) { for (LineGraphSeriesInfo& info : series_) info.symbolSize = size; }
    else if (series < series_.size()) series_[series].symbolSize = size;
}

void cGZLineGraph::SetSeriesColor(std::uint32_t color, std::uint32_t series) {   // @ 0x10037d620
    if (series == kAll) { for (LineGraphSeriesInfo& info : series_) info.color = color; }
    else if (series < series_.size()) series_[series].color = color;
}

void cGZLineGraph::SetSeriesName(const cRZString& name, std::uint32_t series) {   // @ 0x10037d6a2
    if (series == kAll) { for (LineGraphSeriesInfo& info : series_) info.name.AssignFromIGZString(name); }
    else if (series < series_.size()) series_[series].name.AssignFromIGZString(name);
}

void cGZLineGraph::SetSeriesInfo(LineGraphSeriesInfo& info, std::uint32_t series) {   // @ 0x10037d7de
    if (series < series_.size()) series_[series] = info;
}

void cGZLineGraph::InsertSeriesInfo(LineGraphSeriesInfo& info, std::uint32_t series) {   // @ 0x10037d86c
    if (series == kAll) series_.push_back(info);
    else if (series < series_.size()) series_.insert(series_.begin() + series, info);
}

void cGZLineGraph::RemoveSeriesInfo(std::uint32_t series) {   // @ 0x10037d958
    if (series == kAll) series_.pop_back();
    else if (series < series_.size()) series_.erase(series_.begin() + series);
}

void cGZLineGraph::InsertCategoryName(const cRZString& name, std::uint32_t index) {   // @ 0x10037db6e
    if (index == kAll) index = static_cast<std::uint32_t>(categoryNames_.size());
    if (categoryNames_.size() <= index) categoryNames_.resize(static_cast<std::size_t>(index) + 1);
    categoryNames_[index].AssignFromIGZString(name);
}

void cGZLineGraph::RemoveCategoryName(std::uint32_t index) {   // @ 0x10037dc20
    if (index == kAll) index = static_cast<std::uint32_t>(categoryNames_.size()) - 1;
    if (index < categoryNames_.size()) categoryNames_[index].AssignFromCString("");
}

float cGZLineGraph::GetHighestAxisValue(Axis) {   // @ 0x10037da46
    float best = -FLT_MAX;
    for (const std::vector<float>& values : data_)
        for (float v : values) best = v <= best ? best : v;
    return best;
}

float cGZLineGraph::GetLowestAxisValue(Axis) {   // @ 0x10037dada
    float best = FLT_MAX;
    for (const std::vector<float>& values : data_)
        for (float v : values) best = best <= v ? best : v;
    return best;
}

void cGZLineGraph::InsertData(float value, std::uint32_t series, std::uint32_t position) {   // @ 0x10037dd00
    series = OuterIndex(series);
    if (series >= data_.size()) return;
    std::vector<float>& values = data_[series];
    if (position == kAll || position >= values.size()) values.push_back(value);
    else values.insert(values.begin() + position, value);
}

void cGZLineGraph::RemoveData(std::uint32_t position, std::uint32_t series) {   // @ 0x10037e09e
    series = OuterIndex(series);
    if (series >= data_.size()) return;
    std::vector<float>& values = data_[series];
    if (position < values.size()) values.erase(values.begin() + position);
}

void cGZLineGraph::SetData(float value, std::uint32_t position, std::uint32_t series) {   // @ 0x10037d752
    if (series >= data_.size()) return;
    std::vector<float>& values = data_[series];
    if (position == kAll) values.push_back(value);
    else if (position < values.size()) values[position] = value;
}

void cGZLineGraph::SetData(std::vector<float>& values, std::uint32_t series) {   // @ 0x10037e14e
    series = OuterIndex(series);
    if (series < data_.size() && &data_[series] != &values) data_[series].assign(values.begin(), values.end());
}

void cGZLineGraph::AppendData(std::vector<float>& values, std::uint32_t series) {   // @ 0x10037e1b8
    series = OuterIndex(series);
    if (series >= data_.size()) return;
    for (float v : values) data_[series].push_back(v);
}

int cGZLineGraph::GetCenterOfDataPoint(std::uint32_t index) {   // @ 0x10037e588
    float denominator = 1.0f;
    if (!data_.empty()) denominator = static_cast<float>(std::max<std::uint32_t>(2, static_cast<std::uint32_t>(data_[0].size())) - 1);
    if (orientation_ == 0)
        return static_cast<int>((static_cast<float>(graphArea_.right - graphArea_.left) * static_cast<float>(index)) / denominator) + graphArea_.left;
    return graphArea_.bottom - static_cast<int>((static_cast<float>(graphArea_.bottom - graphArea_.top) * static_cast<float>(index)) / denominator);
}

void cGZLineGraph::DrawSymbol(cIGZBuffer* buffer, cRZPoint& point, std::uint32_t series) {   // @ 0x10037e630
    if (series >= series_.size() || symbolFont_ == nullptr) return;
    const char symbol = series_[series].symbol;
    const std::uint32_t color = series_[series].color;
    const int width = symbolFont_->CharWidth(&symbol, 1);
    const int height = symbolFont_->CharHeight(nullptr, 0);
    const int x = point.x - static_cast<int>(static_cast<std::uint32_t>(width) >> 1);
    const int y = point.y - static_cast<int>(static_cast<std::uint32_t>(height) >> 1);
    if (color == kAutoColor) {
        symbolFont_->DrawTextLine(buffer, x, y, &symbol, 1, kFontDefaultColor, kFontDefaultColor, nullptr, nullptr);
    } else {
        const std::uint32_t previous = symbolFont_->GetFontColor();
        symbolFont_->SetFontColor(color, kFontDefaultColor);
        symbolFont_->DrawTextLine(buffer, x, y, &symbol, 1, kFontDefaultColor, kFontDefaultColor, nullptr, nullptr);
        symbolFont_->SetFontColor(previous, kFontDefaultColor);
    }
}

void cGZLineGraph::DrawLine(cIGZBuffer* buffer, cRZRect& segment, std::uint32_t series) {   // @ 0x10037e798
    if (series < series_.size())
        DrawLine(buffer, segment.left, segment.top, segment.right, segment.bottom, static_cast<int>(series_[series].color),
                 series_[series].thickness);
}

void cGZLineGraph::DrawGraphData(cIGZBuffer* buffer) {   // @ 0x10037e292
    if (data_.empty()) return;
    std::uint32_t pointCount = static_cast<std::uint32_t>(data_[0].size());
    if (pointCount == 0) return;
    const Axis valueAxis = orientation_ == 0 ? kAxisVertical : kAxisHorizontal;
    if (buffer->Lock(Flags(kLockLine))) {
        for (std::size_t s = 0; s < data_.size(); ++s) {
            if (!series_[s].lineUse) continue;
            const std::vector<float>& values = data_[s];
            pointCount = static_cast<std::uint32_t>(values.size());
            for (std::uint32_t k = 0; k + 1 < values.size(); ++k) {
                const int x0 = GetCenterOfDataPoint(k);
                const int y0 = ValueToPixel(values[k], valueAxis, true);
                const int x1 = GetCenterOfDataPoint(k + 1);
                const int y1 = ValueToPixel(values[k + 1], valueAxis, true);
                cRZRect segment{x0, y0, x1, y1};
                if (orientation_ == 1) segment = cRZRect{y1, x0, y0, x1};   // sic (not a transpose)
                DrawLine(buffer, segment, static_cast<std::uint32_t>(s));
            }
        }
        buffer->Unlock(Flags(kLockLine));
    }
    if (symbolFont_ && symbolFont_->LockSurfaceForFontUse(buffer)) {
        for (std::size_t s = 0; s < data_.size(); ++s) {
            if (!series_[s].symbolUse) continue;
            for (std::uint32_t k = 0; k < pointCount && k < data_[s].size(); ++k) {   // bounds guard: the binary reads past short series
                const int center = GetCenterOfDataPoint(k);
                const int pixel = ValueToPixel(data_[s][k], valueAxis, true);
                cRZPoint point = orientation_ == 1 ? cRZPoint{pixel, center} : cRZPoint{center, pixel};
                DrawSymbol(buffer, point, static_cast<std::uint32_t>(s));
            }
        }
        symbolFont_->UnlockSurfaceForFontUse(buffer);
    }
}

void cGZLineGraph::DrawTicks(cIGZBuffer* buffer, std::uint32_t axisMask) {   // @ 0x10037e812
    if ((axisMask & 2) && orientation_ == 0) cGZGraph::DrawTicks(buffer, 2);
    if ((axisMask & 1) && orientation_ == 1) cGZGraph::DrawTicks(buffer, 1);
    if (!majorTicks_[0] && !minorTicks_[0] && !majorTicks_[1] && !minorTicks_[1]) return;
    if (data_.empty()) return;
    if (!buffer->Lock(Flags(kLockLine))) return;
    const int color = static_cast<int>(tickColor_);
    const std::uint32_t points = static_cast<std::uint32_t>(data_[0].size());
    // minor ticks mark the data points (the ends sit on the plot edges); major ticks mark the gaps between points
    if ((axisMask & 1) && orientation_ == 0 && (majorTicks_[0] || minorTicks_[0])) {
        int majorEnd = 0, majorStart = 0, minorEnd = 0, minorStart = 0;
        if (majorTicks_[0]) { majorEnd = majorLength_[0] + graphArea_.bottom; majorStart = majorGrid_[0] ? graphArea_.top : graphArea_.bottom; }
        if (minorTicks_[0]) { minorEnd = minorLength_[0] + graphArea_.bottom; minorStart = minorGrid_[0] ? graphArea_.top : graphArea_.bottom; }
        for (std::uint32_t i = 0; i < points;) {
            const int center = GetCenterOfDataPoint(i);
            if (minorTicks_[0]) {
                const int x = i == 0 ? graphArea_.left : (i + 1 == points ? graphArea_.right : center);
                DrawLine(buffer, x, minorStart, x, minorEnd, color, 1);
            }
            ++i;
            if (majorTicks_[0] && i < points) {
                const int x = (GetCenterOfDataPoint(i) + center) / 2;
                DrawLine(buffer, x, majorStart, x, majorEnd, color, 1);
            }
        }
    }
    if ((axisMask & 2) && orientation_ == 1 && (majorTicks_[1] || minorTicks_[1])) {
        int majorStart = 0, majorEnd = 0, minorStart = 0, minorEnd = 0;
        if (majorTicks_[1]) { majorStart = graphArea_.left - majorLength_[1]; majorEnd = majorGrid_[1] ? graphArea_.right : graphArea_.left; }
        if (minorTicks_[1]) { minorStart = graphArea_.left - minorLength_[1]; minorEnd = minorGrid_[1] ? graphArea_.right : graphArea_.left; }
        for (std::uint32_t i = 0; i < points;) {
            const int center = GetCenterOfDataPoint(i);
            if (minorTicks_[1]) {
                const int y = i == 0 ? graphArea_.top : (i + 1 == points ? graphArea_.bottom : center);
                DrawLine(buffer, minorStart, y, minorEnd, y, color, 1);
            }
            ++i;
            if (majorTicks_[1] && i < points) {
                // the binary averages with a stale register here; the neighbouring point is the sensible reading
                const int y = (GetCenterOfDataPoint(i) + center) / 2;
                DrawLine(buffer, majorStart, y, majorEnd, y, color, 1);
            }
        }
    }
    buffer->Unlock(Flags(kLockLine));
}

void cGZLineGraph::DrawTickText(cIGZBuffer* buffer, std::uint32_t axisMask) {   // @ 0x10037ec30
    auto colorOf = [](std::uint32_t c) { return c == kAutoColor ? kFontDefaultColor : c; };
    const std::size_t count = data_.empty() ? 0 : std::min(categoryNames_.size(), data_[0].size());
    if (orientation_ == 1) {
        if ((axisMask & 1) && tickText_[0]) cGZGraph::DrawTickText(buffer, 1);
        if ((axisMask & 2) && tickText_[1] && !data_.empty() && tickFont_[1]) {
            cIGZFont* font = tickFont_[1];
            const int height = font->CharHeight(nullptr, 0);
            const int margin = GetMaxTickLengthForAxis(kAxisVertical);
            for (std::size_t k = 0; k < count; ++k) {
                const cRZString& name = categoryNames_[k];
                if (name.Strlen() == 0) continue;
                cRZRect rect;
                rect.right = graphArea_.left - margin;
                rect.left = rect.right - font->GetStringWidth(name.Data(), name.Strlen());
                rect.top = GetCenterOfDataPoint(static_cast<std::uint32_t>(k)) - height / 2;
                if (rect.top < graphArea_.top) rect.top = graphArea_.top;
                rect.bottom = height + 2 + rect.top;
                if (graphArea_.bottom < rect.bottom) {
                    rect.top = (graphArea_.bottom - rect.bottom) + rect.top;
                    rect.bottom = graphArea_.bottom;
                }
                font->DrawTextParagraph(buffer, rect, name.Data(), name.Strlen(), 1, colorOf(tickFontColor_[1]), kFontDefaultColor,
                                        nullptr, nullptr);
            }
        }
    } else if (orientation_ == 0) {
        if ((axisMask & 2) && tickText_[1]) cGZGraph::DrawTickText(buffer, 2);
        if ((axisMask & 1) && tickText_[0] && !data_.empty() && tickFont_[0]) {
            cIGZFont* font = tickFont_[0];
            const int height = font->CharHeight(nullptr, 0);
            const int margin = GetMaxTickLengthForAxis(kAxisHorizontal);
            for (std::size_t k = 0; k < count; ++k) {
                const cRZString& name = categoryNames_[k];
                if (name.Strlen() == 0) continue;
                const int width = font->GetStringWidth(name.Data(), name.Strlen());
                cRZRect rect;
                rect.top = graphArea_.bottom + margin;
                rect.bottom = rect.top + height;
                const int center = GetCenterOfDataPoint(static_cast<std::uint32_t>(k));
                const int start = center - width / 2;
                rect.right = width + 1 + start;
                rect.left = (center - 1) - width / 2;
                if (start <= graphArea_.left) { rect.right = width + 2 + graphArea_.left; rect.left = graphArea_.left; }
                if (graphArea_.right < rect.right) {
                    rect.left = (graphArea_.right - rect.right) + rect.left;
                    rect.right = graphArea_.right;
                }
                font->DrawTextParagraph(buffer, rect, name.Data(), name.Strlen(), 1, colorOf(tickFontColor_[0]), kFontDefaultColor,
                                        nullptr, nullptr);
            }
        }
    }
}

}  // namespace gz
