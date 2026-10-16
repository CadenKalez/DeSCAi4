#include "gz/ScatterGraph.h"

#include <cfloat>

namespace gz {

namespace {
constexpr std::uint32_t kAll = 0xffffffffu;
}

cGZScatterGraph::cGZScatterGraph(cIGZWinMgr* mgr) : cGZGraph(mgr) {   // @ 0x10038232a
    tickText_[0] = tickText_[1] = true;
}

cGZScatterGraph::~cGZScatterGraph() {   // @ 0x1003823b2
    if (symbolFont_) { symbolFont_->Release(); symbolFont_ = nullptr; }
    cGZGraph::Shutdown();
}

bool cGZScatterGraph::QueryInterface(std::uint32_t iid, void** out) {   // @ 0x10038253a
    if (iid == kGZIID_cIGZGraph) {
        *out = static_cast<cIGZGraph*>(this);
    } else if (iid == kGZIID_cIGZScatterGraph) {
        *out = static_cast<cIGZScatterGraph*>(this);
    } else {
        return cGZWin::QueryInterface(iid, out);
    }
    AddRef();
    return true;
}

bool cGZScatterGraph::Shutdown() {   // @ 0x1003824aa
    if (symbolFont_) { symbolFont_->Release(); symbolFont_ = nullptr; }
    return cGZGraph::Shutdown();
}

void cGZScatterGraph::LoadDefaultFonts() {   // @ 0x100382596
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

void cGZScatterGraph::SetSymbolFont(cIGZFont* font, std::uint32_t color) {   // @ 0x1003826fc
    ReplaceFont(symbolFont_, font);
    if (color != kAutoColor) symbolFontColor_ = color;
}

std::uint32_t cGZScatterGraph::OuterIndex(std::uint32_t series) const {
    return (data_.size() == 1 && series == kAll) ? 0 : series;
}

void cGZScatterGraph::SetSeriesCount(std::uint32_t count) {   // @ 0x100382770
    data_.resize(count);
    series_.resize(count);
    for (std::uint32_t i = 0; i < count; ++i) series_[i] = ScatterGraphSeriesInfo();
}

void cGZScatterGraph::InsertSeries(std::uint32_t index) {   // @ 0x10038292c
    if (index == kAll) {
        data_.emplace_back();
        series_.emplace_back();
    } else {
        if (data_.size() <= index) return;
        data_.insert(data_.begin() + index, std::vector<cRZ2DPointFloat32>());
        series_.insert(series_.begin() + index, ScatterGraphSeriesInfo());
    }
}

void cGZScatterGraph::RemoveSeries(std::uint32_t index) {   // @ 0x1003831aa
    if (index == kAll) {
        data_.pop_back();
        series_.pop_back();
        return;
    }
    if (data_.size() <= index) return;
    data_.erase(data_.begin() + index);
    series_.erase(series_.begin() + index);
}

void cGZScatterGraph::SetSymbolCharacter(char symbol, std::uint32_t series) {   // @ 0x100383340
    if (series == kAll) { for (ScatterGraphSeriesInfo& info : series_) info.symbol = symbol; }
    else if (series < series_.size()) series_[series].symbol = symbol;
}

void cGZScatterGraph::SetSymbolSize(std::uint32_t size, std::uint32_t series) {   // @ 0x1003833c0
    if (series == kAll) { for (ScatterGraphSeriesInfo& info : series_) info.symbolSize = size; }
    else if (series < series_.size()) series_[series].symbolSize = size;
}

void cGZScatterGraph::SetSeriesColor(std::uint32_t color, std::uint32_t series) {   // @ 0x100383442
    if (series == kAll) { for (ScatterGraphSeriesInfo& info : series_) info.color = color; }
    else if (series < series_.size()) series_[series].color = color;
}

void cGZScatterGraph::SetSeriesName(const cRZString& name, std::uint32_t series) {   // @ 0x1003834c4
    if (series == kAll) { for (ScatterGraphSeriesInfo& info : series_) info.name.AssignFromIGZString(name); }
    else if (series < series_.size()) series_[series].name.AssignFromIGZString(name);
}

void cGZScatterGraph::SetSeriesLineDrawing(bool lines, std::uint32_t series) {   // @ 0x100383574
    if (series == kAll) { for (ScatterGraphSeriesInfo& info : series_) info.drawLines = lines; }
    else if (series < series_.size()) series_[series].drawLines = lines;
}

void cGZScatterGraph::SetSeriesInfo(ScatterGraphSeriesInfo& info, std::uint32_t series) {   // @ 0x1003835fa
    if (series < series_.size()) series_[series] = info;
}

void cGZScatterGraph::InsertSeriesInfo(ScatterGraphSeriesInfo& info, std::uint32_t series) {   // @ 0x100383662
    if (series == kAll) series_.push_back(info);
    else if (series < series_.size()) series_.insert(series_.begin() + series, info);
}

void cGZScatterGraph::RemoveSeriesInfo(std::uint32_t series) {   // @ 0x10038374e
    if (series == kAll) series_.pop_back();
    else if (series < series_.size()) series_.erase(series_.begin() + series);
}

void cGZScatterGraph::InsertData(const cRZ2DPointFloat32& point, std::uint32_t series, std::uint32_t position) {   // @ 0x100383c2a
    series = OuterIndex(series);
    if (series >= data_.size()) return;
    std::vector<cRZ2DPointFloat32>& points = data_[series];
    if (position == kAll || position >= points.size()) points.push_back(point);
    else points.insert(points.begin() + position, point);
}

void cGZScatterGraph::RemoveData(std::uint32_t position, std::uint32_t series) {   // @ 0x100384022
    series = OuterIndex(series);
    if (series >= data_.size()) return;
    std::vector<cRZ2DPointFloat32>& points = data_[series];
    if (position < points.size()) points.erase(points.begin() + position);
}

void cGZScatterGraph::SetData(const cRZ2DPointFloat32& point, std::uint32_t position, std::uint32_t series) {   // @ 0x1003840d2
    if (series >= data_.size()) return;
    std::vector<cRZ2DPointFloat32>& points = data_[series];
    if (position == kAll) points.push_back(point);
    else if (position < points.size()) points[position] = point;
}

void cGZScatterGraph::SetData(const std::vector<cRZ2DPointFloat32>& points, std::uint32_t series) {   // @ 0x1003841a8
    series = OuterIndex(series);
    if (series < data_.size() && &data_[series] != &points) data_[series].assign(points.begin(), points.end());
}

void cGZScatterGraph::AppendData(const std::vector<cRZ2DPointFloat32>& points, std::uint32_t series) {   // @ 0x100384212
    series = OuterIndex(series);
    if (series >= data_.size()) return;
    for (const cRZ2DPointFloat32& p : points) data_[series].push_back(p);
}

float cGZScatterGraph::GetHighestAxisValue(Axis axis) {   // @ 0x1003839f6
    float best = -FLT_MAX;
    if (axis != kAxisHorizontal && axis != kAxisVertical) return best;
    for (const std::vector<cRZ2DPointFloat32>& points : data_)
        for (const cRZ2DPointFloat32& p : points) {
            const float v = axis == kAxisVertical ? p.y : p.x;
            best = v <= best ? best : v;
        }
    return best;
}

float cGZScatterGraph::GetLowestAxisValue(Axis axis) {   // @ 0x100383b10
    float best = FLT_MAX;
    if (axis != kAxisHorizontal && axis != kAxisVertical) return best;
    for (const std::vector<cRZ2DPointFloat32>& points : data_)
        for (const cRZ2DPointFloat32& p : points) {
            const float v = axis == kAxisVertical ? p.y : p.x;
            best = best <= v ? best : v;
        }
    return best;
}

void cGZScatterGraph::GetAutomaticGraphAreaRect(cRZRect& out) {   // @ 0x10038383a
    cRZRect area;
    cGZGraph::GetAutomaticGraphAreaRect(area);
    const int width = area.right - area.left, height = area.bottom - area.top;
    if (height < width) area.right = area.left + height;
    else if (height > width) area.bottom = area.top + width;
    out = area;
}

void cGZScatterGraph::GetAutomaticAxisMinValue(Axis axis, float& out) {   // @ 0x1003838a8
    if (!sharedRange_) {
        const float low = GetLowestAxisValue(axis);
        out = low < 0.0f ? low / 0.9f : 0.0f;
    } else {
        float a = GetLowestAxisValue(kAxisHorizontal), b = GetLowestAxisValue(kAxisVertical);
        a = 0.0f <= a ? 0.0f : a;
        b = 0.0f <= b ? 0.0f : b;
        out = (b <= a ? b : a) / 0.9f;
    }
}

void cGZScatterGraph::GetAutomaticAxisMaxValue(Axis axis, float& out) {   // @ 0x10038394e
    if (!sharedRange_) {
        const float high = GetHighestAxisValue(axis);
        out = 0.0f < high ? high / 0.9f : 0.0f;
    } else {
        float a = GetHighestAxisValue(kAxisHorizontal), b = GetHighestAxisValue(kAxisVertical);
        a = a <= 0.0f ? 0.0f : a;
        b = b <= 0.0f ? 0.0f : b;
        out = (a <= b ? b : a) / 0.9f;
    }
}

void cGZScatterGraph::DrawSymbol(cIGZBuffer* buffer, cRZPoint& point, std::uint32_t series) {   // @ 0x1003844a4
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

void cGZScatterGraph::DrawGraphData(cIGZBuffer* buffer) {   // @ 0x1003842f0
    if (data_.empty()) return;
    const std::size_t pointCount = data_[0].size();
    if (pointCount == 0 || symbolFont_ == nullptr) return;   // the binary dereferences the font unchecked
    if (!symbolFont_->LockSurfaceForFontUse(buffer)) return;
    int previousX = 0, previousY = 0;
    for (std::size_t s = 0; s < data_.size(); ++s) {
        const bool lines = series_[s].drawLines;
        for (std::size_t k = 0; k < pointCount && k < data_[s].size(); ++k) {   // bounds guard: the binary reads past short series
            const cRZ2DPointFloat32& p = data_[s][k];
            cRZPoint pixel{ValueToPixel(p.x, kAxisHorizontal, true), ValueToPixel(p.y, kAxisVertical, true)};
            if (lines && k != 0)
                DrawLine(buffer, previousX, previousY, pixel.x, pixel.y, static_cast<int>(series_[s].color), 1);
            previousX = pixel.x;
            previousY = pixel.y;
            DrawSymbol(buffer, pixel, static_cast<std::uint32_t>(s));
        }
    }
    symbolFont_->UnlockSurfaceForFontUse(buffer);
}

}  // namespace gz
