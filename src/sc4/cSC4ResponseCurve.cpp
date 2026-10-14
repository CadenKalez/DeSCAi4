#include "sc4/cSC4ResponseCurve.h"

#include <cfloat>
#include <cmath>

// cSC4ResponseCurve::SetResponseCurveData(vector<SC4Point> const&) @ 0x1000ac220
bool cSC4ResponseCurve::SetResponseCurveData(const std::vector<SC4Point>& points) {
    data_.clear();
    for (std::size_t i = 0; i < points.size(); ++i) {
        ResponseCurveData entry{};
        entry.x = points[i].x;
        entry.y = points[i].y;
        if (i == 0) {
            entry.previousX = -FLT_MAX;
            entry.previousY = entry.y;
        } else {
            entry.previousX = points[i - 1].x;
            entry.previousY = points[i - 1].y;
        }
        float slope = entry.y - entry.previousY;
        if (std::fabs(slope) >= 1e-10f) {
            // The step in x must exceed slope * 1e-10 or the point is skipped (original behaviour).
            if (!(slope * 1e-10f < entry.x - entry.previousX)) continue;
            slope /= entry.x - entry.previousX;
        } else {
            slope = 0.0f;
        }
        entry.slope = slope;
        data_.push_back(entry);
    }
    return !points.empty();
}

// cSC4ResponseCurve::SetResponseCurveData(cISCProperty const*) @ 0x1000ac37a (property body)
bool cSC4ResponseCurve::SetResponseCurveData(const float* values, std::size_t valueCount) {
    std::vector<SC4Point> points;
    points.reserve(valueCount / 2);
    for (std::size_t i = 0; i < (valueCount >> 1); ++i) points.push_back({values[2 * i], values[2 * i + 1]});
    return SetResponseCurveData(points);
}

// cSC4ResponseCurve::GetResponseCurveData @ 0x1000ac1b8
void cSC4ResponseCurve::GetResponseCurveData(std::vector<SC4Point>& out) const {
    out.resize(data_.size());
    for (std::size_t i = 0; i < data_.size(); ++i) out[i] = {data_[i].x, data_[i].y};
}

// cSC4ResponseCurve::GetOutputValue(float) @ 0x1000ac4f6
float cSC4ResponseCurve::GetOutputValue(float input) const {
    const int count = static_cast<int>(data_.size());
    if (count == 0) return 0.0f;
    if (count > 1) {
        const int last = count - 1;
        int lo = 0;
        int hi = last;
        do {
            const int mid = (lo + hi) >> 1;
            if (input < data_[static_cast<std::size_t>(mid)].x) hi = mid; else lo = mid + 1;
        } while (lo < hi);
        if (lo > 0) {
            if (lo >= last && data_[static_cast<std::size_t>(last)].x <= input) {
                return data_[static_cast<std::size_t>(last)].y;
            }
            const ResponseCurveData& segment = data_[static_cast<std::size_t>(lo)];
            return (input - segment.previousX) * segment.slope + segment.previousY;
        }
    }
    return data_.front().y;
}

// cSC4ResponseCurve::GetMaxInputValue(float&) @ 0x1000ac578
bool cSC4ResponseCurve::GetMaxInputValue(float& out) const {
    if (data_.empty()) return false;
    out = data_.back().x;
    return true;
}
