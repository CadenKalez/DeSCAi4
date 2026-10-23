// cRZSpanList — a list of horizontal pixel spans covering a polygon (ctor 0x1002b3f72, dtor 0x1002b41ec,
// SetPolygonSimple 0x1002b42c0, RasterizeSimplePolygon 0x1002b44aa; SetEllipse 0x1002b3f86 and Draw 0x1002b4122 are not
// recovered here).
//
// Layout of the original: three vector pointers. Each entry is {int y, short x0, short x1} (8 bytes): row y covers the pixels
// x0 .. x1 - 1 (x1 may be smaller than x0 for a degenerate row).
//
// SetPolygonSimple takes a convex-ish polygon as x,y float pairs, shifts it by -0.5 (pixel centres), and walks its left
// and right edges from the topmost vertex downwards in 16.16 fixed point; the scanlines are the integer rows from
// ceil(top - 0.5) on.
#pragma once
#include <cstdint>
#include <vector>

struct cRZSpan {
    std::int32_t y;
    std::int16_t x0, x1;
};
static_assert(sizeof(cRZSpan) == 8);

class cRZSpanList {
public:
    // A polygon vertex after the half pixel shift: x, y and the first scanline at or below y.
    struct sPolygonPoint {
        float x, y;
        std::int32_t scanline;
    };

    void SetPolygonSimple(const float* points, int count);                   // 0x1002b42c0 (fewer than 3 points: empty)
    void RasterizeSimplePolygon(const sPolygonPoint* points, int count);     // 0x1002b44aa
    const std::vector<cRZSpan>& Spans() const { return spans_; }
    bool Empty() const { return spans_.empty(); }
    void Clear() { spans_.clear(); }

private:
    std::vector<cRZSpan> spans_;
};
