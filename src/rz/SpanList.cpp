#include "rz/SpanList.h"

#include <cmath>

// 0x1002b42c0: starts at the topmost vertex (the first one on ties) and keeps the vertex order.
void cRZSpanList::SetPolygonSimple(const float* points, int count) {
    spans_.clear();
    if (count < 3) return;
    std::vector<sPolygonPoint> shifted(static_cast<std::size_t>(count));
    int top = 0;
    for (int i = 1; i < count; ++i) {
        if (points[top * 2 + 1] > points[i * 2 + 1]) top = i;
    }
    int source = top;
    for (int i = 0; i < count; ++i) {
        shifted[static_cast<std::size_t>(i)].x = points[source * 2] + -0.5f;
        shifted[static_cast<std::size_t>(i)].y = points[source * 2 + 1] + -0.5f;
        shifted[static_cast<std::size_t>(i)].scanline = static_cast<std::int32_t>(std::ceil(shifted[static_cast<std::size_t>(i)].y));
        if (++source >= count) source = 0;
    }
    RasterizeSimplePolygon(shifted.data(), count);
}

// 0x1002b44aa: the left edge walks the vertices forwards from 0, the right edge backwards from count - 1. Each pass of the
// loop emits one row; an edge that has reached the row of its end vertex picks the next vertex that lies below it.
void cRZSpanList::RasterizeSimplePolygon(const sPolygonPoint* p, int n) {
    int leftNext = 1;          // next candidate vertex of the left edge
    int leftStart = 0;         // vertex the left edge's current segment starts at
    int rightNext = n - 1;     // next vertex of the right edge
    int rightStart = 0;        // vertex the right edge's current segment starts at
    int y = p[0].scanline;
    int leftEnd = y, rightEnd = y;   // scanline at which the current segments end
    std::int64_t xLeft = 0, xRight = 0, stepLeft = 0, stepRight = 0;   // 16.16

    auto edge = [](const sPolygonPoint& from, const sPolygonPoint& to, int row, std::int64_t& x, std::int64_t& step) {
        const double slope = static_cast<double>((to.x - from.x) / (to.y - from.y));
        const double start = ((static_cast<double>(row) - static_cast<double>(from.y)) * slope + static_cast<double>(from.x)) * 65536.0 + 0.5;
        x = static_cast<std::int64_t>(std::floor(start)) + 0xffff;
        step = static_cast<std::int64_t>(std::floor(slope * 65536.0 + 0.5));
    };

    for (;;) {
        if (leftEnd == y) {
            if (leftNext >= n) return;
            int k = leftNext;
            while (p[k].scanline <= leftEnd) {
                leftStart = k;
                if (++k >= n) return;
            }
            edge(p[leftStart], p[k], leftEnd, xLeft, stepLeft);
            leftNext = k + 1;
            leftEnd = p[k].scanline;
            leftStart = k;
        }
        if (rightEnd == y) {
            int j = rightNext;
            if (p[j].scanline <= rightEnd) {
                for (;;) {
                    if (p[j].scanline < rightEnd) return;
                    --j;
                    if (j < 0) return;   // (the original runs off the vertex array here)
                    if (p[j].scanline > rightEnd) break;
                }
                rightStart = j + 1;
            }
            edge(p[rightStart], p[j], rightEnd, xRight, stepRight);
            rightStart = j;
            rightEnd = p[j].scanline;
            rightNext = j - 1;
        }
        cRZSpan span;
        span.y = y;
        span.x0 = static_cast<std::int16_t>(xLeft >> 16);
        span.x1 = static_cast<std::int16_t>(xRight >> 16);
        spans_.push_back(span);
        xLeft += stepLeft;
        xRight += stepRight;
        ++y;
    }
}
