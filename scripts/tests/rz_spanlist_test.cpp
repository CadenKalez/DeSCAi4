// cRZSpanList::SetPolygonSimple / RasterizeSimplePolygon: rows and extents of triangles, quads and a rectangle.
#include <cmath>
#include <cstdio>
#include <vector>

#include "rz/SpanList.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

// Reference: the extent of a convex polygon (shifted by -0.5) on scanline y, as exact doubles.
static bool Extent(const std::vector<float>& pts, double y, double& lo, double& hi) {
    const std::size_t n = pts.size() / 2;
    lo = 1e9; hi = -1e9;
    bool any = false;
    for (std::size_t i = 0; i < n; ++i) {
        const double x0 = pts[i * 2] - 0.5, y0 = pts[i * 2 + 1] - 0.5;
        const double x1 = pts[((i + 1) % n) * 2] - 0.5, y1 = pts[((i + 1) % n) * 2 + 1] - 0.5;
        if (y0 == y1) continue;
        if (y < std::min(y0, y1) || y > std::max(y0, y1)) continue;
        const double x = x0 + (y - y0) * (x1 - x0) / (y1 - y0);
        lo = std::min(lo, x); hi = std::max(hi, x);
        any = true;
    }
    return any;
}

static void Check(const std::vector<float>& pts) {
    cRZSpanList list;
    list.SetPolygonSimple(pts.data(), static_cast<int>(pts.size() / 2));
    const auto& spans = list.Spans();
    CHECK(!spans.empty());
    double top = 1e9, bottom = -1e9;
    for (std::size_t i = 0; i < pts.size(); i += 2) { top = std::min<double>(top, pts[i + 1] - 0.5); bottom = std::max<double>(bottom, pts[i + 1] - 0.5); }
    CHECK(spans.front().y == static_cast<int>(std::ceil(top)));
    for (std::size_t i = 1; i < spans.size(); ++i) CHECK(spans[i].y == spans[i - 1].y + 1);   // consecutive rows
    CHECK(spans.back().y <= static_cast<int>(std::ceil(bottom)));
    CHECK(spans.back().y >= static_cast<int>(std::ceil(bottom)) - 2);
    for (const cRZSpan& s : spans) {
        double lo, hi;
        if (!Extent(pts, s.y, lo, hi)) continue;
        CHECK(std::abs(s.x0 - std::ceil(lo)) <= 1.0);
        CHECK(std::abs(s.x1 - std::ceil(hi)) <= 1.0);
        CHECK(s.x1 >= s.x0 - 1);
    }
}

int main() {
    Check({10, 10, 20, 40, 30, 12});                               // triangle (the left edge walks the vertices forwards)
    Check({10, 10, 10, 30, 30, 30, 30, 10});                      // rectangle
    Check({5.5f, 3.25f, 12, 20, 33, 27.4f, 40.75f, 8.5f});        // quad, fractional corners
    Check({20, 5, 5, 20, 20, 35, 35, 20});                        // diamond
    Check({30, 10, 10, 10, 10, 30});                              // top vertex first by y tie-break, other start
    // the rectangle has exact rows and extents
    {
        cRZSpanList list;
        const float rect[] = {10, 10, 10, 30, 30, 30, 30, 10};
        list.SetPolygonSimple(rect, 4);
        const auto& s = list.Spans();
        CHECK(s.size() >= 19 && s.front().y == 10 && s.front().x0 == 10 && s.front().x1 == 30);   // ceil(9.5)
        CHECK(s[5].x0 == 10 && s[5].x1 == 30);
    }
    // clockwise input (on screen) swaps the two walkers: x0 > x1 (kept; callers pass the winding the original expects)
    {
        cRZSpanList list;
        const float cw[] = {10, 10, 30, 10, 30, 30, 10, 30};
        list.SetPolygonSimple(cw, 4);
        CHECK(list.Spans()[3].x0 == 30 && list.Spans()[3].x1 == 10);
    }
    // fewer than three points: nothing
    {
        cRZSpanList list;
        const float line[] = {1, 1, 5, 5};
        list.SetPolygonSimple(line, 2);
        CHECK(list.Empty());
        const float tri[] = {0, 0, 4, 0, 0, 4};
        list.SetPolygonSimple(tri, 3);
        CHECK(!list.Empty());
        list.SetPolygonSimple(line, 2);
        CHECK(list.Empty());   // a new call clears the previous result
    }
    if (failures == 0) std::printf("rz_spanlist OK\n");
    return failures == 0 ? 0 : 1;
}
